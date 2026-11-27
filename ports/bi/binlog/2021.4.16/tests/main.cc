#include <binlog/binlog.hpp>
#include <binlog/TextOutputStream.hpp>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct Pair
{
  int a = 0;
  std::string b;
};

BINLOG_ADAPT_STRUCT(Pair, a, b)

int main()
{
  BINLOG_INFO("x={} p={}", 42, Pair{2, "three"});
  BINLOG_WARN_C(net, "v={} ok={}", (std::vector<int>{1, 2, 3}), true);

  std::ostringstream os;
  binlog::TextOutputStream out(os, "%S %C %m\n");
  const binlog::Session::ConsumeResult r = binlog::consume(out);

  const std::string expected =
    "INFO main x=42 p=Pair{ a: 2, b: three }\n"
    "WARN net v=[1, 2, 3] ok=true\n";
  if (os.str() != expected || r.bytesConsumed == 0)
  {
    std::cerr << "unexpected output:\n" << os.str();
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
