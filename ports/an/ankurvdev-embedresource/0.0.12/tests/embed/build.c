/* Writes three resources and has the embedresource of this package's
 * [build-dependencies] embed them into res.cpp, which the `embed` target
 * compiles.  Cabin gives the script that executable's path in
 * CABIN_BIN_EMBEDRESOURCE.  The script writes the resources itself so that
 * no checkout converts their line endings. */

/* Declares `fork`, `execv`, and `waitpid` under `-std=c11`. */
#define _POSIX_C_SOURCE 200809L
/* Quiets MSVC's advice to use its own `_s` functions. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

enum { PATH_SIZE = 4096 };

static int write_file(const char *dir, const char *name, const char *bytes, size_t size, char *path) {
    FILE *file;
    size_t written;
    snprintf(path, PATH_SIZE, "%s/%s", dir, name);
    file = fopen(path, "wb");
    if (!file) {
        return -1;
    }
    written = fwrite(bytes, 1, size, file);
    return fclose(file) != 0 || written != size ? -1 : 0;
}

/* Runs the tool with `args` (argv[0] first, NULL last) and waits for it,
 * without a shell. */
static int run(const char *tool, char *const args[]) {
#ifdef _WIN32
    /* CreateProcess, as Cabin's documentation of build scripts advises for
     * a program the script did not write.  No argument ends in a
     * backslash, so quoting each one is enough. */
    char command[8 * PATH_SIZE];
    size_t used = 0;
    STARTUPINFOA startup = {sizeof startup};
    PROCESS_INFORMATION process;
    DWORD status = 1;
    for (; *args; ++args) {
        int n = snprintf(command + used, sizeof command - used, "%s\"%s\"", used ? " " : "", *args);
        if (n < 0 || (size_t)n >= sizeof command - used) {
            return -1;
        }
        used += (size_t)n;
    }
    if (!CreateProcessA(tool, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
        return -1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &status);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)status;
#else
    int status = 0;
    pid_t child = fork();
    if (child == 0) {
        execv(tool, args);
        _exit(127);
    }
    if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
#endif
}

int main(void) {
    const char *out_dir = getenv("CABIN_OUT_DIR");
    const char *tool = getenv("CABIN_BIN_EMBEDRESOURCE");
    char output[PATH_SIZE], hello[PATH_SIZE], blob[PATH_SIZE], notes[PATH_SIZE], list[PATH_SIZE];
    char renamed[PATH_SIZE + 8], listed[PATH_SIZE + 8], line[PATH_SIZE + 8];
    char *args[6];
    FILE *result;

    /* Without the path, Windows would look for an `embedresource` on PATH. */
    if (!out_dir || !tool) {
        fputs("CABIN_OUT_DIR or CABIN_BIN_EMBEDRESOURCE is unset\n", stderr);
        return 1;
    }
    snprintf(output, sizeof output, "%s/res.cpp", out_dir);
    snprintf(notes, sizeof notes, "%s/notes-1.txt", out_dir);
    /* The tool's three forms of a resource: a path, `name!path`, and
     * `@file` naming a file of one resource per line. */
    snprintf(line, sizeof line, "%s\n", notes);
    if (write_file(out_dir, "hello.txt", "hello\n", 6, hello) != 0 ||
        write_file(out_dir, "two.bin", "\0\377", 2, blob) != 0 ||
        write_file(out_dir, "notes-1.txt", "abc", 3, notes) != 0 ||
        write_file(out_dir, "list.txt", line, strlen(line), list) != 0) {
        fputs("cannot write a resource\n", stderr);
        return 1;
    }
    snprintf(renamed, sizeof renamed, "blob!%s", blob);
    snprintf(listed, sizeof listed, "@%s", list);

    args[0] = "embedresource";
    args[1] = output;
    args[2] = hello;
    args[3] = renamed;
    args[4] = listed;
    args[5] = NULL;
    if (run(tool, args) != 0) {
        fputs("embedresource failed\n", stderr);
        return 1;
    }

    result = fopen(getenv("CABIN_RESULT_FILE"), "w");
    if (!result) {
        return 1;
    }
    fprintf(result, "source embed %s\n", output);
    return fclose(result) != 0;
}
