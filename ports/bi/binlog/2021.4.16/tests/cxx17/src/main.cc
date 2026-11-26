#include <binlog/binlog.hpp>
#include <binlog/TextOutputStream.hpp>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

struct Getter
{
  int a() const noexcept { return 33; }
  std::string b() const noexcept { return "foo"; }
};

BINLOG_ADAPT_STRUCT(Getter, a, b)

int main()
{
  BINLOG_INFO("{}", Getter{});

  std::ostringstream os;
  binlog::TextOutputStream out(os, "%S %m\n");
  binlog::consume(out);

  if (os.str() != "INFO Getter{ a: 33, b: foo }\n")
  {
    std::cerr << "unexpected output:\n" << os.str();
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
