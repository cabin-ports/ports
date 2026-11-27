#include <cx_array.h>
#include <cx_math.h>
#include <cx_strenc.h>
#include <cx_utils.h>

#include <cstdio>
#include <string>

constexpr char upper(char c) { return c < 'a' || c > 'z' ? c : c - ('a' - 'A'); }
constexpr int sum(int a, int b) { return a + b; }
constexpr bool even(int i) { return i % 2 == 0; }
constexpr bool cless(char a, char b) { return a != 0 && a < b; }

constexpr auto nums = cx::make_array(1, 2, 3, 4, 5);
constexpr auto doubled = cx::transform(nums, nums, sum);
constexpr auto shouted = cx::transform(cx::make_array("abcde"), upper);
constexpr auto sorted = cx::sort(cx::make_array("jackdwslovemybigphnxfqurtz"), cless);
constexpr auto reversed = cx::reverse(nums);
constexpr auto parted = cx::partition(nums, even);
constexpr auto pushed = nums.push_back(6);
static_assert(doubled[0] == 2 && doubled[4] == 10, "transform");
static_assert(shouted[0] == 'A' && shouted[4] == 'E', "transform string");
static_assert(sorted[0] == 'a' && sorted[1] == 'b' && sorted[2] == 'c' && sorted[3] == 'd', "sort");
static_assert(reversed == cx::make_array(5, 4, 3, 2, 1), "reverse");
static_assert(parted == cx::make_array(2, 4, 1, 3, 5), "partition");
static_assert(pushed.size() == 6 && pushed[5] == 6, "push_back");

// The long double overloads exist only at exactly -std=c++14.
#if __cplusplus == 201402L
constexpr long double floor_ld = cx::floor(-2.5l);
constexpr long double ceil_ld = cx::ceil(2.5l);
static_assert(floor_ld == -3.0l && ceil_ld == 3.0l, "long double floor/ceil");
#endif

// A runtime call would not link, so the encryption happens in a constant expression.
constexpr auto encrypted = cx_make_encrypted_string("Hello, world!");
static_assert(encrypted.size() == 13, "encrypted size");

int main() {
  const std::string plain = encrypted;
  const bool ok = plain == "Hello, world!" && doubled[2] == 6 && sorted[25] == 'z' &&
                  parted[0] == 2 && parted[4] == 5;
  std::puts(ok ? "ok" : "FAIL");
  return ok ? 0 : 1;
}
