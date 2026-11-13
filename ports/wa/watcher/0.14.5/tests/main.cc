#include <wtr/watcher.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

namespace fs = std::filesystem;
using wtr::event;
using effect = enum wtr::event::effect_type;
using kind = enum wtr::event::path_type;

// Collects the events a watcher's thread delivers.
class recorder {
public:
  void add(event const& e) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      events_.push_back(e);
    }
    cv_.notify_all();
  }

  // Waits up to `limit` for an event satisfying `match`.
  bool wait_for(std::function<bool(event const&)> const& match,
                std::chrono::milliseconds limit) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, limit, [&] {
      for (event const& e : events_)
        if (match(e))
          return true;
      return false;
    });
  }

  bool wait_for(fs::path const& path, effect what, kind type) {
    return wait_for(
        [&](event const& e) {
          return e.path_name == path && e.effect_type == what &&
                 e.path_type == type;
        },
        std::chrono::seconds(10));
  }

  std::vector<event> snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<event> events_;
};

long long now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void check_strings() {
  struct {
    effect value;
    char const* name;
  } const effects[] = {{effect::rename, "rename"}, {effect::modify, "modify"},
                       {effect::create, "create"}, {effect::destroy, "destroy"},
                       {effect::owner, "owner"},   {effect::other, "other"}};
  for (auto const& e : effects)
    CHECK(wtr::to<std::string>(e.value) == e.name);
  struct {
    kind value;
    char const* name;
  } const kinds[] = {{kind::dir, "dir"},         {kind::file, "file"},
                     {kind::hard_link, "hard_link"},
                     {kind::sym_link, "sym_link"}, {kind::watcher, "watcher"},
                     {kind::other, "other"}};
  for (auto const& k : kinds)
    CHECK(wtr::to<std::string>(k.value) == k.name);
  CHECK(wtr::to<std::string_view>(effect::destroy) == "destroy");
  CHECK(wtr::to<std::wstring>(kind::hard_link) == L"hard_link");

  // A rename carries its destination as the associated event; the JSON form
  // keys the event by its nanosecond timestamp.
  event moved{event{"/cabin/a.txt", effect::rename, kind::file},
              event{"/cabin/b.txt", effect::rename, kind::file}};
  moved.effect_time = 1700000000123456789;
  std::string const json =
      R"("1700000000123456789":{"effect_type":"rename","path_name":)"
      R"("/cabin/a.txt","path_type":"file","associated":{"effect_type":)"
      R"("rename","path_name":"/cabin/b.txt","path_type":"file"}})";
  CHECK(wtr::to<std::string>(moved) == json);
  std::ostringstream out;
  out << moved;
  CHECK(out.str() == json);
  CHECK(wtr::to<std::wstring>(moved) ==
        std::wstring(json.begin(), json.end()));

  // Copies are deep: the associated event is duplicated, and equality
  // compares it.
  event copy = moved;
  CHECK(copy == moved);
  CHECK(copy.associated && copy.associated.get() != moved.associated.get());
  copy.associated->path_name = "/cabin/c.txt";
  CHECK(copy != moved);
  CHECK(moved.associated->path_name == "/cabin/b.txt");
}

void check_missing_directory(fs::path const& dir) {
  fs::path const missing = dir / "missing";
  std::string const where = fs::absolute(missing).string();
  recorder seen;
  {
    wtr::watch watcher(missing, [&](event const& e) { seen.add(e); });
    CHECK(!watcher.close());
  }
  std::vector<event> const events = seen.snapshot();
  CHECK(events.size() == 2);
  if (events.size() == 2) {
    CHECK(events[0].path_name == "e/self/live@" + where);
    CHECK(events[0].effect_type == effect::create);
    CHECK(events[0].path_type == kind::watcher);
    CHECK(events[1].path_name == "e/self/die@" + where);
    CHECK(events[1].effect_type == effect::destroy);
    CHECK(events[1].path_type == kind::watcher);
  }
}

void check_watch(fs::path const& dir) {
  std::string const where = fs::absolute(dir).string();
  long long const started = now_ns();
  recorder seen;
  wtr::watch watcher(dir, [&](event const& e) { seen.add(e); });

  CHECK(seen.wait_for(fs::path("s/self/live@" + where), effect::create,
                      kind::watcher));

  // There is no readiness signal (upstream's readme, "Ready State"), so
  // create probe files until the watcher reports one.
  bool ready = false;
  for (int i = 0; i < 40 && !ready; ++i) {
    fs::path const probe = dir / ("probe-" + std::to_string(i));
    std::ofstream{probe}.put('p');
    ready = seen.wait_for(
        [&](event const& e) {
          return e.path_name == probe && e.effect_type == effect::create;
        },
        std::chrono::milliseconds(250));
  }
  CHECK(ready);

  fs::path const alpha = dir / "alpha.txt";
  fs::path const beta = dir / "beta.txt";
  fs::path const gamma = dir / "gamma";
  std::ofstream{alpha} << "cabin";
  CHECK(seen.wait_for(alpha, effect::create, kind::file));

  fs::rename(alpha, beta);
  CHECK(seen.wait_for(
      [&](event const& e) {
        return e.path_name == alpha && e.effect_type == effect::rename &&
               e.path_type == kind::file && e.associated &&
               e.associated->path_name == beta &&
               e.associated->effect_type == effect::rename &&
               e.associated->path_type == kind::file;
      },
      std::chrono::seconds(10)));

  fs::remove(beta);
  CHECK(seen.wait_for(beta, effect::destroy, kind::file));

  fs::create_directory(gamma);
  CHECK(seen.wait_for(gamma, effect::create, kind::dir));

  // Watching is recursive, including directories created after it began.
  fs::path const delta = gamma / "delta.txt";
  std::ofstream{delta} << "cabin";
  CHECK(seen.wait_for(delta, effect::create, kind::file));
  std::ofstream{delta, std::ios::app} << "more";
  CHECK(seen.wait_for(delta, effect::modify, kind::file));

  CHECK(watcher.close());
  long long const finished = now_ns();

  std::vector<event> const events = seen.snapshot();
  CHECK(!events.empty());
  if (!events.empty()) {
    CHECK(events.front().path_name == "s/self/live@" + where);
    CHECK(started <= events.front().effect_time &&
          events.front().effect_time <= finished);
    CHECK(events.back().path_name == "s/self/die@" + where);
    CHECK(events.back().effect_type == effect::destroy);
    CHECK(events.back().path_type == kind::watcher);
  }
}

} // namespace

int main() {
  check_strings();

  // FSEvents reports real paths, so watch a canonical directory (macOS's
  // temporary directory sits behind the /var symlink).
  fs::path const dir =
      fs::canonical(fs::temp_directory_path()) /
      ("cabin-watcher-" + std::to_string(now_ns()) + "-" +
       std::to_string(std::random_device{}()));
  if (!fs::create_directory(dir)) {
    std::fprintf(stderr, "cannot create %s\n", dir.string().c_str());
    return 1;
  }
  check_missing_directory(dir);
  check_watch(dir);
  std::error_code ignored;
  fs::remove_all(dir, ignored);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("watcher ok");
  return 0;
}
