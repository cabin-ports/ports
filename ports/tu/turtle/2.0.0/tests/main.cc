// Boost.Test is turtle's default error policy and supplies main().
#define BOOST_TEST_MODULE turtle_probe
#include <boost/test/results_collector.hpp>
#include <boost/test/unit_test.hpp>
#include <turtle/mock.hpp>

struct calculator {
    virtual ~calculator() = default;
    virtual int add(int, int) = 0;
};

MOCK_BASE_CLASS(mock_calculator, calculator) {
    MOCK_METHOD(add, 2)
};

BOOST_AUTO_TEST_CASE(expectation_with_returns) {
    mock_calculator c;
    MOCK_EXPECT(c.add).once().with(2, 3).returns(5);
    BOOST_TEST(c.add(2, 3) == 5);
    BOOST_TEST(MOCK_VERIFY(c.add));
}

BOOST_AUTO_TEST_CASE(constraints_in_sequence) {
    mock_calculator c;
    mock::sequence s;
    MOCK_EXPECT(c.add).once().in(s).with(mock::less(10), mock::any).returns(1);
    MOCK_EXPECT(c.add).once().in(s).returns(2);
    BOOST_TEST(c.add(1, 0) == 1);
    BOOST_TEST(c.add(20, 0) == 2);
    BOOST_TEST(mock::verify(c));
}

BOOST_AUTO_TEST_CASE(actions_and_reset) {
    mock_calculator c;
    MOCK_EXPECT(c.add).calls([](int a, int b) { return a * b; });
    BOOST_TEST(c.add(6, 7) == 42);
    mock::reset(c);
    MOCK_EXPECT(c.add).returns(0);
    BOOST_TEST(c.add(6, 7) == 0);
}

// An unmet expectation is reported through Boost.Test as one failed
// assertion, which expected_failures absorbs; the REQUIRE aborts the case,
// failing the run, if none was reported.
BOOST_AUTO_TEST_CASE(missing_call_is_reported, *boost::unit_test::expected_failures(1)) {
    const auto failed = [] {
        return boost::unit_test::results_collector
            .results(boost::unit_test::framework::current_test_case().p_id)
            .p_assertions_failed.get();
    };
    mock_calculator c;
    MOCK_EXPECT(c.add).once().returns(0);
    const auto before = failed();
    BOOST_TEST(!MOCK_VERIFY(c.add));
    BOOST_REQUIRE_EQUAL(failed(), before + 1);
    mock::reset(c);
}
