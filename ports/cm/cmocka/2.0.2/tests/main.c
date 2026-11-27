/* The C consumer: a mocked function's queued return values and parameter
   checks, string and memory assertions on a test_malloc block, the version
   macros, and a group in which one failing assertion ends its test and is
   the group's only failure. */
#include <cmocka.h>
#include <cmocka_version.h>

#include <stdio.h>
#include <string.h>

static int read_sensor(int channel) {
    check_expected_int(channel);
    return (int)mock_int();
}

static int average_of_two_reads(int channel) {
    int first = read_sensor(channel);
    return (first + read_sensor(channel)) / 2;
}

static void mocked_reads(void **state) {
    (void)state;
    expect_int_value_count(read_sensor, channel, 3, 2);
    will_return_int(read_sensor, 40);
    will_return_int(read_sensor, 44);
    assert_int_equal(average_of_two_reads(3), 42);
}

static void strings_and_memory(void **state) {
    char *copy = test_malloc(6);
    (void)state;
    assert_non_null(copy);
    memcpy(copy, "cabin", 6);
    assert_string_equal(copy, "cabin");
    assert_string_not_equal(copy, "cabim");
    assert_memory_equal(copy, "cab", 3);
    assert_memory_not_equal(copy, "cap", 3);
    test_free(copy);
}

static void version_macros(void **state) {
    (void)state;
    assert_int_equal(CMOCKA_VERSION_MAJOR, 2);
    assert_int_equal(CMOCKA_VERSION_MINOR, 0);
    assert_int_equal(CMOCKA_VERSION_MICRO, 2);
    assert_int_equal(CMOCKA_VERSION_INT, 0x020002);
}

static int reached_after_failure = 0;

static void failing_assertion(void **state) {
    (void)state;
    assert_int_equal(6 * 7, 43);
    reached_after_failure = 1;
}

int main(void) {
    const struct CMUnitTest passing[] = {
        cmocka_unit_test(mocked_reads),
        cmocka_unit_test(strings_and_memory),
        cmocka_unit_test(version_macros),
    };
    const struct CMUnitTest one_failure[] = {
        cmocka_unit_test(version_macros),
        cmocka_unit_test(failing_assertion),
    };
    int passing_failures = cmocka_run_group_tests(passing, NULL, NULL);
    int expected_failures = cmocka_run_group_tests(one_failure, NULL, NULL);

    if (passing_failures != 0 || expected_failures != 1 || reached_after_failure) {
        fprintf(stderr, "FAIL: passing group %d, one-failure group %d, reached %d\n",
                passing_failures, expected_failures, reached_after_failure);
        return 1;
    }
    puts("cmocka C consumer: ok");
    return 0;
}
