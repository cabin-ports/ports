// The NOMINMAX cabin.toml asks of a Windows consumer that has <windows.h>
// (here through TcpSocket.h) and uses REGISTER_VARIABLE.
#define NOMINMAX
#include "clockUtils/sockets/TcpSocket.h"

#include "clockUtils/argParser/ArgumentParser.h"
#include "clockUtils/compression/Compression.h"
#include "clockUtils/compression/algorithm/HuffmanGeneric.h"
#include "clockUtils/container/DoubleBufferQueue.h"
#include "clockUtils/errors.h"
#include "clockUtils/iniParser/iniParser.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using clockUtils::ClockError;

static int failures;

#define EXPECT(cond)                                                \
  do {                                                              \
    if (!(cond)) {                                                  \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

static void argParser() {
  REGISTER_VARIABLE(int32_t, num, n, 1, "A number");
  REGISTER_VARIABLE(std::string, name, s, "none", "A name");
  REGISTER_VARIABLE(bool, verbose, v, false, "A flag");

  const char * args[] = {"-n", "42", "--name", "abc", "-v"};
  EXPECT(PARSE_ARGUMENTS(args, 5) == ClockError::SUCCESS);
  EXPECT(num == 42);
  EXPECT(name == std::string("abc"));
  EXPECT(verbose == true);

  const char * unknown[] = {"-x"};
  EXPECT(PARSE_ARGUMENTS(unknown, 1) == ClockError::INVALID_USAGE);
  EXPECT(GETLASTPARSERERROR() == "argument -x not registered!");
}

static void compression() {
  clockUtils::compression::Compression<clockUtils::compression::algorithm::HuffmanGeneric> codec;
  const std::string text = "abracadabra abracadabra abracadabra, said the wizard";
  std::string packed, unpacked;
  EXPECT(codec.compress(text, packed) == ClockError::SUCCESS);
  EXPECT(packed != text);
  EXPECT(codec.decompress(packed, unpacked) == ClockError::SUCCESS);
  EXPECT(unpacked == text);

  const std::string runs = std::string(1000, 'a') + "b";
  EXPECT(codec.compress(runs, packed) == ClockError::SUCCESS);
  EXPECT(packed.size() < 1000);
  EXPECT(codec.decompress(packed, unpacked) == ClockError::SUCCESS && unpacked == runs);
}

static void container() {
  clockUtils::container::DoubleBufferQueue<int> queue;
  int value = 0;
  EXPECT(queue.poll(value) == ClockError::NO_ELEMENT);
  queue.push(1);
  queue.push(2);
  EXPECT(queue.size() == 2);
  EXPECT(queue.front(value) == ClockError::SUCCESS && value == 1);
  EXPECT(queue.pop() == ClockError::SUCCESS);
  EXPECT(queue.poll(value) == ClockError::SUCCESS && value == 2);
  EXPECT(queue.empty());
}

static void iniParser() {
  clockUtils::iniParser::IniParser ini;
  EXPECT(ini.load("clockutils-missing.ini") == ClockError::FILENOTFOUND);
  ini.setValue("network", "port", 8080);
  ini.setValue("network", "host", std::string("example.org"));
  ini.setValue("user", "ratio", 0.5);

  int port = 0;
  std::string host;
  double ratio = 0;
  EXPECT(ini.getValue("network", "port", port) == ClockError::SUCCESS && port == 8080);
  EXPECT(ini.getValue("network", "host", host) == ClockError::SUCCESS && host == "example.org");
  EXPECT(ini.getValue("user", "ratio", ratio) == ClockError::SUCCESS && ratio == 0.5);
  EXPECT(ini.getValue("network", "host", port) == ClockError::WRONG_TYPE);
  EXPECT(ini.getValue("network", "missing", host) == ClockError::VALUE_NOTFOUND && host.empty());

  std::vector<std::string> entries = ini.getAllEntries("network");
  EXPECT(entries.size() == 2 && entries[0] == "port" && entries[1] == "host");
  ini.removeEntry("network", "port");
  entries = ini.getAllEntries("network");
  EXPECT(entries.size() == 1 && entries[0] == "host");
  EXPECT(ini.getValue("network", "port", port) == ClockError::VALUE_NOTFOUND);
}

int main() {
  argParser();
  compression();
  container();
  iniParser();
  if (failures == 0) {
    std::puts("ok");
  }
  return failures != 0;
}
