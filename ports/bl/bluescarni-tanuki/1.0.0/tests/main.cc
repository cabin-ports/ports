#include <cstdio>
#include <typeinfo>
#include <utility>

#include <tanuki/tanuki.hpp>

template <typename Base, typename Holder, typename T>
struct shape_impl : Base {
    int area() const override { return tanuki::getval<Holder>(this).area(); }
    void scale(int k) override { tanuki::getval<Holder>(this).scale(k); }
};

struct shape_iface {
    virtual ~shape_iface() = default;
    virtual int area() const = 0;
    virtual void scale(int k) = 0;

    template <typename Base, typename Holder, typename T>
    using impl = shape_impl<Base, Holder, T>;
};

struct square {
    int side;
    int area() const { return side * side; }
    void scale(int k) { side *= k; }
};

// Larger than the 48-byte default static storage.
struct big {
    int cells[32] = {7};
    int area() const { return cells[0]; }
    void scale(int k) { cells[0] *= k; }
};

using shape = tanuki::wrap<shape_iface>;
using shape_ref = tanuki::wrap<shape_iface, tanuki::config<>{.semantics = tanuki::wrap_semantics::reference}>;

static int failures = 0;

#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #c);   \
            ++failures;                                           \
        }                                                         \
    } while (0)

int main()
{
    // Dispatch through the erased interface, small (static) and large (dynamic) values.
    shape s(square{3});
    shape b(big{});
    CHECK(s->area() == 9);
    CHECK(b->area() == 7);
    CHECK(has_static_storage(s));
    CHECK(has_dynamic_storage(b));
    s->scale(2);
    CHECK(s->area() == 36);

    // Typed access.
    CHECK(tanuki::value_isa<square>(s));
    CHECK(!tanuki::value_isa<big>(s));
    CHECK(tanuki::value_ptr<big>(s) == nullptr);
    CHECK(tanuki::value_ref<square>(s).side == 6);
    CHECK(value_type_index(s) == typeid(square));
    bool threw = false;
    try {
        (void)tanuki::value_ref<big>(s);
    } catch (const std::bad_cast &) {
        threw = true;
    }
    CHECK(threw);

    // Value semantics: a copy is independent, a move leaves the source invalid.
    shape c(s);
    c->scale(2);
    CHECK(c->area() == 144);
    CHECK(s->area() == 36);
    shape m(std::move(b));
    CHECK(is_invalid(b));
    CHECK(is_valid(m));
    CHECK(m->area() == 7);

    // Assignment replaces the stored type.
    m = shape(square{5});
    CHECK(m->area() == 25);
    swap(m, c);
    CHECK(m->area() == 144 && c->area() == 25);

    // Reference semantics: copies share one object.
    shape_ref r1(square{2});
    shape_ref r2(r1);
    r1->scale(5);
    CHECK(r2->area() == 100);
    CHECK(tanuki::value_ref<square>(r2).side == 10);

    CHECK(TANUKI_VERSION_MAJOR == 1 && TANUKI_VERSION_MINOR == 0);

    return failures == 0 ? 0 : 1;
}
