// Consumer test for cabin-ports/dtl: string and integer diffs, the shortest
// edit script, unified hunks, patching, a custom comparator and a three-way
// merge.  Expected values come from upstream's README and test suite
// (test/Strdifftest.cpp, test/Intdifftest.cpp, test/Strdiff3test.cpp and the
// files under test/ses and test/hunks).
#include <dtl/dtl.hpp>

#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;

static void check(bool pass, const char *what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

typedef dtl::Diff<char, std::string> StrDiff;
// Diff's own typedefs are private, so name the element types directly.
typedef std::vector<std::pair<char, dtl::elemInfo> > SesVec;
typedef std::vector<dtl::uniHunk<std::pair<char, dtl::elemInfo> > > HunkVec;

static std::string ses_text(const StrDiff &d) {
  std::ostringstream out;
  d.printSES(out);
  return out.str();
}

static std::string unified_text(const StrDiff &d) {
  std::ostringstream out;
  d.printUnifiedFormat(out);
  return out.str();
}

static void test_small_string_diff() {
  std::string a("abc"), b("abd");
  StrDiff d(a, b);
  d.compose();
  std::vector<char> lcs = d.getLcsVec();
  check(d.getEditDistance() == 2, "abc -> abd has edit distance 2");
  check(std::string(lcs.begin(), lcs.end()) == "ab", "abc -> abd has LCS ab");
  check(ses_text(d) == " a\n b\n-c\n+d\n", "abc -> abd SES");

  SesVec ses = d.getSes().getSequence();
  check(ses.size() == 4 && ses[2].first == 'c' && ses[2].second.type == dtl::SES_DELETE &&
            ses[3].first == 'd' && ses[3].second.type == dtl::SES_ADD,
        "abc -> abd SES elements");
  check(d.patch(a) == b, "patch turns abc into abd");

  d.composeUnifiedHunks();
  check(unified_text(d) == "@@ -1,3 +1,3 @@\n a\n b\n-c\n+d\n", "abc -> abd unified hunk");
  check(d.uniPatch(a) == b, "uniPatch turns abc into abd");
}

static void test_two_hunks() {
  std::string a("acbdeaqqqqqqqcbed"), b("acebdabbqqqqqqqabed");
  StrDiff d(a, b);
  d.compose();
  d.composeUnifiedHunks();
  std::vector<char> lcs = d.getLcsVec();
  check(d.getEditDistance() == 6, "diff_test8 edit distance");
  check(std::string(lcs.begin(), lcs.end()) == "acbdaqqqqqqqbed", "diff_test8 LCS");

  HunkVec hunks = d.getUniHunks();
  check(hunks.size() == 2, "diff_test8 has two hunks");
  if (hunks.size() == 2) {
    check(hunks[0].a == 1 && hunks[0].b == 9 && hunks[0].c == 1 && hunks[0].d == 11, "first hunk header");
    check(hunks[1].a == 11 && hunks[1].b == 7 && hunks[1].c == 13 && hunks[1].d == 7, "second hunk header");
  }
  check(unified_text(d) ==
            "@@ -1,9 +1,11 @@\n a\n c\n+e\n b\n d\n-e\n a\n+b\n+b\n q\n q\n q\n"
            "@@ -11,7 +13,7 @@\n q\n q\n q\n-c\n+a\n b\n e\n d\n",
        "diff_test8 unified format");
  check(d.patch(a) == b, "diff_test8 patch");
  check(d.uniPatch(a) == b, "diff_test8 uniPatch");
}

static void test_int_diff() {
  static const int a[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  static const int b[] = {3, 5, 1, 4, 5, 1, 7, 9, 6, 10};
  static const int lcs_expected[] = {3, 4, 5, 7, 9, 10};
  std::vector<int> A(a, a + 10), B(b, b + 10);
  dtl::Diff<int> d(A, B);
  d.compose();
  d.composeUnifiedHunks();
  check(d.getEditDistance() == 8, "int diff edit distance");
  check(d.getLcsVec() == std::vector<int>(lcs_expected, lcs_expected + 6), "int diff LCS");

  std::ostringstream out;
  d.printUnifiedFormat(out);
  check(out.str() == "@@ -1,10 +1,10 @@\n-1\n-2\n 3\n+5\n+1\n 4\n 5\n-6\n+1\n 7\n-8\n 9\n+6\n 10\n",
        "int diff unified format");
  check(d.patch(A) == B, "int diff patch");
}

static void test_edit_distance_only() {
  StrDiff d(std::string("abcdef"), std::string("dacfea"));
  d.onOnlyEditDistance();
  d.compose();
  check(d.getEditDistance() == 6, "edit-distance-only result");
  check(d.getLcsVec().empty() && d.getSes().getSequence().empty(), "edit-distance-only skips LCS and SES");
}

class CaseInsensitive : public dtl::Compare<char> {
public:
  virtual bool impl(const char &a, const char &b) const {
    return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
  }
};

static void test_custom_comparator() {
  dtl::Diff<char, std::string, CaseInsensitive> d(std::string("abc"), std::string("Abc"));
  d.compose();
  std::ostringstream out;
  d.printSES(out);
  check(d.getEditDistance() == 0 && out.str() == " a\n b\n c\n", "case-insensitive comparator");
}

static void test_merge() {
  dtl::Diff3<char, std::string> m1(std::string("qqqabc"), std::string("abc"), std::string("abcdef"));
  m1.compose();
  check(m1.merge() && m1.getMergedSequence() == "qqqabcdef", "three-way merge of both sides' additions");

  dtl::Diff3<char, std::string> m2(std::string("1234567390"), std::string("1234567890"),
                                   std::string("1239567890"));
  m2.compose();
  check(m2.merge() && m2.getMergedSequence() == "1239567390", "three-way merge of separate changes");

  dtl::Diff3<char, std::string> conflict(std::string("adc"), std::string("abc"), std::string("aec"));
  conflict.compose();
  check(!conflict.merge(), "a conflicting change is detected");
}

int main() {
  test_small_string_diff();
  test_two_hunks();
  test_int_diff();
  test_edit_distance_only();
  test_custom_comparator();
  test_merge();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("dtl 1.21.0 C++ ok");
  return 0;
}
