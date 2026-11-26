#include <rapidjson/document.h>  // before uconfig: relies on the interface define

#include <uconfig/uconfig.h>
#include <uconfig/format/Env.h>
#include <uconfig/format/Rapidjson.h>

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      std::cerr << __FILE__ << ":" << __LINE__ << ": " #c << std::endl; \
      return 1;                                                        \
    }                                                                  \
  } while (0)

using Json = uconfig::RapidjsonFormat<>;

struct JsonConfig : uconfig::Config<Json> {
  uconfig::Variable<std::string> file;
  uconfig::Variable<unsigned> rotate_mb{1024};
  uconfig::Variable<int> timeout_ms{100};

  using uconfig::Config<Json>::Config;

  void Init(const std::string& p) override {
    Register<Json>(p + "/log/file", &file);
    Register<Json>(p + "/log/rotate_mb", &rotate_mb);
    Register<Json>(p + "/timeout_ms", &timeout_ms);
  }
};

struct EnvConfig : uconfig::Config<uconfig::EnvFormat> {
  uconfig::Variable<std::string> file;
  uconfig::Variable<unsigned> rotate_mb{1024};
  uconfig::Vector<int> ids;

  using uconfig::Config<uconfig::EnvFormat>::Config;

  void Init(const std::string& p) override {
    Register<uconfig::EnvFormat>(p + "_FILE", &file);
    Register<uconfig::EnvFormat>(p + "_ROTATE_MB", &rotate_mb);
    Register<uconfig::EnvFormat>(p + "_ID", &ids);
  }
};

static void SetEnv(const char* k, const char* v) {
#ifdef _WIN32
  _putenv_s(k, v);
#else
  setenv(k, v, 1);
#endif
}

int main() {
  // JSON: parse, with the optional variables defaulted or overridden.
  {
    rapidjson::Document doc;
    doc.Parse(R"({"log":{"file":"/var/log/app.log","rotate_mb":16}})");
    CHECK(!doc.HasParseError());
    JsonConfig cfg;
    CHECK(cfg.Parse(Json{}, "", &doc));
    CHECK(*cfg.file == "/var/log/app.log");
    CHECK(*cfg.rotate_mb == 16u);
    CHECK(*cfg.timeout_ms == 100);

    rapidjson::Document out(rapidjson::kObjectType);
    cfg.Emit(Json{}, "", &out);
    CHECK(out["log"]["file"] == "/var/log/app.log");
    CHECK(out["log"]["rotate_mb"].GetUint() == 16u);
    CHECK(out["timeout_ms"].GetInt() == 100);
  }

  // JSON: a missing required variable throws, or returns false on request.
  {
    rapidjson::Document doc;
    doc.Parse("{}");
    JsonConfig cfg;
    bool threw = false;
    try {
      cfg.Parse(Json{}, "", &doc);
    } catch (const uconfig::ParseError&) {
      threw = true;
    }
    CHECK(threw);
    JsonConfig soft;
    CHECK(!soft.Parse(Json{}, "", &doc, false));

    // A required value of the wrong JSON type counts as missing.
    rapidjson::Document bad;
    bad.Parse(R"({"log":{"file":5}})");
    JsonConfig typed;
    CHECK(!typed.Parse(Json{}, "", &bad, false));
  }

  // Environment: parse from variables, emit into a map.
  {
    SetEnv("CABIN_UCFG_T_FILE", "/tmp/a.log");
    SetEnv("CABIN_UCFG_T_ID_0", "7");
    SetEnv("CABIN_UCFG_T_ID_1", "9");
    EnvConfig cfg;
    CHECK(cfg.Parse(uconfig::EnvFormat{}, "CABIN_UCFG_T", nullptr));
    CHECK(*cfg.file == "/tmp/a.log");
    CHECK(*cfg.rotate_mb == 1024u);
    CHECK(cfg.ids->size() == 2 && (*cfg.ids)[0] == 7 && (*cfg.ids)[1] == 9);

    std::map<std::string, std::string> out;
    cfg.Emit(uconfig::EnvFormat{}, "CABIN_UCFG_T", &out);
    std::map<std::string, std::string> want{{"CABIN_UCFG_T_FILE", "/tmp/a.log"},
                                            {"CABIN_UCFG_T_ROTATE_MB", "1024"},
                                            {"CABIN_UCFG_T_ID_0", "7"},
                                            {"CABIN_UCFG_T_ID_1", "9"}};
    CHECK(out == want);

    EnvConfig missing;
    bool threw = false;
    try {
      missing.Parse(uconfig::EnvFormat{}, "CABIN_UCFG_NONE", nullptr);
    } catch (const uconfig::ParseError&) {
      threw = true;
    }
    CHECK(threw);
  }
  return 0;
}
