#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <zip/zip.h>

static bool ok = true;

static bool check(bool cond, const char *what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ok = false;
    }
    return cond;
}

static bool add(zip_t *zip, const char *name, const std::string &data) {
    return zip_entry_open(zip, name) == 0 && zip_entry_write(zip, data.data(), data.size()) == 0 &&
           zip_entry_close(zip) == 0;
}

static std::string copy_out(zip_t *zip) {
    void *buf = nullptr;
    size_t size = 0;
    std::string out;
    if (zip_stream_copy(zip, &buf, &size) > 0) out.assign(static_cast<char *>(buf), size);
    std::free(buf);
    return out;
}

// What zip_entry_read gives back for name, or the error code it returned.
static std::string read_entry(zip_t *zip, const char *name, ssize_t *result) {
    void *buf = nullptr;
    size_t size = 0;
    std::string out;
    *result = zip_entry_open(zip, name);
    if (*result == 0) {
        *result = zip_entry_read(zip, &buf, &size);
        if (*result >= 0) out.assign(static_cast<char *>(buf), size);
        std::free(buf);
        zip_entry_close(zip);
    }
    return out;
}

// A stored encrypted entry stamped 00:00:00 or 00:00:01 makes its archive
// unreadable (see cabin.toml), so a write in those two seconds is redone.
static std::string encrypt(int level, const std::string &secret) {
    for (;;) {
        zip_t *zip = zip_stream_open_with_password(nullptr, 0, level, 'w', "s3cret");
        check(zip != nullptr && add(zip, "secret.txt", secret), "an entry is written with a password");
        std::string archive = zip != nullptr ? copy_out(zip) : std::string();
        if (zip != nullptr) zip_stream_close(zip);
        size_t central = archive.rfind("PK\1\2");
        if (level != 0 || central == std::string::npos || archive[central + 12] != 0 || archive[central + 13] != 0)
            return archive;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// Level 0 encrypts stored data; other levels encrypt deflate's output.
static void password(int level, const std::string &secret) {
    std::string archive = encrypt(level, secret);
    if (!check(!archive.empty(), "the encrypted archive is copied out")) return;
    check(archive.find(secret) == std::string::npos, "the stored entry does not hold the plaintext");

    ssize_t result = 0;
    zip_t *zip = zip_stream_open_with_password(archive.data(), archive.size(), 0, 'r', "s3cret");
    check(zip != nullptr && read_entry(zip, "secret.txt", &result) == secret && result == (ssize_t) secret.size(),
          "the right password decrypts it");
    if (zip != nullptr) zip_stream_close(zip);

    zip = zip_stream_open_with_password(archive.data(), archive.size(), 0, 'r', "s3creT");
    check(zip != nullptr && (read_entry(zip, "secret.txt", &result), result == ZIP_EPASSWD),
          "a wrong password is ZIP_EPASSWD");
    if (zip != nullptr) zip_stream_close(zip);

    zip = zip_stream_open(archive.data(), archive.size(), 0, 'r');
    check(zip != nullptr && (read_entry(zip, "secret.txt", &result), result < 0), "no password does not read it");
    if (zip != nullptr) zip_stream_close(zip);
}

// Info-ZIP's zip 3.0 wrote this with -X from files stamped 12:34:56, not
// midnight (see cabin.toml): stored.txt (-0 -P s3cret), deflated.txt
// (-P s3cret) and plain.txt (no password).
static const char info_zip_archive[] =
    "\x50\x4b\x03\x04\x0a\x00\x09\x00\x00\x00\x5c\x64\x21\x5c\xbf\xfc"
    "\x1f\x45\x1e\x00\x00\x00\x12\x00\x00\x00\x0a\x00\x00\x00\x73\x74"
    "\x6f\x72\x65\x64\x2e\x74\x78\x74\xc0\x71\x88\xde\xcd\x99\xe1\x5e"
    "\xda\xa3\xdf\xba\xe4\x6f\xb5\x39\x67\xb6\x89\xe0\xbd\xbb\x4c\xe3"
    "\x1f\xf7\x0f\x05\x73\x08\x50\x4b\x07\x08\xbf\xfc\x1f\x45\x1e\x00"
    "\x00\x00\x12\x00\x00\x00\x50\x4b\x03\x04\x14\x00\x09\x00\x08\x00"
    "\x5c\x64\x21\x5c\x29\x5c\xd4\xd2\x2d\x00\x00\x00\x00\x03\x00\x00"
    "\x0c\x00\x00\x00\x64\x65\x66\x6c\x61\x74\x65\x64\x2e\x74\x78\x74"
    "\x78\x88\x2c\x8b\xa7\x55\xfc\x95\xf5\xa1\x8b\x8f\x59\x2f\x42\x7a"
    "\x96\x7d\xa1\xa7\xbf\x08\xe1\xa5\xee\xa4\x2f\x84\xe6\x41\xbc\x0e"
    "\xc3\x01\xb1\x2b\x8c\x2e\x87\xe9\xee\xbe\x60\x50\x10\x50\x4b\x07"
    "\x08\x29\x5c\xd4\xd2\x2d\x00\x00\x00\x00\x03\x00\x00\x50\x4b\x03"
    "\x04\x0a\x00\x00\x00\x00\x00\x5c\x64\x21\x5c\x30\x48\xd8\x9b\x0b"
    "\x00\x00\x00\x0b\x00\x00\x00\x09\x00\x00\x00\x70\x6c\x61\x69\x6e"
    "\x2e\x74\x78\x74\x6e\x6f\x20\x70\x61\x73\x73\x77\x6f\x72\x64\x50"
    "\x4b\x01\x02\x1e\x03\x0a\x00\x09\x00\x00\x00\x5c\x64\x21\x5c\xbf"
    "\xfc\x1f\x45\x1e\x00\x00\x00\x12\x00\x00\x00\x0a\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\xa4\x81\x00\x00\x00\x00\x73\x74\x6f"
    "\x72\x65\x64\x2e\x74\x78\x74\x50\x4b\x01\x02\x1e\x03\x14\x00\x09"
    "\x00\x08\x00\x5c\x64\x21\x5c\x29\x5c\xd4\xd2\x2d\x00\x00\x00\x00"
    "\x03\x00\x00\x0c\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\xa4"
    "\x81\x56\x00\x00\x00\x64\x65\x66\x6c\x61\x74\x65\x64\x2e\x74\x78"
    "\x74\x50\x4b\x01\x02\x1e\x03\x0a\x00\x00\x00\x00\x00\x5c\x64\x21"
    "\x5c\x30\x48\xd8\x9b\x0b\x00\x00\x00\x0b\x00\x00\x00\x09\x00\x00"
    "\x00\x00\x00\x00\x00\x01\x00\x00\x00\xa4\x81\xbd\x00\x00\x00\x70"
    "\x6c\x61\x69\x6e\x2e\x74\x78\x74\x50\x4b\x05\x06\x00\x00\x00\x00"
    "\x03\x00\x03\x00\xa9\x00\x00\x00\xef\x00\x00\x00\x00\x00";

static void info_zip() {
    std::string archive(info_zip_archive, sizeof info_zip_archive - 1), repeated;
    for (int i = 0; i < 32; ++i) repeated += "kubazip reads Info-ZIP. ";
    ssize_t result = 0;
    zip_t *zip = zip_stream_open_with_password(archive.data(), archive.size(), 0, 'r', "s3cret");
    check(zip != nullptr && read_entry(zip, "stored.txt", &result) == "stored by Info-ZIP" &&
              read_entry(zip, "deflated.txt", &result) == repeated,
          "Info-ZIP's encrypted entries decrypt");
    if (zip != nullptr) zip_stream_close(zip);
    zip = zip_stream_open(archive.data(), archive.size(), 0, 'r');
    check(zip != nullptr && zip_entries_total(zip) == 3 && read_entry(zip, "plain.txt", &result) == "no password",
          "its plain entry reads without a password");
    if (zip != nullptr) zip_stream_close(zip);
}

struct Sink {
    std::string data;
    bool contiguous = true;
    size_t calls = 0;
    size_t stop_after = 0;  // 0: accept everything
};

static size_t on_extract(void *arg, uint64_t offset, const void *data, size_t size) {
    Sink *sink = static_cast<Sink *>(arg);
    if (sink->stop_after != 0 && sink->calls == sink->stop_after) return 0;
    ++sink->calls;
    sink->contiguous = sink->contiguous && offset == sink->data.size();
    sink->data.append(static_cast<const char *>(data), size);
    return size;
}

static void entries(const std::string &big) {
    zip_t *zip = zip_stream_open(nullptr, 0, ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');
    check(zip != nullptr && add(zip, "a.txt", "alpha") && add(zip, "Notes.txt", "beta") && add(zip, "big.bin", big),
          "three entries are written");
    std::string archive = zip != nullptr ? copy_out(zip) : std::string();
    if (zip != nullptr) zip_stream_close(zip);
    zip = archive.empty() ? nullptr : zip_stream_open(archive.data(), archive.size(), 0, 'r');
    if (!check(zip != nullptr, "the archive opens for reading")) return;

    Sink sink;
    check(zip_entry_open(zip, "big.bin") == 0 && zip_entry_extract(zip, on_extract, &sink) == 0,
          "an entry extracts through a callback");
    check(sink.data == big && sink.contiguous && sink.calls > 1, "the callback gets every byte in order, in chunks");
    Sink partial;
    partial.stop_after = 1;
    check(zip_entry_extract(zip, on_extract, &partial) < 0, "a callback that stops early fails the extraction");
    zip_entry_close(zip);

    check(zip_entry_open(zip, "notes.TXT") == 0 && zip_entry_index(zip) == 1, "zip_entry_open ignores case when reading");
    zip_entry_close(zip);
    check(zip_entry_opencasesensitive(zip, "notes.TXT") == ZIP_ENOENT, "zip_entry_opencasesensitive does not");
    check(zip_entry_opencasesensitive(zip, "Notes.txt") == 0 && zip_entry_index(zip) == 1,
          "it finds the exact name at index 1");
    zip_entry_close(zip);
    zip_stream_close(zip);

    zip = zip_stream_open(archive.data(), archive.size(), 0, 'd');
    if (!check(zip != nullptr, "the archive opens for deletion")) return;
    size_t doomed[] = {0, 2};
    check(zip_entries_deletebyindex(zip, doomed, 2) == 2, "entries 0 and 2 are deleted by index");
    std::string pruned = copy_out(zip);
    zip_stream_close(zip);
    zip = pruned.empty() ? nullptr : zip_stream_open(pruned.data(), pruned.size(), 0, 'r');
    if (!check(zip != nullptr, "the pruned archive opens")) return;
    ssize_t result = 0;
    check(zip_entries_total(zip) == 1 && read_entry(zip, "Notes.txt", &result) == "beta" &&
              zip_entry_open(zip, "a.txt") == ZIP_ENOENT && zip_entry_open(zip, "big.bin") == ZIP_ENOENT,
          "only the middle entry is left");
    zip_stream_close(zip);
}

int main() {
    std::string big;
    for (size_t i = 0; i < 200000; ++i) big += static_cast<char>((i * 2654435761u) >> 24);
    password(0, "attack at dawn, bring snacks");
    password(ZIP_DEFAULT_COMPRESSION_LEVEL, big);
    entries(big);
    info_zip();
    if (!ok) return 1;
    std::puts("kubazip (C++): ok");
    return 0;
}
