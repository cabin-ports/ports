#include <emhash/hash_set8.hpp>
#include <emhash/hash_table5.hpp>
#include <emhash/hash_table6.hpp>
#include <emhash/hash_table7.hpp>
#include <emhash/hash_table8.hpp>
#include <emhash/version.hpp>
#include <emilib/emihmap1.hpp>

#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const char *what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

template <class Map> static void check_thousand(const char *what) {
    Map m;
    for (int i = 0; i < 1000; ++i) {
        m[i] = i * 2;
    }
    bool ok = m.size() == 1000 && m.at(999) == 1998;
    for (int i = 0; ok && i < 1000; ++i) {
        ok = m.count(i) == 1 && m.at(i) == i * 2;
    }
    check(ok, what);
}

int main() {
    emhash8::HashMap<int, std::string> m;
    m[1] = "one";
    m.emplace(2, "two");
    m.insert({3, "three"});
    check(m.size() == 3, "emhash8 size");
    check(m.at(2) == "two", "emhash8 at");
    check(m.contains(3) && !m.contains(4), "emhash8 contains");
    m.erase(1);
    check(m.size() == 2 && m.count(1) == 0, "emhash8 erase");

    emhash7::HashMap<std::string, int> s;
    s["a"] = 1;
    s.emplace("b", 2);
    const auto b = s.find("b");
    check(s.size() == 2 && b != s.end() && b->second == 2, "emhash7 string keys");

    check_thousand<emhash5::HashMap<int, int>>("emhash5 1000 inserts");
    check_thousand<emhash6::HashMap<int, int>>("emhash6 1000 inserts");

    emhash8::HashSet<int> hs;
    hs.insert(5);
    hs.insert(5);
    check(hs.size() == 1 && hs.count(5) == 1, "emhash8 set dedupe");

    // Matches with SSE2 on x86-64 and through sse2neon.h on arm64.
    emilib::HashMap<int, int> e;
    e[7] = 70;
    e[8] = 80;
    check(e.size() == 2 && e[7] == 70 && e.count(9) == 0, "emilib map");

    check(EMHASH_VERSION_MAJOR == 1, "version major");

    if (failures != 0) {
        return 1;
    }
    std::printf("emhash consumer test passed\n");
    return 0;
}
