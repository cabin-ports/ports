// C++98: the probe builds this at the declared c++98, so Do() takes plain functions rather than
// lambdas, as upstream's checkFunctorsCalled (HippoMocksTest/test_do.cpp) does.
#include <hippomocks.h>

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

class Store {
public:
    virtual ~Store() {}
    virtual int get(int key) = 0;
    virtual void put(int key, int value) = 0;
    virtual void open() = 0;
    virtual void close() = 0;
};

int putCalls = 0;
int putSum = 0;
void recordPut(int key, int value) {
    ++putCalls;
    putSum += key * value;
}

int twice(int key) { return 2 * key; }

}  // namespace

int main() {
    // ExpectCall(...).Return(...): the mock returns the configured value.
    {
        MockRepository mocks;
        Store *store = mocks.Mock<Store>();
        mocks.ExpectCall(store, Store::get).Return(42);
        CHECK(store->get(7) == 42);
    }

    // OnCall(...).Do(...): every call runs the function with the call's arguments, and its result
    // is the call's result.
    {
        MockRepository mocks;
        Store *store = mocks.Mock<Store>();
        mocks.OnCall(store, Store::put).Do(recordPut);
        mocks.OnCall(store, Store::get).Do(twice);
        store->put(2, 3);
        store->put(4, 5);
        CHECK(putCalls == 2);
        CHECK(putSum == 26);
        CHECK(store->get(21) == 42);
        CHECK(store->get(-4) == -8);
    }

    // With(...): a call with other arguments throws ExpectationException and leaves the
    // expectation for the matching call.
    {
        MockRepository mocks;
        Store *store = mocks.Mock<Store>();
        mocks.ExpectCall(store, Store::get).With(1).Return(10);
        bool mismatch = false;
        try {
            store->get(2);
        } catch (HippoMocks::ExpectationException &e) {
            mismatch = std::strstr(e.what(), "called with mismatching expectation") != 0;
        }
        CHECK(mismatch);
        CHECK(store->get(1) == 10);
    }

    // An expected call that never happens: the repository's destructor throws
    // CallMissingException.
    {
        bool missing = false;
        try {
            MockRepository mocks;
            Store *store = mocks.Mock<Store>();
            mocks.ExpectCall(store, Store::open);
            mocks.ExpectCall(store, Store::close);
            store->open();
        } catch (HippoMocks::CallMissingException &e) {
            missing = std::strstr(e.what(), "Function with expectation not called!") != 0;
        }
        CHECK(missing);
    }

    // ExpectCalls are ordered by default (DEFAULT_AUTOEXPECT): close() before open() throws.
    {
        MockRepository mocks;
        Store *store = mocks.Mock<Store>();
        mocks.ExpectCall(store, Store::open);
        mocks.ExpectCall(store, Store::close);
        bool outOfOrder = false;
        try {
            store->close();
        } catch (HippoMocks::ExpectationException &) {
            outOfOrder = true;
        }
        CHECK(outOfOrder);
        store->open();
        store->close();
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("hippomocks: all checks passed");
    return 0;
}
