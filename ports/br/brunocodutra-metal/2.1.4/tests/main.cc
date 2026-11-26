#include <metal.hpp>

#include <type_traits>

union x { char payload[10]; };
class y { public: char c; };
struct z { char c; int i; };

using l0 = metal::list<>;
using l1 = metal::prepend<l0, x>;
using l2 = metal::append<l1, z>;
using l3 = metal::insert<l2, metal::number<1>, y>;

template <class a, class b>
using smaller = metal::number<(sizeof(a) < sizeof(b))>;

using n3 = metal::list<metal::number<10>, metal::number<20>, metal::number<30>>;

static_assert(metal::same<l3, metal::list<x, y, z>>::value, "");
static_assert(metal::size<l3>::value == 3, "");
static_assert(metal::same<metal::at<l3, metal::number<1>>, y>::value, "");
static_assert(metal::same<metal::reverse<l3>, metal::list<z, y, x>>::value, "");
static_assert(metal::count_if<l3, metal::trait<std::is_class>>::value == 2, "");
static_assert(metal::same<metal::sort<l3, metal::lambda<smaller>>, metal::list<y, z, x>>::value, "");

int main() {
  // Runtime reads of compile-time results, so a wrong value is a nonzero exit.
  int rc = 0;
  if (metal::at<n3, metal::number<2>>::value != 30) rc |= 1;
  if (metal::size<metal::range<l3, metal::number<1>, metal::number<3>>>::value != 2) rc |= 2;
  if (metal::count_if<l3, metal::trait<std::is_class>>::value != 2) rc |= 4;
  if (metal::add<metal::number<1>, metal::number<2>, metal::number<3>>::value != 6) rc |= 8;
  return rc;
}
