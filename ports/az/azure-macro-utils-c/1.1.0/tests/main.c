/* Enums from MU_DEFINE_ENUM and MU_DEFINE_ENUM_2 convert to and from their
   names and print through PRI_MU_ENUM, MU_DEFINE_STRUCT declares the listed
   fields in order, and the counting, iteration, conditional and arithmetic
   macros expand to the values macro_utils.h documents. */
#include <azure_macro_utils/macro_utils.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, int line, const char* what)
{
    if (!ok)
    {
        fprintf(stderr, "main.c:%d: check failed: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(expr) check((expr) != 0, __LINE__, #expr)

#define FRUIT_VALUES APPLE, PEAR, PLUM
MU_DEFINE_ENUM(FRUIT, FRUIT_VALUES)
MU_DEFINE_ENUM_STRINGS(FRUIT, FRUIT_VALUES)

#define HTTP_CODE_VALUES HTTP_OK, 200, HTTP_NOT_FOUND, 404
MU_DEFINE_ENUM_2(HTTP_CODE, HTTP_CODE_VALUES)
MU_DEFINE_ENUM_STRINGS_2(HTTP_CODE, HTTP_CODE_VALUES)

MU_DEFINE_LOCAL_ENUM_WITHOUT_INVALID(LEVEL, LOW, HIGH)

MU_DEFINE_STRUCT(POINT, int, x, double, y)

/* The generated tables count up to 123 arguments. */
#define ONE_TO_123 \
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, \
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, \
    39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, \
    57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, \
    75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, \
    93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107,     \
    108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,   \
    122, 123

#define ADD(x) + (x)
#define ADD_DIFFERENCE(a, b) + ((a) - (b))
#define ADD_SCALED_DIFFERENCE(k, a, b) + (k) * ((a) - (b))
#define ADD_AFFINE(k, c, x) + (k) * (x) + (c)
#define ADD_WEIGHTED(n, x) + (n) * (x)
#define ADD_SUM(n, base) + ((n) + (base))
#define JOIN_PAIR(a, b) a b

static void enums(void)
{
    char text[32];
    FRUIT parsed = APPLE;

    CHECK(FRUIT_INVALID == 0 && APPLE == 1 && PLUM == 3);
    CHECK(MU_ENUM_VALUE_COUNT(FRUIT_VALUES) == 4);
    CHECK(strcmp(MU_ENUM_TO_STRING(FRUIT, PEAR), "PEAR") == 0);
    CHECK(strcmp(MU_ENUM_TO_STRING(FRUIT, FRUIT_INVALID), "FRUIT_INVALID") == 0);
    CHECK(strcmp(MU_ENUM_TO_STRING(FRUIT, (FRUIT)(PLUM + 1)), "NULL") == 0);
    CHECK(MU_STRING_TO_ENUM("PLUM", FRUIT, &parsed) == 0 && parsed == PLUM);
    parsed = APPLE;
    CHECK(MU_STRING_TO_ENUM("plum", FRUIT, &parsed) != 0 && parsed == APPLE);
    CHECK(MU_STRING_TO_ENUM(NULL, FRUIT, &parsed) != 0);
    CHECK(MU_STRING_TO_ENUM("PEAR", FRUIT, NULL) != 0);

    CHECK(HTTP_CODE_INVALID == (int)0xDDDDDDDD);
    CHECK(HTTP_OK == 200 && HTTP_NOT_FOUND == 404);
    CHECK(MU_ENUM_2_VALUE_COUNT(HTTP_CODE_VALUES) == 3);
    CHECK(strcmp(MU_ENUM_TO_STRING_2(HTTP_CODE, HTTP_NOT_FOUND), "HTTP_NOT_FOUND") == 0);
    CHECK(strcmp(MU_ENUM_TO_STRING_2(HTTP_CODE, (HTTP_CODE)201), "NULL") == 0);

    CHECK(LOW == 0 && HIGH == 1);
    CHECK(strcmp(MU_ENUM_TO_STRING(LEVEL, HIGH), "HIGH") == 0);
    CHECK(strcmp(MU_ENUM_TO_STRING(LEVEL, (LEVEL)2), "NULL") == 0);

    snprintf(text, sizeof text, "%" PRI_MU_ENUM, MU_ENUM_VALUE(FRUIT, PEAR));
    CHECK(strcmp(text, "PEAR (2)") == 0);
    snprintf(text, sizeof text, "%" PRI_MU_ENUM, MU_ENUM_VALUE_2(HTTP_CODE, HTTP_OK));
    CHECK(strcmp(text, "HTTP_OK (200)") == 0);
}

static void structs_and_counts(void)
{
    POINT point = {3, 0.5};
    int numbers[] = {1, 2, 3, 4, 5};
    int one = MU_EAT_EMPTY_ARGS(1);
    int kept[] = {MU_EAT_EMPTY_ARGS(, 4, , 5)};
    const char* none = NULL;
    const char* name = "name";

    CHECK(point.x == 3 && point.y == 0.5);
    CHECK(MU_COUNT_ARG() == 0);
    CHECK(MU_COUNT_ARG(a) == 1);
    CHECK(MU_COUNT_ARG(a, b, c) == 3);
    CHECK(MU_COUNT_ARG(ONE_TO_123) == 123);
    CHECK(MU_COUNT_ARRAY_ITEMS(numbers) == 5);

    CHECK(one == 1);
    CHECK(MU_COUNT_ARRAY_ITEMS(kept) == 2 && kept[0] == 4 && kept[1] == 5);

    CHECK(strcmp(MU_P_OR_NULL(none), "NULL") == 0);
    CHECK(strcmp(MU_P_OR_NULL(name), "name") == 0);
    CHECK(strcmp(MU_TOSTRING(MU_C3(fo, o, bar)), "foobar") == 0);
}

static void iteration(void)
{
    const char* joined = MU_FOR_EACH_2_REVERSE(JOIN_PAIR, "a", "b", "c", "d");

    CHECK((0 MU_FOR_EACH_1(ADD, 1, 2, 3, 4)) == 10);
    CHECK((0 MU_FOR_EACH_1(ADD, ONE_TO_123)) == 123 * 124 / 2);
    CHECK((0 MU_FOR_EACH_2(ADD_DIFFERENCE, 9, 2, 5, 1)) == 11);
    CHECK((0 MU_FOR_EACH_1_KEEP_1(ADD_DIFFERENCE, 10, 1, 2, 3)) == 24);
    CHECK((0 MU_FOR_EACH_1_KEEP_2(ADD_AFFINE, 10, 1, 2, 3)) == 52);
    CHECK((0 MU_FOR_EACH_2_KEEP_1(ADD_SCALED_DIFFERENCE, 3, 5, 1, 9, 2)) == 33);
    /* The counters run down: 3 * 1 + 2 * 10 + 1 * 100. */
    CHECK((0 MU_FOR_EACH_1_COUNTED(ADD_WEIGHTED, 1, 10, 100)) == 123);
    /* Pairs count arguments: 4 * (5 - 1) + 2 * (9 - 2). */
    CHECK((0 MU_FOR_EACH_2_COUNTED(ADD_SCALED_DIFFERENCE, 5, 1, 9, 2)) == 30);
    CHECK(strcmp(joined, "cdab") == 0);
    /* MU_DO(2, ...) invokes the macro for 2, 1 and 0. */
    CHECK((0 MU_DO(2, ADD_SUM, 10)) == 33);
}

static void conditions_and_arithmetic(void)
{
    CHECK(MU_IF(0, 1, 2) == 2);
    CHECK(MU_IF(1, 1, 2) == 1);
    CHECK(MU_IF(7, 1, 2) == 1);
    CHECK(MU_ISEMPTY() == 1);
    CHECK(MU_ISEMPTY(a) == 0);
    CHECK(MU_ISEMPTY(a, b) == 0);

    /* The arithmetic macros expand to single number tokens. */
    CHECK(strcmp(MU_TOSTRING(MU_INC(41)), "42") == 0);
    CHECK(strcmp(MU_TOSTRING(MU_DEC(2048)), "2047") == 0);
    CHECK(strcmp(MU_TOSTRING(MU_DIV2(9)), "4") == 0);
    CHECK(strcmp(MU_TOSTRING(MU_MOD2(9)), "1") == 0);
}

int main(void)
{
    enums();
    structs_and_counts();
    iteration();
    conditions_and_arithmetic();
    if (failures != 0)
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("azure-macro-utils-c C consumer test passed");
    return 0;
}
