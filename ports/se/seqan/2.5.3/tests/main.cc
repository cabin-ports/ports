// ArgumentParser's default update check goes online; a test must not touch the network.
#define SEQAN_DISABLE_VERSION_CHECK

#include <seqan/align.h>
#include <seqan/arg_parse.h>
#include <seqan/index.h>
#include <seqan/modifier.h>
#include <seqan/seq_io.h>
#include <seqan/sequence.h>
#include <seqan/stream.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)

template <typename T>
std::string str(T const& value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

void test_sequence() {
  seqan2::DnaString dna = "ACGTTA";
  seqan2::reverseComplement(dna);
  CHECK(str(dna) == "TAACGT" && seqan2::length(dna) == 6u);
  seqan2::Dna5String n = "ACGTN";
  CHECK(seqan2::ordValue(n[4]) == 4u && str(seqan2::infix(n, 1, 4)) == "CGT");
}

void test_align() {
  seqan2::CharString a = "kitten";
  seqan2::CharString b = "sitting";
  // Unit costs: the score is minus the edit distance (two substitutions, one insertion).
  CHECK(seqan2::globalAlignmentScore(a, b, seqan2::Score<int, seqan2::Simple>(0, -1, -1)) == -3);

  seqan2::Align<seqan2::DnaString> align;
  seqan2::resize(seqan2::rows(align), 2);
  seqan2::assignSource(seqan2::row(align, 0), "ACGT");
  seqan2::assignSource(seqan2::row(align, 1), "AGT");
  // The only alignment scoring 2 gaps the C: three matches, one gap.
  CHECK(seqan2::globalAlignment(align, seqan2::Score<int, seqan2::Simple>(1, -1, -1)) == 2);
  CHECK(str(seqan2::row(align, 0)) == "ACGT" && str(seqan2::row(align, 1)) == "A-GT");

  seqan2::Align<seqan2::DnaString> local;
  seqan2::resize(seqan2::rows(local), 2);
  seqan2::assignSource(seqan2::row(local, 0), "AAACGTAAA");
  seqan2::assignSource(seqan2::row(local, 1), "TTCGTTT");
  // The best local match is the shared CGT: three matches at 2 each.
  CHECK(seqan2::localAlignment(local, seqan2::Score<int, seqan2::Simple>(2, -1, -2)) == 6);
  CHECK(str(seqan2::row(local, 0)) == "CGT" && str(seqan2::row(local, 1)) == "CGT");
  CHECK(seqan2::clippedBeginPosition(seqan2::row(local, 0)) == 3 &&
        seqan2::clippedBeginPosition(seqan2::row(local, 1)) == 2);
}

template <typename TIndexSpec>
std::vector<unsigned> occurrences(seqan2::DnaString text, seqan2::DnaString const& pattern) {
  seqan2::Index<seqan2::DnaString, TIndexSpec> index(text);
  seqan2::Finder<seqan2::Index<seqan2::DnaString, TIndexSpec> > finder(index);
  std::vector<unsigned> hits;
  while (seqan2::find(finder, pattern)) hits.push_back(static_cast<unsigned>(seqan2::beginPosition(finder)));
  std::sort(hits.begin(), hits.end());
  return hits;
}

void test_index() {
  seqan2::DnaString const text = "ACGTACGTTACG";
  CHECK(occurrences<seqan2::IndexEsa<> >(text, "ACG") == std::vector<unsigned>{0, 4, 9});
  CHECK(occurrences<seqan2::FMIndex<> >(text, "ACG") == std::vector<unsigned>{0, 4, 9});
  CHECK(occurrences<seqan2::FMIndex<> >(text, "GTT") == std::vector<unsigned>{6});
  CHECK(occurrences<seqan2::FMIndex<> >(text, "TTT").empty());
}

void test_fasta() {
  std::istringstream in(">r1 first\nACGT\nAC\n>r2\nGGNN\n");
  auto it = seqan2::directionIterator(in, seqan2::Input());
  seqan2::CharString id;
  seqan2::Dna5String seq;
  seqan2::readRecord(id, seq, it, seqan2::Fasta());
  CHECK(id == "r1 first" && str(seq) == "ACGTAC");
  seqan2::readRecord(id, seq, it, seqan2::Fasta());
  CHECK(id == "r2" && str(seq) == "GGNN" && seqan2::atEnd(it));

  std::ostringstream out;
  seqan2::writeRecord(out, seqan2::CharString("r3"), seqan2::DnaString("TTGA"), seqan2::Fasta());
  CHECK(out.str() == ">r3\nTTGA\n");
}

void test_arg_parse() {
  seqan2::ArgumentParser parser("demo");
  seqan2::addOption(parser, seqan2::ArgParseOption("n", "number", "A number.", seqan2::ArgParseArgument::INTEGER, "INT"));
  char const* argv[] = {"demo", "-n", "5"};
  std::ostringstream out, err;
  CHECK(seqan2::parse(parser, 3, argv, out, err) == seqan2::ArgumentParser::PARSE_OK);
  int n = 0;
  CHECK(seqan2::getOptionValue(n, parser, "number") && n == 5);
  CHECK(!seqan2::hasOption(parser, "version-check"));
}

}  // namespace

int main() {
  CHECK(SEQAN_VERSION_MAJOR == 2 && SEQAN_VERSION_MINOR == 5 && SEQAN_VERSION_PATCH == 3);
  test_sequence();
  test_align();
  test_index();
  test_fasta();
  test_arg_parse();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("seqan 2.5.3 ok");
  return 0;
}
