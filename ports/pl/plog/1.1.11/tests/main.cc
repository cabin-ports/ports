// The C++ consumer: plog in C++11 - severity filtering that skips the
// stream expression, the conditional macros, separate and chained logger
// instances, DynamicAppender, record streaming (containers, pairs, null
// strings, printf, PLOG_PRINT_VAR, hexdump and ascdump), the text, CSV,
// function and message-only formatters, severity names, a
// RollingFileAppender that keeps a bounded set of CSV files, the file
// initializer that picks its formatter by extension, and, on Windows, the
// event-log source registry that EventLogAppender uses.
#include <plog/Appenders/DynamicAppender.h>
#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/CsvFormatter.h>
#include <plog/Formatters/FuncMessageFormatter.h>
#include <plog/Formatters/MessageOnlyFormatter.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Helpers/AscDump.h>
#include <plog/Helpers/HexDump.h>
#include <plog/Helpers/PrintVar.h>
#include <plog/Init.h>
#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Log.h>
#ifdef _WIN32
#include <plog/Appenders/EventLogAppender.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

static bool ok = true;

static void check(bool pass, const char *what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

typedef plog::util::nstring nstring;

// Keeps every record that reaches it.
class Capture : public plog::IAppender {
public:
    void write(const plog::Record &record) override {
        severities.push_back(record.getSeverity());
        messages.push_back(record.getMessage());
    }
    nstring last() const { return messages.empty() ? nstring() : messages.back(); }

    std::vector<plog::Severity> severities;
    std::vector<nstring> messages;
};

enum { Second = 1, Chained = 2, Unused = 3, Dynamic = 4, Rolling = 5, TxtFile = 6, CsvFile = 7 };

static Capture main_capture, second_capture;

static int touched = 0;
static int touch() { return ++touched; }

static void filters_by_severity() {
    PLOGI << touch();
    check(plog::get() == nullptr && touched == 0, "logging before init does nothing");

    plog::Logger<PLOG_DEFAULT_INSTANCE_ID> &logger = plog::init(plog::info, &main_capture);
    check(plog::get() == &logger && logger.getMaxSeverity() == plog::info, "init installs the default logger");

    PLOGD << "debug " << touch();
    check(main_capture.messages.empty() && touched == 0,
          "a record above the maximum is dropped without evaluating its stream");
    PLOGI << "info " << touch();
    PLOGW << "warning";
    PLOG_ERROR << "error";
    check(main_capture.messages.size() == 3 && main_capture.messages[0] == PLOG_NSTR("info 1") && touched == 1 &&
              main_capture.severities[0] == plog::info && main_capture.severities[1] == plog::warning &&
              main_capture.severities[2] == plog::error,
          "records at or below the maximum reach the appender in order");

    int x = 0;
    PLOG_INFO_IF(x == 0) << "if true";
    PLOG_INFO_IF(x != 0) << "if false";
    PLOG_DEBUG_IF(x == 0) << "if true but debug";
    check(main_capture.messages.size() == 4 && main_capture.last() == PLOG_NSTR("if true"),
          "PLOG_*_IF logs only when both its condition and its severity allow");
    IF_PLOG(plog::info) x += 1;
    IF_PLOG(plog::debug) x += 10;
    check(x == 1, "IF_PLOG guards a statement by severity");

    logger.setMaxSeverity(plog::verbose);
    PLOGV << "verbose";
    check(logger.checkSeverity(plog::verbose) && main_capture.last() == PLOG_NSTR("verbose"),
          "raising the maximum lets verbose records through");
    logger.setMaxSeverity(plog::none);
    PLOG_FATAL << "fatal";
    check(main_capture.messages.size() == 5 && !logger.checkSeverity(plog::fatal), "a none maximum drops everything");
    logger.setMaxSeverity(plog::info);
}

static void separates_and_chains_instances() {
    const std::size_t main_before = main_capture.messages.size();
    plog::init<Second>(plog::debug, &second_capture);
    PLOGD_(Second) << "to the second logger";
    check(second_capture.messages.size() == 1 && second_capture.last() == PLOG_NSTR("to the second logger") &&
              main_capture.messages.size() == main_before,
          "an instance id selects its own logger");

    // A logger is an appender: the second logger filters what the chained one forwards.
    plog::init<Chained>(plog::verbose, plog::get<Second>());
    PLOGW_(Chained) << "chained";
    check(second_capture.messages.size() == 2 && second_capture.last() == PLOG_NSTR("chained"),
          "a chained logger forwards to the next logger");
    PLOGV_(Chained) << "too verbose for the second logger";
    check(second_capture.messages.size() == 2, "the next logger applies its own maximum");

    PLOGI_(Unused) << touch();
    check(plog::get<Unused>() == nullptr && touched == 1, "an uninitialized instance logs nothing");

    static Capture first, second;
    static plog::DynamicAppender dynamic;
    plog::init<Dynamic>(plog::info, &dynamic);
    dynamic.addAppender(&first).addAppender(&second);
    PLOGI_(Dynamic) << "both";
    dynamic.removeAppender(&first);
    PLOGI_(Dynamic) << "second only";
    check(first.messages.size() == 1 && first.last() == PLOG_NSTR("both") && second.messages.size() == 2 &&
              second.last() == PLOG_NSTR("second only"),
          "DynamicAppender adds and removes appenders");
}

static void streams_values() {
    const std::vector<int> numbers = {1, 2, 3};
    PLOGI << numbers;
    check(main_capture.last() == PLOG_NSTR("[1, 2, 3]"), "a container prints its elements");
    const std::map<std::string, int> colors = {{"red", 1}, {"blue", 4}};
    PLOGI << colors;
    check(main_capture.last() == PLOG_NSTR("[blue:4, red:1]"), "a map prints its pairs in order");
    PLOGI << std::make_pair(5, "five");
    check(main_capture.last() == PLOG_NSTR("5:five"), "a pair prints as first:second");
    const char *nothing = nullptr;
    PLOGI << nothing;
    check(main_capture.last() == PLOG_NSTR("(null)"), "a null string prints as (null)");

    PLOGI.printf("%d-%s-%.1f", 42, "x", 2.5);
    check(main_capture.last() == PLOG_NSTR("42-x-2.5"), "printf formats its arguments");
    PLOGI.printf("%s|%05d", std::string(300, 'y').c_str(), 7);
    check(main_capture.last() == nstring(300, PLOG_NSTR('y')) + PLOG_NSTR("|00007"),
          "printf sizes a long message");

    const int width = 3, height = 4;
    PLOGI << PLOG_PRINT_VAR(width, height);
    check(main_capture.last() == PLOG_NSTR("width: 3, height: 4"), "PLOG_PRINT_VAR names its variables");

    const unsigned char bytes[] = {0xde, 0xad, 0xbe, 0xef, 0x01};
    PLOGI << plog::hexdump(bytes);
    check(main_capture.last() == PLOG_NSTR("de ad be ef 01"), "hexdump prints bytes");
    PLOGI << plog::hexdump(bytes).group(2).separator(":", " | ");
    check(main_capture.last() == PLOG_NSTR("de:ad | be:ef | 01"), "hexdump groups bytes");
    const char text[] = {'o', 'k', '\n', '!'};
    PLOGI << plog::ascdump(text);
    check(main_capture.last() == PLOG_NSTR("ok.!"), "ascdump masks unprintable bytes");
}

static bool ends_with(const nstring &text, const nstring &tail) {
    return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

// The record's local time as the formatters print it: the date and time
// in the given strftime format, then a dot and the milliseconds.
static nstring stamp(const plog::Record &record, const char *format) {
    const std::time_t seconds = record.getTime().time;
    std::tm local;
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char text[64];
    const std::size_t length = std::strftime(text, sizeof text, format, &local);
    std::snprintf(text + length, sizeof text - length, ".%03d", static_cast<int>(record.getTime().millitm));
    const std::string narrow(text);
    return nstring(narrow.begin(), narrow.end());
}

static void formats_records() {
    plog::Record record(plog::warning, "make", 42, "file.cc", nullptr, 0);
    record << "say \"hi\"";
    check(record.getTid() != 0, "a record carries the thread id");
    check(plog::MessageOnlyFormatter::format(record) == PLOG_NSTR("say \"hi\"\n"), "the message-only formatter");
    check(plog::FuncMessageFormatter::format(record) == PLOG_NSTR("make@42: say \"hi\"\n"),
          "the function-and-message formatter");

    const nstring txt = plog::TxtFormatter::format(record);
    plog::util::nostringstream txt_tail;
    txt_tail << PLOG_NSTR(" WARN  [") << record.getTid() << PLOG_NSTR("] [make@42] say \"hi\"\n");
    check(txt == stamp(record, "%Y-%m-%d %H:%M:%S") + txt_tail.str(),
          "the text formatter: local time, padded severity, thread, function@line, message");

    check(plog::CsvFormatter::header() == PLOG_NSTR("Date;Time;Severity;TID;This;Function;Message\n"),
          "the CSV header");
    const nstring csv = plog::CsvFormatter::format(record);
    plog::util::nostringstream csv_head;
    csv_head << stamp(record, "%Y/%m/%d;%H:%M:%S") << PLOG_NSTR(";WARN;") << record.getTid() << PLOG_NSTR(";");
    check(csv.compare(0, csv_head.str().size(), csv_head.str()) == 0 &&
              ends_with(csv, PLOG_NSTR(";make@42;\"say \"\"hi\"\"\"\n")),
          "the CSV formatter: local time, severity, thread, function@line, quoted message with doubled quotes");

    check(std::strcmp(plog::severityToString(plog::warning), "WARN") == 0 &&
              std::strcmp(plog::severityToString(plog::verbose), "VERB") == 0 &&
              plog::severityFromString("error") == plog::error && plog::severityFromString("Debug") == plog::debug &&
              plog::severityFromString("bogus") == plog::none,
          "severity names convert both ways");
}

static std::string slurp(const std::string &path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The record numbers, written as <n>, that a log file holds.
static std::vector<int> numbers_in(const std::string &text) {
    std::vector<int> found;
    for (std::size_t at = text.find('<'); at != std::string::npos; at = text.find('<', at + 1)) {
        found.push_back(std::atoi(text.c_str() + at + 1));
    }
    return found;
}

// Whether the numbers are non-empty and each is one more than the last.
static bool consecutive(const std::vector<int> &numbers) {
    for (std::size_t i = 1; i < numbers.size(); ++i) {
        if (numbers[i] != numbers[i - 1] + 1) return false;
    }
    return !numbers.empty();
}

static void rolls_files() {
    // Among the probe's own build outputs: `cabin run` keeps the caller's
    // working directory.
    const char *const dir = std::getenv("CABIN_PROFILE_DIR");
    check(dir != nullptr, "CABIN_PROFILE_DIR is set");
    if (dir == nullptr) return;
    const std::string base = std::string(dir) + "/port-probe-plog";
    const std::string files[] = {base + ".csv", base + ".1.csv", base + ".2.csv", base + ".3.csv"};
    for (const std::string &file : files) std::remove(file.c_str());

    {
        plog::RollingFileAppender<plog::CsvFormatter> roller(files[0].c_str(), 1000, 3);
        plog::Logger<Rolling> logger(plog::info);
        logger.addAppender(&roller);
        for (int i = 0; i < 60; ++i) PLOGI_(Rolling) << "<" << i << ">" << std::string(200, 'x');
    }

    const std::string header = "\xEF\xBB\xBF" "Date;Time;Severity;TID;This;Function;Message";
    std::vector<int> previous;
    for (int n = 2; n >= 0; --n) {  // oldest first
        const std::string text = slurp(files[n]);
        const std::vector<int> numbers = numbers_in(text);
        check(text.compare(0, header.size(), header) == 0, "every log file starts with a BOM and the CSV header");
        check(text.size() > 1000 || n == 0, "a log file rolls only once it passes the size limit");
        check(text.size() < 1400, "a log file rolls soon after it passes the size limit");
        check(consecutive(numbers), "a log file holds consecutive records");
        const std::ptrdiff_t lines = std::count(text.begin(), text.end(), '\n');
        const std::ptrdiff_t returns = std::count(text.begin(), text.end(), '\r');
#ifdef _WIN32
        check(lines > 0 && returns == lines, "log files end lines with CRLF on Windows");
#else
        check(lines > 0 && returns == 0, "log files end lines with LF");
#endif
        check(numbers.empty() || previous.empty() || numbers.front() == previous.back() + 1,
              "a newer log file continues where the older one stopped");
        previous = numbers;
    }
    check(!previous.empty() && previous.back() == 59, "the current log file holds the newest record");
    check(std::ifstream(files[3].c_str()).fail(), "rolling keeps no more than three files");
    for (const std::string &file : files) std::remove(file.c_str());

    // The initializer's appenders are static, so their files stay open until
    // exit; each run starts them afresh.
    const std::string txt_file = base + "-init.txt", csv_file = base + "-init.csv";
    std::remove(txt_file.c_str());
    std::remove(csv_file.c_str());
    plog::init<TxtFile>(plog::info, txt_file.c_str());
    plog::init<CsvFile>(plog::info, csv_file.c_str());
    PLOGI_(TxtFile) << "to text";
    PLOGD_(TxtFile) << "too detailed";
    PLOGI_(CsvFile) << "to csv";
    const std::string txt = slurp(txt_file), csv = slurp(csv_file);
    check(txt.compare(0, 3, "\xEF\xBB\xBF") == 0 && txt.find(" INFO  [") != std::string::npos &&
              txt.find("] to text") != std::string::npos && txt.find(';') == std::string::npos &&
              txt.find("too detailed") == std::string::npos,
          "init writes a .txt file through the text formatter at its severity");
    check(csv.compare(0, header.size(), header) == 0 && csv.find(";INFO;") != std::string::npos &&
              csv.find(";\"to csv\"") != std::string::npos,
          "init writes a .csv file through the CSV formatter");
}

static void finds_event_sources() {
#ifdef _WIN32
    // Read-only registry lookups through advapi32: the event log service
    // registers its own source in the System log.
    check(plog::EventLogAppenderRegistry::exists(PLOG_NSTR("EventLog"), PLOG_NSTR("System")),
          "the event log service's source is registered");
    check(!plog::EventLogAppenderRegistry::exists(PLOG_NSTR("port-probe-plog-unregistered")),
          "an unregistered event source is not found");
#endif
}

int main() {
    filters_by_severity();
    separates_and_chains_instances();
    streams_values();
    formats_records();
    rolls_files();
    finds_event_sources();
    std::printf("plog (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
