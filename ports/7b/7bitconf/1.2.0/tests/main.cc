#include <SevenBit/Conf.hpp>

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace sb::cf;

namespace
{
int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void setEnv(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}
} // namespace

int main()
{
    setEnv("CABINTEST_Logging__Level", "warn");
    setEnv("CABINTEST_Array__1___uint", "7");

    std::istringstream stream(R"({"Name":"stream","Logging":{"Level":"info","Depth":3},"Array":[1,2,3]})");
    const std::vector<std::string_view> args{"--Name", "cmd", "Logging:Extra!bool=true", "/Array:0!int", "-5"};

    // Later sources override earlier ones.
    const IConfiguration::Ptr conf = ConfigurationBuilder{}
                                         .addJsonStream(stream)
                                         .addJson({{"Name", "object"}, {"Only", "object"}})
                                         .AddInMemory("Mem:Nested", "m")
                                         .addEnvironmentVariables("CABINTEST_")
                                         .addCommandLine(args)
                                         .build();

    check(conf->at("Name").get_string() == "cmd", "command line overrides json");
    check(conf->at("Only").get_string() == "object", "json object source kept");
    check(conf->deepAt("Logging:Level").get_string() == "warn", "env var overrides json, __ is a nesting separator");
    check(conf->deepAt("Logging:Depth").get_unsigned() == 3, "untouched stream value kept");
    check(conf->deepAt("Logging:Extra").get_boolean(), "command line !bool type");
    check(conf->deepAt("Array:0").get_signed() == -5, "command line !int array element");
    check(conf->deepAt("Array:1").get_unsigned() == 7, "env var ___ type separator and array index");
    check(conf->deepAt("Array:2").get_unsigned() == 3, "array tail kept");
    check(conf->deepAt("Mem:Nested").get_string() == "m", "in-memory nested key");
    check(conf->find("Missing") == nullptr, "find returns null for a missing key");

    bool threw = false;
    try
    {
        (void)conf->deepAt("Missing:Key");
    }
    catch (const ValueNotFoundException &)
    {
        threw = true;
    }
    check(threw, "deepAt throws ValueNotFoundException");

    threw = false;
    try
    {
        ConfigurationBuilder{}.addJsonFile("cabin-7bitconf-no-such-file.json").build();
    }
    catch (const ConfigFileNotFoundException &)
    {
        threw = true;
    }
    check(threw, "missing json file throws ConfigFileNotFoundException");

    check(ConfigurationBuilder{}.addJsonFile("cabin-7bitconf-no-such-file.json", true).build()->rootAsObject().empty(),
          "optional missing json file is ignored");

    if (failures == 0)
        std::printf("OK\n");
    return failures == 0 ? 0 : 1;
}
