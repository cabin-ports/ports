#include "clockUtils/compression/algorithm/HuffmanFixed.h"
#include "clockUtils/compression/Compression.h"
#include "clockUtils/errors.h"

#include <cstdio>
#include <string>

int main() {
  clockUtils::compression::Compression<clockUtils::compression::algorithm::HuffmanFixed> codec;
  const std::string text = "mississippi mississippi";
  std::string packed, unpacked;
  if (codec.compress(text, packed) != clockUtils::ClockError::SUCCESS || packed == text ||
      codec.decompress(packed, unpacked) != clockUtils::ClockError::SUCCESS || unpacked != text) {
    std::puts("FAIL compression");
    return 1;
  }
  return 0;
}
