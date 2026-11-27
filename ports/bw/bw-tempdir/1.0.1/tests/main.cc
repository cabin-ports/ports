#include <bw/tempdir/tempdir.hpp>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace bw::tempdir;

static int failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond << "\n";                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

int main()
{
    fs::path always_path;
    {
        TempDir dir;
        always_path = dir.path();
        CHECK(fs::is_directory(always_path));
        CHECK(fs::equivalent(always_path.parent_path(), fs::temp_directory_path()));
        CHECK(always_path.filename().string().rfind("temp_dir_", 0) == 0);
        std::ofstream(always_path / "f.txt") << "x";
        CHECK(fs::exists(always_path / "f.txt"));
    }
    CHECK(!fs::exists(always_path));

    // never keeps the directory; the test removes it itself.
    fs::path never_path;
    {
        TempDir dir(Cleanup::never);
        never_path = dir.path();
    }
    CHECK(fs::is_directory(never_path));
    fs::remove_all(never_path);

    // on_success removes without an exception in flight and keeps it during one.
    fs::path ok_path, thrown_path;
    {
        TempDir dir(Cleanup::on_success);
        ok_path = dir.path();
    }
    CHECK(!fs::exists(ok_path));
    try
    {
        TempDir dir(Cleanup::on_success);
        thrown_path = dir.path();
        throw std::runtime_error("boom");
    }
    catch (const std::runtime_error&)
    {
    }
    CHECK(fs::is_directory(thrown_path));
    fs::remove_all(thrown_path);

    // Config: root path, prefix, and logging.
    std::vector<std::string> log;
    fs::path root;
    {
        TempDir outer(Cleanup::always);
        root = outer.path();
        auto config = Config()
                          .set_root_path(root)
                          .set_temp_dir_prefix("cabin")
                          .enable_logging([&](const std::string& m) { log.push_back(m); });
        fs::path inner_path;
        {
            TempDir inner(config);
            inner_path = inner.path();
            CHECK(fs::equivalent(inner_path.parent_path(), root));
            CHECK(inner_path.filename().string().rfind("cabin_", 0) == 0);
            CHECK(log.size() == 1 && log[0] == "TempDir create '" + inner_path.string() + "'");

            // Moving transfers the directory to the new owner.
            TempDir moved(std::move(inner));
            CHECK(moved.path() == inner_path);
            CHECK(fs::is_directory(inner_path));
        }
        CHECK(!fs::exists(inner_path));
        CHECK(log.size() == 2 && log[1] == "TempDir remove '" + inner_path.string() + "'");

        // Explicit cleanup, then the destructor finds nothing to do.
        TempDir explicit_dir(root, Cleanup::always);
        fs::path p = explicit_dir.path();
        explicit_dir.cleanup();
        CHECK(!fs::exists(p));
    }
    CHECK(!fs::exists(root));

    // A root path that is a regular file makes creation fail with TempDirException.
    TempDir holder;
    fs::path file = holder.path() / "plain";
    std::ofstream(file) << "x";
    bool thrown = false;
    try
    {
        TempDir bad(file);
    }
    catch (const TempDirException& e)
    {
        thrown = std::string(e.what()).rfind("TempDirExcepton: ", 0) == 0;
    }
    CHECK(thrown);

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
