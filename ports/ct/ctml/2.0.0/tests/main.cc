#include <ctml.hpp>

#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

int main() {
  // README.md, "Nodes".
  CTML::Node para("p.text#para", "Hello world!");
  check(para.ToString() == "<p class=\"text\" id=\"para\">Hello world!</p>",
        "README node");

  CTML::Node node("div");
  node.AppendText("Hello ")
      .AppendChild(CTML::Node("span", "Maxwell"))
      .AppendText(", welcome back!");
  check(node.ToString() ==
            "<div>Hello <span>Maxwell</span>, welcome back!</div>",
        "AppendText and AppendChild chain");

  check(CTML::Node("p.class.names#identify").GetSelector() ==
            "p.class.names#identify",
        "GetSelector");

  if (failures != 0) {
    return 1;
  }
  std::printf("ctml 2.0.0 C++ ok\n");
  return 0;
}
