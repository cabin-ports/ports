// The X fixture adapts upstream src/test/operators/test_operators.cpp
// (taocpp/operators 1.2.2), under its license:
//
// Copyright (c) 2013-2019 Daniel Frey
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <tao/operators.hpp>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

// Mirrors upstream's src/test/operators/test_operators.cpp:10-114: the
// compound operators and ==/< are written by hand, the rest is generated.
struct X : tao::operators::ordered_field<X>,
           tao::operators::modable<X>,
           tao::operators::ordered_field<X, int>,
           tao::operators::modable<X, int> {
  explicit X(int v) : v(v) {}
  X &operator+=(const X &x) { v += x.v; return *this; }
  X &operator-=(const X &x) { v -= x.v; return *this; }
  X &operator*=(const X &x) { v *= x.v; return *this; }
  X &operator/=(const X &x) { v /= x.v; return *this; }
  X &operator%=(const X &x) { v %= x.v; return *this; }
  X &operator+=(int i) { v += i; return *this; }
  X &operator-=(int i) { v -= i; return *this; }
  X &operator*=(int i) { v *= i; return *this; }
  X &operator/=(int i) { v /= i; return *this; }
  X &operator%=(int i) { v %= i; return *this; }
  int v;
};

bool operator==(const X &a, const X &b) { return a.v == b.v; }
bool operator<(const X &a, const X &b) { return a.v < b.v; }
bool operator==(const X &a, int b) { return a.v == b; }
bool operator<(const X &a, int b) { return a.v < b; }
bool operator>(const X &a, int b) { return a.v > b; }

} // namespace

int main() {
  // Arithmetic generated from the compound operators.
  check((X(7) + X(2)).v == 9, "X + X");
  check((X(7) - X(2)).v == 5, "X - X");
  check((X(7) * X(2)).v == 14, "X * X");
  check((X(7) / X(2)).v == 3, "X / X");
  check((X(7) % X(2)).v == 1, "X % X");

  // Comparisons generated from == and <.
  check(X(2) < X(3), "X < X");
  check(X(3) > X(2), "X > X");
  check(X(3) >= X(3), "X >= X");
  check(X(3) <= X(3), "X <= X");
  check(X(2) != X(3), "X != X");
  check(!(X(3) < X(3)), "!(X < X)");

  // Rvalue chain (the move-aware overloads).
  check((X(1) + X(2) + X(3)).v == 6, "rvalue chain");
  check((X(20) - X(5) * X(2) / X(3)).v == 17, "mixed chain");

  // Mixed X/int operators; int - X is generated separately from X - int.
  check((X(7) + 3).v == 10, "X + int");
  check((3 + X(7)).v == 10, "int + X");
  check((3 * X(7)).v == 21, "int * X");
  check((X(7) - 3).v == 4, "X - int");
  check((X(7) % 4).v == 3, "X % int");
  check((X(7) / 2).v == 3, "X / int");
  check((10 - X(3)).v == 7, "int - X");
  check(X(7) > 6 && X(7) == 7 && X(7) != 8, "X vs int comparisons");
  check(6 < X(7) && 7 >= X(7), "int vs X comparisons");

  return failures == 0 ? 0 : 1;
}
