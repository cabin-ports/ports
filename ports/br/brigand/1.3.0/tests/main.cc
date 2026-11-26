#include <brigand/brigand.hpp>
#include <brigand/version.hpp>

#include <boost/fusion/include/at_c.hpp>
#include <boost/fusion/include/vector.hpp>
#include <boost/variant.hpp>

#include <cstddef>
#include <cstdio>
#include <string>
#include <type_traits>

namespace {

template <class T> const char* name();
template <> const char* name<char>() { return "char"; }
template <> const char* name<short>() { return "short"; }
template <> const char* name<int>() { return "int"; }
template <> const char* name<double>() { return "double"; }

struct collector {
  std::string names;
  std::size_t bytes = 0;
  template <class T> void operator()(brigand::type_<T>) {
    names += name<T>();
    names += ' ';
    bytes += sizeof(T);
  }
};

using L = brigand::list<double, char, int, short>;
using brigand::_1;
using brigand::_2;

static_assert(brigand::size<L>::value == 4, "size");
static_assert(std::is_same<brigand::at_c<L, 2>, int>::value, "at_c");
static_assert(std::is_same<brigand::push_back<L, long>,
                           brigand::list<double, char, int, short, long>>::value,
              "push_back");
static_assert(std::is_same<brigand::transform<L, std::add_pointer<_1>>,
                           brigand::list<double*, char*, int*, short*>>::value,
              "transform");
static_assert(std::is_same<brigand::filter<L, std::is_integral<_1>>,
                           brigand::list<char, int, short>>::value,
              "filter");
static_assert(std::is_same<
                  brigand::sort<L, brigand::less<brigand::sizeof_<_1>,
                                                 brigand::sizeof_<_2>>>,
                  brigand::list<char, short, int, double>>::value,
              "sort");
static_assert(std::is_same<brigand::find<L, std::is_same<_1, int>>,
                           brigand::list<int, short>>::value,
              "find");
static_assert(brigand::index_of<L, int>::value == 2, "index_of");
static_assert(brigand::fold<brigand::list<brigand::int32_t<1>,
                                          brigand::int32_t<2>,
                                          brigand::int32_t<3>>,
                            brigand::int32_t<10>,
                            brigand::plus<brigand::_state, brigand::_element>>::value == 16,
              "fold");
static_assert(std::is_same<brigand::as_variant<L>,
                           boost::variant<double, char, int, short>>::value,
              "as_variant");
static_assert(std::is_same<brigand::as_fusion_vector<L>,
                           boost::fusion::vector<double, char, int, short>>::value,
              "as_fusion_vector");

int fail(const char* what) {
  std::fprintf(stderr, "brigand: %s\n", what);
  return 1;
}

}  // namespace

int main() {
  collector c = brigand::for_each<L>(collector{});
  if (c.names != "double char int short ") return fail("for_each order");
  if (c.bytes != sizeof(double) + sizeof(char) + sizeof(int) + sizeof(short))
    return fail("for_each sizes");

  using Sorted = brigand::sort<L, brigand::less<brigand::sizeof_<_1>,
                                                brigand::sizeof_<_2>>>;
  if (brigand::for_each<Sorted>(collector{}).names != "char short int double ")
    return fail("sorted order");

  using Filtered = brigand::filter<L, std::is_integral<_1>>;
  if (brigand::for_each<Filtered>(collector{}).names != "char int short ")
    return fail("filtered");

  if (std::string(BRIGAND_VERSION) != "1.3.0") return fail("version");

  brigand::as_fusion_vector<L> fv(1.5, 'x', 7, short(9));
  if (boost::fusion::at_c<0>(fv) != 1.5 || boost::fusion::at_c<1>(fv) != 'x' ||
      boost::fusion::at_c<2>(fv) != 7 || boost::fusion::at_c<3>(fv) != 9)
    return fail("fusion vector values");

  brigand::as_variant<L> v = 42;
  if (v.which() != 2 || boost::get<int>(v) != 42) return fail("variant int");
  v = 2.5;
  if (v.which() != 0 || boost::get<double>(v) != 2.5) return fail("variant double");
  return 0;
}
