// The C++ consumer: load a TOML file, then log through the configured logger.
#include <spdlog_setup/spdlog_setup.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

static bool ok = true;

static void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

static void write(const std::string &path, const std::string &text) {
    std::ofstream(path, std::ios::binary) << text;
}

static std::string read(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    const std::string conf = (dir / "cabin-spdlog-setup-test.toml").generic_string();
    const std::string out = (dir / "cabin-spdlog-setup-test.log").generic_string();

    write(conf,
          std::string("[[sink]]\n"
          "name = \"file\"\n"
          "type = \"basic_file_sink_st\"\n"
          "filename = \"" + out + "\"\n"
          "truncate = true\n"
          "\n"
          "[[pattern]]\n"
          "name = \"plain\"\n"
          "value = \"%l|%n|%v\"\n"
          "\n"
          "[[logger]]\n"
          "name = \"app\"\n"
          "sinks = [\"file\"]\n"
          "pattern = \"plain\"\n"
          "level = \"warn\"\n"));
    spdlog_setup::from_file(conf);

    auto logger = spdlog::get("app");
    check(logger != nullptr, "spdlog::get returns the configured logger");
    if (logger) {
        check(logger->level() == spdlog::level::warn, "logger level is warn");
        logger->info("dropped {}", 1);
        logger->error("kept {}", 2);
        logger->flush();
        check(read(out) == "error|app|kept 2" + std::string(spdlog::details::os::default_eol),
              "only the error message reaches the file sink, formatted by the pattern");
    }
    spdlog::drop_all();

    write(conf, "[[sink]]\nname = \"null\"\ntype = \"null_sink_st\"\n[[logger]]\nname = \"bad\"\nsinks = [\"nosuchsink\"]\n");
    bool threw = false;
    try {
        spdlog_setup::from_file(conf);
    } catch (const spdlog_setup::setup_error &e) {
        threw = std::string(e.what()).find("nosuchsink") != std::string::npos;
    }
    check(threw, "unknown sink raises setup_error naming it");

    write(conf, "a = 1\na = 2\n");
    threw = false;
    try {
        spdlog_setup::from_file(conf);
    } catch (const spdlog_setup::setup_error &e) {
        threw = std::string(e.what()).find("at line 2") != std::string::npos;
    }
    check(threw, "unparsable TOML raises setup_error carrying the parse error");

    threw = false;
    try {
        spdlog_setup::from_file("spdlog_setup_no_such_file.toml");
    } catch (const spdlog_setup::setup_error &) {
        threw = true;
    }
    check(threw, "missing file raises setup_error");

    std::remove(conf.c_str());
    std::remove(out.c_str());
    if (ok) std::puts("ok");
    return ok ? 0 : 1;
}
