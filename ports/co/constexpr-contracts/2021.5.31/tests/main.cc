#include <cjdb/contracts.hpp>

#include <cstdio>
#include <type_traits>

constexpr int pre(int v)
{
	CJDB_EXPECTS(v > 0);
	return v;
}

constexpr int post(int v)
{
	int r = v - 1;
	CJDB_ENSURES(r >= 0);
	return r;
}

constexpr int mid(int v)
{
	CJDB_ASSERT(v != 0 and v < 100);
	return v * 2;
}

static_assert(pre(3) == 3);
static_assert(post(3) == 2);
static_assert(mid(21) == 42);

// A failing contract is undefined behaviour in optimised and MSVC builds, so
// only constant evaluation tells a failure from a pass.  GCC and Clang reject
// the failing call there; MSVC may accept __assume(false), so it skips these.
#ifndef _MSC_VER
template<auto F, int V>
concept holds = requires { typename std::integral_constant<int, F(V)>; };

static_assert(holds<pre, 1> and not holds<pre, 0> and not holds<pre, -5>);
static_assert(holds<post, 1> and not holds<post, 0>);
static_assert(holds<mid, 99> and not holds<mid, 0> and not holds<mid, 100>);
#endif

int main()
{
	volatile int v = 7;
	if (pre(v) != 7 or post(v) != 6 or mid(v) != 14) {
		std::fputs("contract wrappers returned wrong values\n", stderr);
		return 1;
	}
	return 0;
}
