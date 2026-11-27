#include "clockUtils/iniParser/iniParser.h"
#include "clockUtils/errors.h"

#include <cstdio>
#include <string>
#include <vector>

int main() {
  clockUtils::iniParser::IniParser ini;
  ini.setValue("b", "x", 1);
  ini.setValue("a", "y", 2);
  std::string value;
  const std::vector<std::string> sections = ini.getAllSections();
  if (ini.getValue("a", "y", value) != clockUtils::ClockError::SUCCESS || value != "2" || sections.size() != 2 ||
      sections[0] != "a" || sections[1] != "b") {
    std::puts("FAIL iniparser");
    return 1;
  }
  return 0;
}
