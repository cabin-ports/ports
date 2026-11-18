// Every dialog waits for a person, so the test answers them itself where pfd allows it.  Outside
// Windows each dialog runs a helper program found on PATH: osascript on macOS, elsewhere zenity,
// matedialog, qarma or kdialog.  The test sets PATH to a directory of fake helpers that record the
// command pfd builds and reply with a fixed answer.  That covers command construction and quoting,
// the child-process plumbing, result parsing, and ready() and kill() on a helper that never
// answers.  Win32 dialogs offer no such seam, so on Windows the test only builds and links them.
#include <portable-file-dialogs.h>

#include <cstdio>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

int failures = 0;

void check(bool ok, const char *what, int line) {
    if (!ok) {
        std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

#if !defined(_WIN32)
std::string dir;

void writeScript(const char *name, const std::string &body) {
    const std::string path = dir + "/" + name;
    std::ofstream(path.c_str()) << "#!/bin/sh\n" << body;
    chmod(path.c_str(), 0755);
}

// The next helper run prints out and exits with code.
void answer(const char *out, const char *code) {
    setenv("PFD_TEST_OUT", out, 1);
    setenv("PFD_TEST_CODE", code, 1);
}

// The arguments of the last helper run, each preceded and followed by a newline.
std::string recorded() {
    std::ifstream in((dir + "/args").c_str());
    std::ostringstream text;
    text << '\n' << in.rdbuf();
    return text.str();
}

bool contains(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

bool hasArg(const std::string &text, const std::string &arg) { return contains(text, '\n' + arg + '\n'); }
#endif

}  // namespace

int main(int argc, char **) {
    const pfd::opt options = pfd::opt::multiselect | pfd::opt::force_path;
    CHECK(options & pfd::opt::multiselect);
    CHECK(options & pfd::opt::force_path);
    CHECK(!(options & pfd::opt::force_overwrite));

#if defined(_WIN32)
    // Never taken: it only makes the build compile and link every dialog.
    if (argc > 100) {
        pfd::notify("t", "m");
        pfd::message("t", "m", pfd::choice::yes_no, pfd::icon::question).result();
        pfd::open_file("t", "", {"Text", "*.txt"}, pfd::opt::multiselect).result();
        pfd::save_file("t").result();
        pfd::select_folder("t").result();
    }
#else
    (void)argc;
    // The directory becomes PATH, which cannot name a directory containing ':'.
    const char *tmp = std::getenv("TMPDIR");
    std::string root = tmp && *tmp ? tmp : "/tmp";
    if (root.find(':') != std::string::npos)
        root = "/tmp";
    std::string pattern = root + "/pfd-test-XXXXXX";
    if (!mkdtemp(&pattern[0])) {
        std::perror("mkdtemp");
        return 1;
    }
    dir = pattern;
    setenv("PFD_TEST_ARGS", (dir + "/args").c_str(), 1);
    const std::string helper = "printf '%s\\n' \"$@\" > \"$PFD_TEST_ARGS\"\n"
                               "[ -z \"$PFD_TEST_SLEEP\" ] || exec /bin/sleep 1000000\n"
                               "printf '%s' \"$PFD_TEST_OUT\"\n"
                               "exit \"$PFD_TEST_CODE\"\n";
    writeScript("osascript", helper);
    writeScript("zenity", helper);
    // pfd looks for zenity, matedialog, qarma and kdialog with `which`; only zenity is found.
    writeScript("which", "[ \"$1\" = zenity ]\n");
    setenv("PATH", dir.c_str(), 1);
#if __APPLE__
    const bool osascript = true;
#else
    const bool osascript = false;
#endif
    std::string args;

    // Arguments reach the helper unchanged, without a shell; osascript gets them quoted for AppleScript.
    const std::string title = "Quote \" back \\ tick ' $(id)";
    answer(osascript ? "button returned:Yes\n" : "Yes\n", osascript ? "0" : "1");
    CHECK(pfd::message(title, "Body", pfd::choice::yes_no, pfd::icon::question).result() == pfd::button::yes);
    args = recorded();
    if (osascript) {
        CHECK(hasArg(args, "-e"));
        CHECK(contains(args, "display dialog \"Body\" with title \"Quote \\\" back \\\\ tick ' $(id)\""));
        CHECK(contains(args, "buttons {\"Yes\", \"No\"}"));
    } else {
        CHECK(hasArg(args, "--question") && hasArg(args, "--switch") && hasArg(args, "--extra-button"));
        CHECK(hasArg(args, "--title") && hasArg(args, title) && hasArg(args, "--text") && hasArg(args, "Body"));
        CHECK(hasArg(args, "--icon-name=dialog-question"));
    }

    // Without a recognised answer, the exit code decides.
    answer("", "0");
    CHECK(pfd::message("T", "B").result() == pfd::button::ok);
    answer("", "1");
    CHECK(pfd::message("T", "B").result() == pfd::button::cancel);

    // osascript ends its multiple-selection output with an empty line.
    answer(osascript ? "/a/one.txt\n/b/two words.md\n\n" : "/a/one.txt\n/b/two words.md\n", "0");
    const std::vector<std::string> picked =
        pfd::open_file("Pick", "/start", {"Text", "*.txt *.md"}, pfd::opt::multiselect).result();
    CHECK(picked.size() == 2 && picked[0] == "/a/one.txt" && picked[1] == "/b/two words.md");
    args = recorded();
    if (osascript) {
        CHECK(contains(args, "choose file with multiple selections allowed default location \"/start\" "
                             "with prompt \"Pick\" of type {\"txt\",\"md\"}"));
    } else {
        CHECK(hasArg(args, "--file-selection") && hasArg(args, "--filename=/start") && hasArg(args, "Pick"));
        CHECK(hasArg(args, "--file-filter") && hasArg(args, "Text|*.txt *.md") && hasArg(args, "--multiple"));
    }

    answer("/c/out file.txt\n", "0");
    CHECK(pfd::save_file("Save", "/c/in.txt", {"All Files", "*"}, pfd::opt::force_overwrite).result() ==
          "/c/out file.txt");
    args = recorded();
    if (osascript) {
        CHECK(contains(args, "choose file name default location \"/c/in.txt\" with prompt \"Save\""));
    } else {
        CHECK(hasArg(args, "--save") && !hasArg(args, "--confirm-overwrite"));
    }

    answer("/d/folder\n", "0");
    CHECK(pfd::select_folder("Folder", "/d").result() == "/d/folder");
    args = recorded();
    if (osascript) {
        CHECK(contains(args, "choose folder default location \"/d\" with prompt \"Folder\""));
    } else {
        CHECK(hasArg(args, "--directory") && hasArg(args, "--confirm-overwrite"));
    }

    // A notification's helper is waited for when the notify object is destroyed.
    answer("", "0");
    pfd::notify("Note", "Done", pfd::icon::warning);
    args = recorded();
    if (osascript) {
        CHECK(contains(args, "display notification \"Done\" with title \"Note\""));
    } else {
        CHECK(contains(args, "\n--notification\n--window-icon\nwarning\n--text\nNote\nDone\n"));
    }

    // A dialog nobody answers: ready() polls without blocking, and kill() ends it.  The helper
    // sleeps for days, so a kill() or ready() that waited for it would hang the test rather than
    // pass.
    setenv("PFD_TEST_SLEEP", "1", 1);
    pfd::message pending("Wait", "Never answered");
    CHECK(!pending.ready(0));
    CHECK(pending.kill());
    CHECK(pending.ready(0));

    const char *files[] = {"args", "osascript", "zenity", "which"};
    for (const char *file : files)
        unlink((dir + "/" + file).c_str());
    rmdir(dir.c_str());
#endif

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("portable-file-dialogs: ok\n");
    return 0;
}
