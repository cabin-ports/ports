// Copyright (c) 2011 Google, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// setup() and the expected values below are copied from upstream's src/city-test.cc.
// Expected values are rows of upstream's src/city-test.cc testdata table
// (indexes 0, 1, 2, 10, 20, 40, 100, 298, 299), run on the same generated data.
#include <city.h>

#include <cstdio>
#include <cstring>

namespace {

const uint64 k0 = 0xc3a5c85c97cb3127ULL;
const uint64 kSeed0 = 1234567;
const uint64 kSeed1 = k0;
const int kDataSize = 1 << 20;

char data[kDataSize];

// city-test.cc's setup().
void setup() {
  uint64 a = 9;
  uint64 b = 777;
  for (int i = 0; i < kDataSize; i++) {
    a += b;
    b += a;
    a = (a ^ (a >> 41)) * k0;
    b = (b ^ (b >> 41)) * k0 + i;
    uint8 u = static_cast<uint8>(b >> 37);
    std::memcpy(data + i, &u, 1);
  }
}

struct Row {
  int offset;
  int len;
  // CityHash64, CityHash64WithSeed, CityHash64WithSeeds, CityHash128 low and
  // high, CityHash128WithSeed low and high, CityHash32.
  uint64 e[7];
  uint32 e32;
};

const Row rows[] = {
  {0, 0, {0x9ae16a3b2f90404fULL, 0x75106db890237a4aULL, 0x3feac5f636039766ULL, 0x3df09dfc64c09a2bULL, 0x3cb540c392e51e29ULL, 0x6b56343feac0663ULL, 0x5b7bc50fd8e8ad92ULL}, 0xdc56d17a},
  {1, 1, {0x541150e87f415e96ULL, 0x1aef0d24b3148a1aULL, 0xbacc300e1e82345aULL, 0xc3cdc41e1df33513ULL, 0x2c138ff2596d42f6ULL, 0xf58e9082aed3055fULL, 0x162e192b2957163dULL}, 0x99929334},
  {4, 2, {0xf3786a4b25827c1ULL, 0x34ee1a2bf767bd1cULL, 0x2f15ca2ebfb631f2ULL, 0x3149ba1dac77270dULL, 0x70e2e076e30703cULL, 0x59bcc9659bc5296ULL, 0x9ecbc8132ae2f1d7ULL}, 0x4252edb7},
  {100, 10, {0x236827beae282a46ULL, 0xe43970221139c946ULL, 0x4f3ac6faa837a3aaULL, 0x71fec0f972248915ULL, 0x2170ec2061f24574ULL, 0x9eb346b6caa36e82ULL, 0x2908f0fdbca48e73ULL}, 0x70eb1a1f},
  {400, 20, {0x4182832b52d63735ULL, 0x337097e123eea414ULL, 0xb5a72ca0456df910ULL, 0x7ebc034235bc122fULL, 0xd9a7783d4edd8049ULL, 0x5f8b04a15ae42361ULL, 0xfc193363336453ddULL}, 0x7a48b105},
  {1600, 40, {0x4ec0b54cf1566affULL, 0x30d2c7269b206bf4ULL, 0x77c22e82295e1061ULL, 0x3df9b04434771542ULL, 0xfeddce785ccb661fULL, 0xa644aff716928297ULL, 0xdd46aee73824b4edULL}, 0x72c39bea},
  {10000, 100, {0x6369163565814de6ULL, 0x8feb86fb38d08c2fULL, 0x4976933485cc9a20ULL, 0x7d3e82d5ba29a90dULL, 0xd5983cc93a9d126aULL, 0x37e9dfd950e7b692ULL, 0x80673be6a7888b87ULL}, 0x9d90eaf5},
  {88804, 298, {0x74c0b8a6821faafeULL, 0xabac39d7491370e7ULL, 0xfaf0b2a48a4e6aedULL, 0x967e970df9673d2aULL, 0xd465247cffa415c0ULL, 0x33a1df0ca1107722ULL, 0x49fc2a10adce4a32ULL}, 0xf6a9fbf8},
  {0, 1048576, {0x5fb5e48ac7b7fa4fULL, 0xa96170f08f5acbc7ULL, 0xbbf5c63d4f52a1e5ULL, 0x6cc09e60700563e9ULL, 0xd18f23221e964791ULL, 0xffc23eeef7af26ebULL, 0x693a954a3622a315ULL}, 0x5398210c},
};

int errors = 0;

void check(const char* what, int len, uint64 expected, uint64 actual) {
  if (expected != actual) {
    std::printf("%s len %d: expected %llx, got %llx\n", what, len,
                static_cast<unsigned long long>(expected),
                static_cast<unsigned long long>(actual));
    ++errors;
  }
}

}  // namespace

int main() {
  setup();
  const uint128 seed128(kSeed0, kSeed1);
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    const Row& r = rows[i];
    const char* p = data + r.offset;
    check("CityHash64", r.len, r.e[0], CityHash64(p, r.len));
    check("CityHash64WithSeed", r.len, r.e[1], CityHash64WithSeed(p, r.len, kSeed0));
    check("CityHash64WithSeeds", r.len, r.e[2], CityHash64WithSeeds(p, r.len, kSeed0, kSeed1));
    const uint128 u = CityHash128(p, r.len);
    check("CityHash128 low", r.len, r.e[3], Uint128Low64(u));
    check("CityHash128 high", r.len, r.e[4], Uint128High64(u));
    const uint128 v = CityHash128WithSeed(p, r.len, seed128);
    check("CityHash128WithSeed low", r.len, r.e[5], Uint128Low64(v));
    check("CityHash128WithSeed high", r.len, r.e[6], Uint128High64(v));
    check("CityHash32", r.len, r.e32, CityHash32(p, r.len));
  }
  return errors == 0 ? 0 : 1;
}
