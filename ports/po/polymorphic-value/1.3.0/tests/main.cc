#include <polymorphic_value.h>

#include <cstdio>
#include <cstring>
#include <utility>

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,    \
                   #cond);                                                     \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int live = 0;   // Shape objects alive
int copies = 0; // Shape copy constructions

struct Shape {
  Shape() { ++live; }
  Shape(const Shape &) {
    ++live;
    ++copies;
  }
  virtual ~Shape() { --live; }
  virtual int area() const = 0;
  virtual const char *name() const = 0;
};

struct Square : Shape {
  int side;
  explicit Square(int s) : side(s) {}
  int area() const override { return side * side; }
  const char *name() const override { return "square"; }
};

struct Rect : Shape {
  int w, h;
  Rect(int w_, int h_) : w(w_), h(h_) {}
  int area() const override { return w * h; }
  const char *name() const override { return "rect"; }
};

// A Square whose dynamic type differs from the static type it is passed as.
struct Tile : Square {
  using Square::Square;
  const char *name() const override { return "tile"; }
};

int copier_calls = 0, deleter_calls = 0;

// The copier and deleter count through state they carry, so a value must
// keep and copy them rather than default-construct new ones.
struct CountingCopier {
  int *calls;
  Square *operator()(const Square &s) const {
    ++*calls;
    return new Square(s);
  }
};

struct CountingDeleter {
  int *calls;
  void operator()(const Square *s) const {
    ++*calls;
    delete s;
  }
};

struct ThrowingCopier {
  Square *operator()(const Square &) const { throw 42; }
};

using isocpp_p0201::make_polymorphic_value;
using isocpp_p0201::polymorphic_value;

} // namespace

int main() {
  {
    polymorphic_value<Shape> empty;
    CHECK(!empty);
    polymorphic_value<Shape> empty_copy = empty;
    CHECK(!empty_copy);

    auto sq = make_polymorphic_value<Shape, Square>(3);
    CHECK(sq && sq->area() == 9 && std::strcmp(sq->name(), "square") == 0);
    CHECK(live == 1 && copies == 0);

    // Copying clones the Square through the Shape handle.
    polymorphic_value<Shape> copy = sq;
    CHECK(copy && &*copy != &*sq && copy->area() == 9);
    CHECK(std::strcmp(copy->name(), "square") == 0);
    CHECK(live == 2 && copies == 1);
    static_cast<Square &>(*copy).side = 5;
    CHECK(copy->area() == 25 && sq->area() == 9);

    // Moving transfers the object without copying it.
    const Shape *before = &*copy;
    polymorphic_value<Shape> moved(std::move(copy));
    CHECK(!copy && moved && &*moved == before && copies == 1 && live == 2);

    // Assignment copies, self-assignment keeps the value, and assigning an
    // empty value releases the old one.
    polymorphic_value<Shape> assigned;
    assigned = moved;
    CHECK(assigned->area() == 25 && copies == 2 && live == 3);
    polymorphic_value<Shape> &alias = assigned;
    assigned = alias;
    CHECK(assigned->area() == 25 && copies == 2 && live == 3);
    assigned = empty;
    CHECK(!assigned && live == 2);
    assigned = std::move(moved);
    CHECK(assigned->area() == 25 && !moved && live == 2);

    // In-place construction and swap.
    polymorphic_value<Shape> rect(std::in_place_type<Rect>, 4, 6);
    CHECK(rect->area() == 24 && live == 3);
    swap(rect, sq);
    CHECK(rect->area() == 9 && sq->area() == 24);
    rect.swap(sq);
    CHECK(rect->area() == 24 && sq->area() == 9);
    polymorphic_value<Shape> hole;
    swap(hole, rect);
    CHECK(!rect && hole && hole->area() == 24);

    // Converting from polymorphic_value<Square> keeps the dynamic type, and
    // copies of the converted value still clone a Square.
    auto square = make_polymorphic_value<Square>(2);
    polymorphic_value<Shape> widened(square);
    CHECK(widened->area() == 4 && &*widened != &*square && copies == 3);
    polymorphic_value<Shape> widened_copy = widened;
    CHECK(widened_copy->area() == 4 && copies == 4);
    polymorphic_value<Shape> stolen(std::move(square));
    CHECK(!square && stolen->area() == 4 && copies == 4);

    const polymorphic_value<Shape> &cref = stolen;
    CHECK(cref->area() == 4 && (*cref).area() == 4);
  }
  CHECK(live == 0);

  {
    // Taking ownership of a pointer; a null pointer gives an empty value.
    polymorphic_value<Shape> owned(new Square(7));
    CHECK(owned->area() == 49 && live == 1);
    polymorphic_value<Shape> owned_copy = owned;
    CHECK(owned_copy->area() == 49 && &*owned_copy != &*owned && live == 2);
    polymorphic_value<Shape> none(static_cast<Square *>(nullptr));
    CHECK(!none);

    // With the default copier and deleter, a pointer whose dynamic type
    // differs from its static type would be sliced on copy, so it is
    // rejected and stays owned by the caller.
    Tile *tile = new Tile(1);
    bool threw = false;
    try {
      polymorphic_value<Shape> sliced(static_cast<Square *>(tile));
    } catch (const isocpp_p0201::bad_polymorphic_value_construction &e) {
      threw = std::strcmp(e.what(), "Dynamic and static type mismatch in "
                                    "polymorphic_value construction") == 0;
    }
    CHECK(threw);
    if (threw)
      delete tile;
    CHECK(live == 2);

    // A custom copier and deleter are used for every copy and destruction.
    {
      polymorphic_value<Shape> custom(new Square(6),
                                      CountingCopier{&copier_calls},
                                      CountingDeleter{&deleter_calls});
      polymorphic_value<Shape> custom_copy = custom;
      polymorphic_value<Shape> copy_of_copy = custom_copy;
      CHECK(custom_copy->area() == 36 && copy_of_copy->area() == 36);
      CHECK(copier_calls == 2 && live == 5 && deleter_calls == 0);
    }
    CHECK(deleter_calls == 3 && live == 2);

    // A custom copier takes over copying, so a dynamic type that differs from
    // the static type is accepted.
    {
      polymorphic_value<Shape> custom_tile(static_cast<Square *>(new Tile(2)),
                                           CountingCopier{&copier_calls});
      CHECK(std::strcmp(custom_tile->name(), "tile") == 0 && live == 3);
    }
    CHECK(live == 2);
  }
  CHECK(live == 0);

  {
    // Assigning to a non-empty value destroys its old object, moving leaves
    // the source empty, and a value moved into itself is kept.
    auto a = make_polymorphic_value<Shape, Square>(2);
    auto b = make_polymorphic_value<Shape, Rect>(3, 4);
    copies = 0;
    a = b;
    CHECK(a->area() == 12 && &*a != &*b && copies == 1 && live == 2);
    auto c = make_polymorphic_value<Shape, Square>(5);
    a = std::move(c);
    CHECK(a->area() == 25 && !c && live == 2);
    polymorphic_value<Shape> &self = a;
    a = std::move(self);
    CHECK(a && a->area() == 25 && live == 2);

    // A copy that throws leaves the assigned-to value unchanged.
    polymorphic_value<Shape> thrower(new Square(8), ThrowingCopier{});
    bool caught = false;
    try {
      a = thrower;
    } catch (int) {
      caught = true;
    }
    CHECK(caught && a && a->area() == 25 && live == 3);
  }
  CHECK(live == 0);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("polymorphic-value ok");
  return 0;
}
