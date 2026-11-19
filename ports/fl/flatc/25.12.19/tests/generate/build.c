/* Has the flatc of this package's [build-dependencies] compile
 * schema/monster.fbs into a C++ header.  Cabin gives the script that
 * executable's path in CABIN_BIN_FLATC. */

/* Declares `fork`, `execl`, and `waitpid` under `-std=c11`. */
#define _POSIX_C_SOURCE 200809L
/* Quiets MSVC's advice to use its own `_s` functions. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Runs `flatc --cpp -o <out> <schema>` and waits for it, without a shell. */
static int generate(const char *flatc, const char *out, const char *schema) {
#ifdef _WIN32
    /* CreateProcess, as Cabin's documentation of build scripts advises
     * for a program the script did not write. */
    char command[8192];
    STARTUPINFOA startup = {sizeof startup};
    PROCESS_INFORMATION process;
    DWORD status = 1;
    snprintf(command, sizeof command, "flatc --cpp -o \"%s\" \"%s\"", out, schema);
    if (!CreateProcessA(flatc, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
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
        execl(flatc, "flatc", "--cpp", "-o", out, schema, (char *)NULL);
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
    const char *manifest_dir = getenv("CABIN_MANIFEST_DIR");
    const char *flatc = getenv("CABIN_BIN_FLATC");
    char schema[4096];
    FILE *result;
    snprintf(schema, sizeof schema, "%s/schema/monster.fbs", manifest_dir);

    /* Without the path, Windows would look for a `flatc` on PATH. */
    if (!flatc || generate(flatc, out_dir, schema) != 0) {
        fputs("flatc failed\n", stderr);
        return 1;
    }

    result = fopen(getenv("CABIN_RESULT_FILE"), "w");
    if (!result) {
        return 1;
    }
    fprintf(result, "include-dir generate %s\n", out_dir);
    fprintf(result, "rerun-if-changed %s\n", schema);
    return fclose(result) != 0;
}
