#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/dialects/ManTone.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace v2 = cpp_pinyin::v2;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond)                                             \
    do {                                                        \
        if (!(cond)) {                                          \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            ++failures;                                         \
        }                                                       \
    } while (0)

static void put(const fs::path &file, const std::string &text) {
    std::ofstream(file, std::ios::binary) << text;
}

int main() {
    // Dictionary-free tone helpers.
    CHECK(v2::ManTone::toneToTone3(u"zh\u014dng") == u"zhong1");
    CHECK(v2::ManTone::toneToInitials(u"zh\u014dng") == u"zh");
    CHECK(v2::ManTone::toneToFinals(u"zh\u014dng") == u"ong");
    CHECK(v2::ManTone::toneToFinals3(u"zh\u014dng") == u"ong1");
    CHECK(v2::ManTone::toneToFirstLetter(u"zh\u014dng") == u"z");

    // A minimal Mandarin dictionary in upstream's format (UTF-8 bytes).
    const fs::path root = fs::temp_directory_path() / "cabin-cpp-pinyin2-cc";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "mandarin");
    put(root / "mandarin" / "format.txt", "2\n");
    put(root / "mandarin" / "word.txt",
        "\xe4\xb8\xad:zh\xc5\x8dng,zh\xc3\xb2ng\n"
        "\xe5\x9b\xbd:gu\xc3\xb3\n"
        "\xe5\xa5\x96:ji\xc7\x8eng\n");
    put(root / "mandarin" / "phrases_dict.txt",
        "\xe4\xb8\xad\xe5\xa5\x96:zh\xc3\xb2ng,ji\xc7\x8eng\n");
    put(root / "mandarin" / "phrases_map.txt", "\xe4\xb8\xad:2\n");
    put(root / "mandarin" / "trans_word.txt", "\xe5\x9c\x8b:\xe5\x9b\xbd\n");

    const v2::Registry registry;
    v2::Outcome<v2::Engine> opened = v2::Engine::open(registry, root, "mandarin");
    CHECK(opened.has_value());
    if (opened.has_value()) {
        const v2::Engine &engine = opened.value();
        v2::EngineConfig cfg;
        cfg.style = v2::MandarinStyle::TONE3;

        // Characters take their first reading.
        v2::Outcome<v2::Result> plain = engine.query("\xe4\xb8\xad\xe5\x9b\xbd", cfg);
        CHECK(plain.has_value() && plain.value().join() == "zhong1 guo2");
        // A phrase entry overrides the first reading of its first character.
        v2::Outcome<v2::Result> phrase = engine.query("\xe4\xb8\xad\xe5\xa5\x96", cfg);
        CHECK(phrase.has_value() && phrase.value().join() == "zhong4 jiang3");

        v2::Outcome<std::vector<std::string>> all = engine.readings("\xe4\xb8\xad", cfg);
        CHECK(all.has_value() && all.value() == std::vector<std::string>({"zhong1", "zhong4"}));
        CHECK(engine.isPolyphonic("\xe4\xb8\xad"));
        CHECK(!engine.isPolyphonic("\xe5\x9b\xbd"));
        v2::Outcome<std::string> simplified = engine.toSimplified("\xe5\x9c\x8b");
        CHECK(simplified.has_value() && simplified.value() == "\xe5\x9b\xbd");
    }
    fs::remove_all(root, ec);

    // A missing dictionary directory is a Load error.
    v2::Outcome<v2::Engine> missing = v2::Engine::open(registry, root / "absent", "mandarin");
    CHECK(!missing.has_value());
    if (!missing.has_value()) CHECK(missing.error().code == v2::Code::Load);

    return failures == 0 ? 0 : 1;
}
