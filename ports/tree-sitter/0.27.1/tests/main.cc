// The C++ consumer, without a grammar (tests/main.c parses with one):
// the parser lifecycle through a counting allocator, the language-less
// state, included ranges, the logger, and parse options whose progress
// callback a parser with no language never reaches.
#include <tree_sitter/api.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,      \
                   __LINE__, #cond);                                   \
      std::exit(1);                                                    \
    }                                                                  \
  } while (0)

static std::size_t allocations, releases;

static void* counting_malloc(std::size_t n) {
  ++allocations;
  return std::malloc(n);
}
static void* counting_calloc(std::size_t n, std::size_t size) {
  ++allocations;
  return std::calloc(n, size);
}
static void* counting_realloc(void* p, std::size_t n) {
  if (!p) ++allocations;
  return std::realloc(p, n);
}
static void counting_free(void* p) {
  if (p) ++releases;
  std::free(p);
}

static void log_message(void* payload, TSLogType, const char*) {
  ++*static_cast<int*>(payload);
}

static bool stop(TSParseState* state) {
  ++*static_cast<int*>(state->payload);
  return true;
}

static const char* read_text(void* payload, std::uint32_t byte, TSPoint,
                             std::uint32_t* bytes_read) {
  *bytes_read = byte == 0 ? 3 : 0;
  return byte == 0 ? static_cast<const char*>(payload) : "";
}

static TSRange range(std::uint32_t start, std::uint32_t end) {
  TSRange r;
  r.start_point.row = 0;
  r.start_point.column = start;
  r.end_point.row = 0;
  r.end_point.column = end;
  r.start_byte = start;
  r.end_byte = end;
  return r;
}

int main() {
  CHECK(TREE_SITTER_LANGUAGE_VERSION == 15 &&
        TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION == 13);

  ts_set_allocator(counting_malloc, counting_calloc, counting_realloc,
                   counting_free);
  TSParser* parser = ts_parser_new();
  CHECK(parser != nullptr && allocations > 0);

  // No language: setting none is accepted, and parsing yields no tree.
  CHECK(ts_parser_language(parser) == nullptr);
  CHECK(ts_parser_set_language(parser, nullptr));
  CHECK(ts_parser_language(parser) == nullptr);
  CHECK(ts_parser_parse_string(parser, nullptr, "[1]", 3) == nullptr);

  int progress = 0;
  TSInput input;
  input.payload = const_cast<char*>("[1]");
  input.read = read_text;
  input.encoding = TSInputEncodingUTF8;
  input.decode = nullptr;
  TSParseOptions options;
  options.payload = &progress;
  options.progress_callback = stop;
  CHECK(ts_parser_parse_with_options(parser, nullptr, input, options) ==
        nullptr);
  CHECK(progress == 0);

  // Included ranges must be ordered and disjoint.
  const TSRange ordered[] = {range(0, 5), range(10, 15)};
  CHECK(ts_parser_set_included_ranges(parser, ordered, 2));
  std::uint32_t count = 0;
  const TSRange* got = ts_parser_included_ranges(parser, &count);
  CHECK(count == 2 && got[1].start_byte == 10 && got[1].end_byte == 15);
  const TSRange overlapping[] = {range(0, 12), range(10, 15)};
  CHECK(!ts_parser_set_included_ranges(parser, overlapping, 2));
  got = ts_parser_included_ranges(parser, &count);
  CHECK(count == 2 && got[0].end_byte == 5);
  CHECK(ts_parser_set_included_ranges(parser, nullptr, 0));
  got = ts_parser_included_ranges(parser, &count);
  CHECK(count == 1 && got[0].start_byte == 0 && got[0].end_byte == UINT32_MAX);

  int logged = 0;
  TSLogger logger;
  logger.payload = &logged;
  logger.log = log_message;
  ts_parser_set_logger(parser, logger);
  CHECK(ts_parser_logger(parser).payload == &logged &&
        ts_parser_logger(parser).log == log_message);

  ts_parser_reset(parser);
  ts_parser_delete(parser);
  CHECK(releases > 0 && releases == allocations);
  ts_set_allocator(nullptr, nullptr, nullptr, nullptr);

  std::puts("ok");
  return 0;
}
