// Runs a minicoro coroutine from C++: it fills a std::vector across yields,
// hands a struct back through the storage, and, run to completion, destroys
// its local objects (minicoro.h:53 requires running it until it dies for
// that).
#define MINICORO_IMPL
#include <minicoro.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
int live_guards = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

struct Guard {
    Guard() { ++live_guards; }
    ~Guard() { --live_guards; }
};

struct Point {
    int x;
    int y;
};

} // namespace

// The entry point has C linkage, matching the type mco_desc_init declares.
extern "C" {
static void split_words(mco_coro *co)
{
    Guard guard;
    const std::string text = "alpha beta gamma";
    std::vector<std::string> *out = static_cast<std::vector<std::string> *>(mco_get_user_data(co));
    std::string::size_type start = 0;
    while (start < text.size()) {
        std::string::size_type end = text.find(' ', start);
        if (end == std::string::npos)
            end = text.size();
        out->push_back(text.substr(start, end - start));
        check(mco_yield(co) == MCO_SUCCESS, "yield");
        start = end + 1;
    }
    Point done = {3, 4};
    check(mco_push(co, &done, sizeof(done)) == MCO_SUCCESS, "push a struct");
}
}

int main()
{
    std::vector<std::string> words;
    mco_desc desc = mco_desc_init(split_words, 0);
    desc.user_data = &words;
    mco_coro *co = 0;
    check(mco_create(&co, &desc) == MCO_SUCCESS && co != 0, "create");
    if (co == 0)
        return 1;

    int resumes = 0;
    while (mco_status(co) == MCO_SUSPENDED && resumes < 10) {
        check(mco_resume(co) == MCO_SUCCESS, "resume");
        ++resumes;
        if (mco_status(co) == MCO_SUSPENDED) {
            check(live_guards == 1, "locals live while suspended");
            check(words.size() == static_cast<std::vector<std::string>::size_type>(resumes),
                  "one word per resume");
        }
    }
    check(resumes == 4 && mco_status(co) == MCO_DEAD, "three yields, then the end");
    check(live_guards == 0, "locals destroyed when the coroutine finished");
    check(words.size() == 3 && words[0] == "alpha" && words[1] == "beta" && words[2] == "gamma", "words");

    Point done = {0, 0};
    check(mco_pop(co, &done, sizeof(done)) == MCO_SUCCESS && done.x == 3 && done.y == 4, "pop a struct");
    check(mco_destroy(co) == MCO_SUCCESS, "destroy");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("minicoro 0.1.3 ok (C++)");
    return 0;
}
