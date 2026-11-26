#include "ApprovalTests.hpp"

#include <chrono>
#include <cstdio>
#ifdef _WIN32
#include <direct.h>
#define CHDIR _chdir
#else
#include <unistd.h>
#define CHDIR chdir
#endif
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace
{
    using namespace ApprovalTests;

    int failures = 0;

    void check(bool ok, const char* what)
    {
        if (!ok)
        {
            std::cerr << "FAILED: " << what << "\n";
            ++failures;
        }
    }

    class FixedNamer : public ApprovalNamer
    {
    public:
        std::string getApprovedFile(std::string ext) const override
        {
            return "approval_probe" + SystemUtils::getDirectorySeparator() + "probe.approved" + ext;
        }
        std::string getReceivedFile(std::string ext) const override
        {
            return "approval_probe" + SystemUtils::getDirectorySeparator() + "probe.received" + ext;
        }
    };

    class SilentReporter : public Reporter
    {
    public:
        bool report(std::string, std::string) const override
        {
            return true;
        }
    };

    template <typename Exception, typename F> bool throws(F f)
    {
        try
        {
            f();
        }
        catch (const Exception&)
        {
            return true;
        }
        return false;
    }
}

int main()
{
    // verify() logs under ./.approval_tests_temp, so run in a writable directory.
    std::string tmp = SystemUtils::safeGetEnv(SystemUtils::isWindowsOs() ? "TEMP" : "TMPDIR");
    if (tmp.empty())
    {
        tmp = SystemUtils::isWindowsOs() ? "." : "/tmp";
    }
    // A per-run directory keeps concurrent runs from sharing files.
    tmp += SystemUtils::getDirectorySeparator() + "approval-tests-cpp-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    SystemUtils::ensureDirectoryExists(tmp);
    check(CHDIR(tmp.c_str()) == 0, "chdir to run directory");

    check(std::string(APPROVAL_TESTS_VERSION_STR) == "10.14.0", "version");

    check(StringUtils::replaceAll("a-b-c", "-", "++") == "a++b++c", "replaceAll");
    check(StringUtils::toLower("MiXed") == "mixed", "toLower");
    check(StringUtils::trim("  x y \t") == "x y", "trim");
    check(StringUtils::beginsWith("approval", "app") &&
              !StringUtils::endsWith("approval", "app"),
          "beginsWith/endsWith");
    const std::string sep = SystemUtils::getDirectorySeparator();
    check(FileUtils::getDirectory("a" + sep + "b" + sep + "c.txt") == "a" + sep + "b" + sep,
          "getDirectory");
    check(FileUtils::getExtensionWithDot("a" + sep + "b" + sep + "c.txt") == ".txt", "extension");

    check(Scrubbers::scrubGuid("a 2fd78d4a-ad49-447d-96a8-deda585a9aa5 b "
                               "2fd78d4a-ad49-447d-96a8-deda585a9aa5 c "
                               "00000000-0000-0000-0000-000000000001") ==
              "a guid_1 b guid_1 c guid_2",
          "scrubGuid");
    check(Scrubbers::createRegexScrubber("[0-9]+", "N")("a 12 b 345") == "a N b N",
          "regex scrubber");

    // verify() against an approved file the test writes beside the received one.
    const std::string approved = "approval_probe" + sep + "probe.approved.txt";
    const std::string received = "approval_probe" + sep + "probe.received.txt";
    SystemUtils::ensureParentDirectoryExists(approved);
    {
        std::ofstream out(approved.c_str());
        out << "hello N\nworld N\n";
    }
    check(FileUtils::fileExists(approved), "fileExists");

    SilentReporter reporter; // Options keeps a reference
    Options opts =
        Options(reporter).withNamer(std::make_shared<FixedNamer>());

    Approvals::verify(std::string("hello N\nworld N"), opts);
    check(!FileUtils::fileExists(received), "received removed after a match");

    check(throws<ApprovalMismatchException>(
              [&] { Approvals::verify(std::string("changed"), opts); }),
          "mismatch throws");
    check(FileUtils::readFileThrowIfMissing(received) == "changed\n",
          "received file kept on mismatch");

    Approvals::verify(
        std::string("hello 5\nworld 99"),
        opts.withScrubber(Scrubbers::createRegexScrubber("[0-9]+", "N")));
    check(!FileUtils::fileExists(received), "scrubbed match");

    std::remove(approved.c_str());
    check(throws<ApprovalMissingException>(
              [&] { Approvals::verify(std::string("x"), opts); }),
          "missing approved throws");
    std::remove(received.c_str());
    std::remove("approval_probe"); // best effort: the directory is empty here

    return failures == 0 ? 0 : 1;
}
