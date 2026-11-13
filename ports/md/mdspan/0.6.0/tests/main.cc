// Views known buffers through mdspan and mdarray and checks the extents, the
// index mappings of the three standard layouts, and element access.  Some
// cases are adapted from upstream's tests (tests/test_layout_stride.cpp,
// test_mdspan_size.cpp, test_element_access.cpp, test_mdarray_to_mdspan.cpp).

// First, for MSVC: the layout_stride != below exercises the `not` that
// cabin.toml documents.
#include <iso646.h>

#include <experimental/mdarray>
#include <experimental/mdspan>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace stdex = std::experimental;

namespace {

constexpr std::size_t dyn = stdex::dynamic_extent;

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void test_extents()
{
    using ext = stdex::extents<std::size_t, 3, dyn, 5>;
    static_assert(ext::rank() == 3 && ext::rank_dynamic() == 1, "extents: rank");
    static_assert(ext::static_extent(0) == 3 && ext::static_extent(1) == dyn, "extents: static_extent");
    ext e(4);
    check(e.extent(0) == 3 && e.extent(1) == 4 && e.extent(2) == 5, "extents: static and dynamic values");
    check(e == stdex::dextents<std::size_t, 3>(3, 4, 5), "extents: equal to dextents with the same values");
    check(e != stdex::dextents<std::size_t, 3>(3, 5, 5), "extents: unequal values");

    stdex::dextents<std::size_t, 3> d = e;
    stdex::extents<std::size_t, 3, 4, 5> fixed(d);
    check(d.extent(1) == 4 && fixed == e, "extents: conversions between static and dynamic");
}

void test_element_access()
{
    std::array<double, 6> a{};
    stdex::mdspan<double, stdex::extents<std::size_t, 2, 3>> s(a.data());
    check(s(std::array<int, 2>{1, 2}) == 0, "element access: zero-initialized");
    s(std::array<int, 2>{0, 1}) = 3.14;
    check(s(std::array<int, 2>{0, 1}) == 3.14 && s(0, 1) == 3.14 && a[1] == 3.14,
          "element access: array and variadic indices agree");

    stdex::mdspan<double, stdex::dextents<std::size_t, 2>> d = s;
    check(d.extent(0) == 2 && d.extent(1) == 3 && d(0, 1) == 3.14, "element access: through dynamic extents");

    int x = 42;
    stdex::mdspan<int, stdex::extents<std::size_t>> scalar(&x);
    check(scalar.rank() == 0 && scalar.size() == 1 && scalar() == 42, "element access: rank 0");
}

void test_right_and_left()
{
    std::vector<int> buf(12);
    for (int i = 0; i < 12; ++i) {
        buf[i] = i;
    }
    stdex::mdspan<int, stdex::dextents<std::size_t, 2>> right(buf.data(), 3, 4);
    stdex::mdspan<int, stdex::dextents<std::size_t, 2>, stdex::layout_left> left(buf.data(), 3, 4);
    check(right.extent(0) == 3 && right.extent(1) == 4 && right.size() == 12 && !right.empty(),
          "layouts: extents and size");
    stdex::mdspan<int, stdex::dextents<std::size_t, 2>> none(buf.data(), 3, 0);
    check(none.empty() && none.size() == 0, "layouts: a zero extent is empty");
    check(right(1, 2) == 6 && left(1, 2) == 7, "layouts: row-major and column-major offsets");
    check(right.stride(0) == 4 && right.stride(1) == 1 && left.stride(0) == 1 && left.stride(1) == 3,
          "layouts: strides");
    check(right.is_exhaustive() && left.is_exhaustive() && right.mapping().required_span_size() == 12
              && left.mapping().required_span_size() == 12,
          "layouts: exhaustive over the buffer");

    // Each layout visits every element once.
    int right_sum = 0;
    int left_sum = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            right_sum += right(i, j);
            left_sum += left(i, j);
        }
    }
    check(right_sum == 66 && left_sum == 66, "layouts: full traversal");

    left(2, 3) = 100;
    check(buf[11] == 100 && right(2, 3) == 100, "layouts: writes reach the buffer");

    stdex::mdspan<const int, stdex::dextents<std::size_t, 2>> view = right;
    check(view.data_handle() == buf.data() && view(1, 2) == 6, "layouts: const view of the same data");
}

void test_layout_stride()
{
    stdex::layout_stride::mapping<stdex::extents<std::size_t, dyn, dyn>> m{stdex::dextents<std::size_t, 2>{16, 32},
                                                                            std::array<int, 2>{1, 128}};
    check(m.extents().extent(0) == 16 && m.extents().extent(1) == 32, "layout_stride: extents");
    check(m.stride(0) == 1 && m.stride(1) == 128 && m.strides()[0] == 1 && m.strides()[1] == 128,
          "layout_stride: strides");
    check(m.required_span_size() == 1 + 15 * 1 + 31 * 128, "layout_stride: required span size");
    check(!m.is_exhaustive(), "layout_stride: not exhaustive");

    std::vector<int> big(m.required_span_size());
    stdex::mdspan<int, stdex::dextents<std::size_t, 2>, stdex::layout_stride> s(big.data(), m);
    s(15, 31) = 7;
    s(1, 1) = 5;
    check(big.back() == 7 && big[1 + 128] == 5, "layout_stride: element offsets");

    using ext345 = stdex::extents<std::size_t, 3, 4, 5>;
    stdex::layout_right::mapping<ext345> rm;
    stdex::layout_stride::mapping<ext345> from_right(rm);
    check(from_right.stride(0) == 20 && from_right.stride(1) == 5 && from_right.stride(2) == 1
              && from_right.required_span_size() == 60 && from_right.is_exhaustive(),
          "layout_stride: from layout_right");
    stdex::layout_stride::mapping<ext345> as_left{ext345{}, std::array<std::size_t, 3>{1, 3, 12}};
    check(from_right == rm && !(from_right != rm) && as_left != rm && as_left.is_exhaustive(),
          "layout_stride: compared with layout_right");

    using ext16x32 = stdex::extents<std::size_t, 16, 32>;
    using ext16x64 = stdex::extents<std::size_t, 16, 64>;
    stdex::layout_stride::mapping<ext16x32> a{ext16x32{}, std::array<std::size_t, 2>{1, 16}};
    stdex::layout_stride::mapping<ext16x32> b{ext16x32{}, std::array<std::size_t, 2>{1, 16}};
    stdex::layout_stride::mapping<ext16x32> c{ext16x32{}, std::array<std::size_t, 2>{1, 17}};
    stdex::layout_stride::mapping<ext16x64> d{ext16x64{}, std::array<std::size_t, 2>{1, 16}};
    check(a == b && !(a == c) && !(a == d), "layout_stride: equality");
}

void test_size()
{
    // Upstream's narrow index types, with sizes they can represent.
    std::vector<char> storage(240);
    stdex::mdspan<char, stdex::extents<std::int8_t, 12, 10>> s8(storage.data());
    stdex::mdspan<char, stdex::extents<std::uint8_t, 16, 15>> u8(storage.data());
    check(s8.size() == 120 && u8.size() == 240 && s8.mapping().required_span_size() == 120,
          "size: narrow index types");
    s8(11, 9) = 'a';
    u8(15, 14) = 'b';
    check(storage[119] == 'a' && storage[239] == 'b', "size: last elements of narrow index types");
}

void test_mdarray()
{
    stdex::mdarray<int, stdex::dextents<std::size_t, 2>> a(3, 4);
    check(a.extent(0) == 3 && a.extent(1) == 4 && a.container().size() == 12, "mdarray: owns its elements");
    check(a(2, 3) == 0, "mdarray: value-initialized");
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            a(i, j) = static_cast<int>(10 * i + j);
        }
    }
    check(a.data()[1 * 4 + 2] == 12, "mdarray: layout_right storage");

    const auto &ca = a;
    auto v = ca.to_mdspan();
    check(v.data_handle() == a.data() && v.extents() == a.extents() && v.mapping() == a.mapping() && v(2, 3) == 23,
          "mdarray: to_mdspan views the same elements");

    auto copy = a;
    copy(0, 0) = -1;
    check(a(0, 0) == 0 && copy(0, 0) == -1, "mdarray: copies own their storage");

    stdex::mdarray<int, stdex::extents<int, dyn>> one(100);
    stdex::mdspan<int, stdex::extents<int, dyn>> s = one;
    check(s.data_handle() == one.data() && s.extent(0) == 100, "mdarray: converts to mdspan");
}

} // namespace

int main()
{
    test_extents();
    test_element_access();
    test_right_and_left();
    test_layout_stride();
    test_size();
    test_mdarray();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("mdspan 0.6.0 ok");
    return 0;
}
