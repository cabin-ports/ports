// The C++ consumer: cmocka.h from C++, standard headers first (with MSVC
// it defines inline as a macro), and integer, boolean, string and memory
// assertions in a group that passes and in one whose single failing
// assertion is its only failure.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <cmocka.h>

namespace {

bool reached_after_failure = false;

void integers_and_booleans(void **) {
    assert_int_equal(6 * 7, 42);
    assert_uint_equal(sizeof(std::uint32_t), 4u);
    assert_true(std::strlen("abc") == 3);
    assert_false(std::strcmp("abc", "abd") == 0);
}

void strings_and_memory(void **) {
    char name[8];
    std::strcpy(name, "cab");
    std::strcat(name, "in");
    assert_string_equal(name, "cabin");
    assert_memory_equal(name, "cabin", 6);
    assert_string_not_equal(name, "cabins");
}

void failing_string_assertion(void **) {
    assert_string_equal("cabin", "cabins");
    reached_after_failure = true;
}

} // namespace

int main() {
    const struct CMUnitTest passing[] = {
        cmocka_unit_test(integers_and_booleans),
        cmocka_unit_test(strings_and_memory),
    };
    const struct CMUnitTest one_failure[] = {
        cmocka_unit_test(strings_and_memory),
        cmocka_unit_test(failing_string_assertion),
    };
    int passing_failures = cmocka_run_group_tests(passing, NULL, NULL);
    int expected_failures = cmocka_run_group_tests(one_failure, NULL, NULL);

    if (passing_failures != 0 || expected_failures != 1 || reached_after_failure) {
        std::fprintf(stderr, "FAIL: passing group %d, one-failure group %d\n", passing_failures,
                     expected_failures);
        return 1;
    }
    std::puts("cmocka C++ consumer: ok");
    return 0;
}
