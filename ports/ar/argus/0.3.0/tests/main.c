/* Parses a command line through the option macros and reads the values back:
   flags, strings, ints with defaults, the range validator, arrays, maps,
   positionals and the missing-required error. */
#include <argus.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "main.c:%d: check failed: %s\n", __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

/* The options array holds parse state, so each case declares its own. */
#define DECLARE_OPTIONS()                                                    \
ARGUS_OPTIONS(                                                               \
    options,                                                                 \
    HELP_OPTION(),                                                           \
    VERSION_OPTION(),                                                        \
    OPTION_FLAG('v', "verbose", HELP("Verbose")),                            \
    OPTION_STRING('o', "output", HELP("Output"), DEFAULT("output.txt")),     \
    OPTION_INT('p', "port", HELP("Port"), DEFAULT(8080), VALIDATOR(V_RANGE(1, 65535))),\
    OPTION_ARRAY_INT('i', "ints", HELP("Ints")),                             \
    OPTION_MAP_INT('m', "ports", HELP("Ports")),                             \
    POSITIONAL_STRING("input", HELP("Input")),                               \
)

int main(void)
{
    {
        DECLARE_OPTIONS()
        char *argv[] = {"prog", "--verbose", "--output=custom.txt", "--port=9000",
                        "--ints=1,2,3", "--ports=http=80,https=443", "input.txt"};
        argus_t a = argus_init(options, "prog", "1.0.0");
        CHECK(argus_parse(&a, 7, argv) == ARGUS_SUCCESS);
        CHECK(argus_get(&a, "verbose").as_bool == true);
        CHECK(strcmp(argus_get(&a, "output").as_string, "custom.txt") == 0);
        CHECK(argus_get(&a, "port").as_int == 9000);
        CHECK(argus_is_set(&a, "port"));
        CHECK(argus_count(&a, "ints") == 3);
        CHECK(argus_array_get(&a, "ints", 2).as_int == 3);
        CHECK(argus_map_get(&a, "ports", "https").as_int == 443);
        CHECK(strcmp(argus_get(&a, "input").as_string, "input.txt") == 0);
        argus_free(&a);
    }
    {
        DECLARE_OPTIONS()
        char *argv[] = {"prog", "input.txt"};
        argus_t a = argus_init(options, "prog", "1.0.0");
        CHECK(argus_parse(&a, 2, argv) == ARGUS_SUCCESS);
        CHECK(argus_get(&a, "verbose").as_bool == false);
        CHECK(strcmp(argus_get(&a, "output").as_string, "output.txt") == 0);
        CHECK(argus_get(&a, "port").as_int == 8080);
        CHECK(!argus_is_set(&a, "port"));
        argus_free(&a);
    }
    {
        DECLARE_OPTIONS()
        char *argv[] = {"prog", "--port=70000", "input.txt"};
        argus_t a = argus_init(options, "prog", "1.0.0");
        CHECK(argus_parse(&a, 3, argv) == ARGUS_ERROR_INVALID_RANGE);
        argus_free(&a);
    }
    {
        DECLARE_OPTIONS()
        char *argv[] = {"prog"};
        argus_t a = argus_init(options, "prog", "1.0.0");
        CHECK(argus_parse(&a, 1, argv) == ARGUS_ERROR_MISSING_REQUIRED);
        argus_free(&a);
    }
    return failures != 0;
}
