// The C++11 consumer: drives zip.c and unzip.c through its own in-memory
// I/O callbacks (zipOpen2_64 / unzOpen2_64), seeks with the position APIs,
// reads an entry raw and inflates it with zlib, and rebuilds a missing
// central directory with mztools' unzRepair.
#include <minizip/mztools.h>
#include <minizip/unzip.h>
#include <minizip/zip.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// One in-memory file, handed to the callbacks as `opaque`.
struct Memory {
    std::vector<unsigned char> bytes;
    ZPOS64_T pos;
};

}  // namespace

extern "C" {

static voidpf ZCALLBACK mem_open(voidpf opaque, const void*, int mode) {
    Memory* m = static_cast<Memory*>(opaque);
    if ((mode & ZLIB_FILEFUNC_MODE_CREATE) != 0) m->bytes.clear();
    m->pos = 0;
    return m;
}

static uLong ZCALLBACK mem_read(voidpf, voidpf stream, void* buf, uLong size) {
    Memory* m = static_cast<Memory*>(stream);
    const ZPOS64_T left = m->pos < m->bytes.size() ? m->bytes.size() - m->pos : 0;
    const uLong n = left < size ? static_cast<uLong>(left) : size;
    if (n != 0) std::memcpy(buf, &m->bytes[m->pos], n);
    m->pos += n;
    return n;
}

static uLong ZCALLBACK mem_write(voidpf, voidpf stream, const void* buf, uLong size) {
    Memory* m = static_cast<Memory*>(stream);
    if (m->pos + size > m->bytes.size()) m->bytes.resize(m->pos + size);
    if (size != 0) std::memcpy(&m->bytes[m->pos], buf, size);
    m->pos += size;
    return size;
}

static ZPOS64_T ZCALLBACK mem_tell(voidpf, voidpf stream) {
    return static_cast<Memory*>(stream)->pos;
}

// minizip passes backward offsets as wrapped unsigned values.
static long ZCALLBACK mem_seek(voidpf, voidpf stream, ZPOS64_T offset, int origin) {
    Memory* m = static_cast<Memory*>(stream);
    const ZPOS64_T base = origin == ZLIB_FILEFUNC_SEEK_SET   ? 0
                          : origin == ZLIB_FILEFUNC_SEEK_CUR ? m->pos
                                                             : m->bytes.size();
    m->pos = base + offset;
    return 0;
}

static int ZCALLBACK mem_close(voidpf, voidpf) { return 0; }

static int ZCALLBACK mem_error(voidpf, voidpf) { return 0; }

}  // extern "C"

namespace {

int fail(const char* what) {
    std::fprintf(stderr, "minizip C++ test failed: %s\n", what);
    return 1;
}

struct Entry {
    const char* name;
    int method;
    int level;
    std::string data;
};

std::vector<Entry> entries() {
    std::string text;
    while (text.size() < 300000) text += "Cabin consumes cabin-ports/minizip from C++11.\n";
    std::string noise(70000, '\0');
    unsigned long x = 2463534242UL;
    for (char& c : noise) {
        x ^= (x << 13) & 0xffffffffUL;
        x ^= x >> 17;
        x ^= (x << 5) & 0xffffffffUL;
        c = static_cast<char>(x);
    }
    return {{"text.txt", Z_DEFLATED, Z_BEST_COMPRESSION, text},
            {"noise.bin", Z_DEFLATED, Z_BEST_SPEED, noise},
            {"caf\xc3\xa9.txt", 0, 0, "stored, under a UTF-8 name\n"}};
}

// The local extra field written with every entry: header 0xcafe, 4 bytes.
const unsigned char extra[] = {0xfe, 0xca, 0x04, 0x00, 'c', 'a', 'b', 'i'};

bool write_archive(zipFile zf, const std::vector<Entry>& all) {
    for (const Entry& e : all) {
        zip_fileinfo zi = zip_fileinfo();
        zi.dosDate = 0x5c516000UL;  // 2026-02-17 12:00:00
        if (zipOpenNewFileInZip64(zf, e.name, &zi, extra, sizeof extra, nullptr, 0, nullptr,
                                  e.method, e.level, 0) != ZIP_OK ||
            zipWriteInFileInZip(zf, e.data.data(), static_cast<unsigned>(e.data.size())) != ZIP_OK ||
            zipCloseFileInZip(zf) != ZIP_OK) {
            return false;
        }
    }
    return zipClose(zf, nullptr) == ZIP_OK;
}

// Reads the current entry in 4 KiB chunks, checking unztell64 and unzeof
// along the way; an empty result means a failure, including the CRC check.
std::string read_current(unzFile uf) {
    std::string out;
    char buf[4096];
    if (unzOpenCurrentFile(uf) != UNZ_OK) return std::string();
    int n;
    while ((n = unzReadCurrentFile(uf, buf, sizeof buf)) > 0) {
        out.append(buf, n);
        if (unztell64(uf) != out.size()) n = -1;
        if (n < 0) break;
    }
    const bool at_end = unzeof(uf) == 1;
    return unzCloseCurrentFile(uf) == UNZ_OK && n == 0 && at_end ? out : std::string();
}

bool reads_back(unzFile uf, const std::vector<Entry>& all) {
    unz_global_info64 gi;
    if (unzGetGlobalInfo64(uf, &gi) != UNZ_OK || gi.number_entry != all.size()) return false;
    std::vector<unz64_file_pos> positions(all.size());
    std::vector<ZPOS64_T> offsets(all.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        unz_file_info64 fi;
        char name[64];
        if ((i == 0 ? unzGoToFirstFile(uf) : unzGoToNextFile(uf)) != UNZ_OK ||
            unzGetCurrentFileInfo64(uf, &fi, name, sizeof name, nullptr, 0, nullptr, 0) != UNZ_OK ||
            std::strcmp(name, all[i].name) != 0 ||
            fi.compression_method != static_cast<uLong>(all[i].method) ||
            fi.uncompressed_size != all[i].data.size() || fi.dosDate != 0x5c516000UL ||
            fi.crc != crc32(0L, reinterpret_cast<const Bytef*>(all[i].data.data()),
                            static_cast<uInt>(all[i].data.size())) ||
            unzGetFilePos64(uf, &positions[i]) != UNZ_OK) {
            return false;
        }
        offsets[i] = unzGetOffset64(uf);
    }
    if (unzGoToNextFile(uf) != UNZ_END_OF_LIST_OF_FILE) return false;

    // Backwards, by both kinds of position.
    for (std::size_t i = all.size(); i-- > 0;) {
        if (unzGoToFilePos64(uf, &positions[i]) != UNZ_OK || read_current(uf) != all[i].data ||
            unzSetOffset64(uf, offsets[i]) != UNZ_OK || read_current(uf) != all[i].data) {
            return false;
        }
    }

    // The UTF-8 name sets the language-encoding flag; the local extra field
    // comes back as written.
    unz_file_info64 fi;
    unsigned char local[16];
    if (unzLocateFile(uf, all[2].name, 1) != UNZ_OK ||
        unzGetCurrentFileInfo64(uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
        (fi.flag & (1 << 11)) == 0 || unzOpenCurrentFile(uf) != UNZ_OK ||
        unzGetLocalExtrafield(uf, local, sizeof local) != static_cast<int>(sizeof extra) ||
        std::memcmp(local, extra, sizeof extra) != 0 || unzCloseCurrentFile(uf) != UNZ_OK) {
        return false;
    }
    return true;
}

// noise.bin's deflate stream, read raw and inflated with zlib itself.
bool raw_entry_inflates(unzFile uf, const Entry& e) {
    unz_file_info64 fi;
    int method = 0;
    int level = 0;
    if (unzLocateFile(uf, e.name, 1) != UNZ_OK ||
        unzGetCurrentFileInfo64(uf, &fi, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
        unzOpenCurrentFile2(uf, &method, &level, 1) != UNZ_OK || method != Z_DEFLATED) {
        return false;
    }
    std::vector<unsigned char> raw(static_cast<std::size_t>(fi.compressed_size) + 1);
    const int n = unzReadCurrentFile(uf, raw.data(), static_cast<unsigned>(raw.size()));
    unzCloseCurrentFile(uf);
    if (n < 0 || static_cast<ZPOS64_T>(n) != fi.compressed_size) return false;

    std::string out(e.data.size() + 1, '\0');
    z_stream s = z_stream();
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return false;
    s.next_in = raw.data();
    s.avail_in = static_cast<uInt>(n);
    s.next_out = reinterpret_cast<Bytef*>(&out[0]);
    s.avail_out = static_cast<uInt>(out.size());
    const int ret = inflate(&s, Z_FINISH);
    out.resize(s.total_out);
    inflateEnd(&s);
    return ret == Z_STREAM_END && out == e.data;
}

bool write_file(const std::string& path, const unsigned char* data, std::size_t size) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(data, 1, size, f) == size;
    return std::fclose(f) == 0 && ok;
}

// Cuts the archive at its central directory, as an interrupted writer
// would leave it, and has unzRepair rebuild one from the local headers.
bool repairs_a_truncated_copy(const std::vector<unsigned char>& zip,
                              const std::vector<Entry>& all) {
    // Among the probe's own build outputs: `cabin run` keeps the caller's
    // working directory.
    const char* const dir = std::getenv("CABIN_PROFILE_DIR");
    if (dir == nullptr || zip.size() < 22) return false;
    const std::string base = std::string(dir) + "/port-probe-minizip-cc";
    const std::string broken = base + "-broken.zip", fixed = base + "-fixed.zip",
                      tmp = base + "-tmp.zip";
    // The end-of-central-directory record (no archive comment) holds the
    // directory's offset at byte 16.
    const unsigned char* eocd = &zip[zip.size() - 22];
    const std::size_t cd = eocd[16] | eocd[17] << 8 | eocd[18] << 16 |
                           static_cast<std::size_t>(eocd[19]) << 24;
    if (eocd[0] != 'P' || eocd[1] != 'K' || eocd[2] != 5 || eocd[3] != 6 || cd >= zip.size() ||
        !write_file(broken, zip.data(), cd)) {
        return false;
    }
    bool ok = unzOpen64(broken.c_str()) == nullptr;  // nothing to find a directory in
    uLong recovered = 0;
    uLong bytes = 0;
    ok = ok && unzRepair(broken.c_str(), fixed.c_str(), tmp.c_str(), &recovered, &bytes) == Z_OK &&
         recovered == all.size();
    if (ok) {
        unzFile uf = unzOpen64(fixed.c_str());
        ok = uf != nullptr;
        for (std::size_t i = 0; ok && i < all.size(); ++i) {
            ok = unzLocateFile(uf, all[i].name, 1) == UNZ_OK && read_current(uf) == all[i].data;
        }
        if (uf != nullptr) unzClose(uf);
    }
    std::remove(broken.c_str());
    std::remove(fixed.c_str());
    std::remove(tmp.c_str());
    return ok;
}

}  // namespace

int main() {
    const std::vector<Entry> all = entries();
    Memory memory = Memory();
    zlib_filefunc64_def funcs = {mem_open, mem_read,  mem_write, mem_tell,
                                 mem_seek, mem_close, mem_error, &memory};

    zipFile zf = zipOpen2_64("memory", APPEND_STATUS_CREATE, nullptr, &funcs);
    if (zf == nullptr || !write_archive(zf, all)) return fail("writing through the callbacks");
    if (memory.bytes.size() >= all[0].data.size() / 10 + all[1].data.size() + 1000) {
        return fail("text.txt was not compressed");
    }

    unzFile uf = unzOpen2_64("memory", &funcs);
    if (uf == nullptr) return fail("unzOpen2_64");
    const bool read_ok = reads_back(uf, all);
    const bool raw_ok = raw_entry_inflates(uf, all[1]);
    unzClose(uf);
    if (!read_ok) return fail("reading through the callbacks");
    if (!raw_ok) return fail("raw read of the deflate stream");
    if (!repairs_a_truncated_copy(memory.bytes, all)) return fail("unzRepair");
    return 0;
}
