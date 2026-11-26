// The C++ side of tests/main.c: the same UDF 1.02 image, built in a
// std::vector and read through a custom block input and udfread_open().
#include <udfread/blockinput.h>
#include <udfread/udfread.h>
extern "C" {
#include <udfread/udfread-version.h>
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

const std::size_t kBlock = 2048;
const std::uint32_t kPartStart = 257;  // FSD, TD, root FE, file FE, 2 data blocks
const std::uint32_t kPartLen = 6;
const std::uint32_t kSectors = kPartStart + kPartLen + 1;
const std::size_t kFileSize = 2500;

int failures = 0;

bool check(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
    return ok;
}

void put16(unsigned char* p, unsigned long v) {
    p[0] = static_cast<unsigned char>(v);
    p[1] = static_cast<unsigned char>(v >> 8);
}

void put32(unsigned char* p, unsigned long v) {
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

// CRC-ITU-T of ECMA-167 3/7.2.6.
unsigned long crc16(const unsigned char* p, std::size_t n) {
    unsigned long crc = 0;
    while (n--) {
        crc ^= static_cast<unsigned long>(*p++) << 8;
        for (int i = 0; i < 8; ++i) crc = (crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1) & 0xffff;
    }
    return crc;
}

// Descriptor tag (3/7.2) of a filled-in descriptor of len bytes.
void tag(unsigned char* d, unsigned id, unsigned long loc, std::size_t len) {
    put16(d, id);
    put16(d + 2, 2);
    put16(d + 8, crc16(d + 16, len - 16));
    put16(d + 10, len - 16);
    put32(d + 12, loc);
    unsigned sum = 0;
    for (int i = 0; i < 16; ++i)
        if (i != 4) sum += d[i];
    d[4] = static_cast<unsigned char>(sum);
}

void entity(unsigned char* p, const std::string& id) { std::memcpy(p + 1, id.data(), id.size()); }

void charspec(unsigned char* p) { std::memcpy(p + 1, "OSTA Compressed Unicode", 23); }

// 8-bit CS0 dstring of a field_len-byte field.
void dstring(unsigned char* p, std::size_t field_len, const std::string& s) {
    p[0] = 8;
    std::memcpy(p + 1, s.data(), s.size());
    p[field_len - 1] = static_cast<unsigned char>(s.size() + 1);
}

void long_ad(unsigned char* p, unsigned long len, unsigned long lba) {
    put32(p, len);
    put32(p + 4, lba);
    put16(p + 8, 0);
}

const std::string kVolumeId = "Caf\xe9 UDF";  // Latin-1 e-acute

unsigned char content(std::size_t i) { return static_cast<unsigned char>(i % 251); }

class Image {
public:
    Image() : bytes_(kSectors * kBlock) {
        static const char* const vrs[] = {"BEA01", "NSR02", "TEA01"};
        for (int i = 0; i < 3; ++i) {
            std::memcpy(sector(16 + i) + 1, vrs[i], 5);
            sector(16 + i)[6] = 1;
        }
        volume_descriptors(32);
        volume_descriptors(48);
        anchor(256);
        anchor(kSectors - 1);

        unsigned char* d = sector(kPartStart);  // File Set Descriptor, block 0
        put16(d + 28, 3);
        put16(d + 30, 3);
        put32(d + 32, 1);
        put32(d + 36, 1);
        charspec(d + 48);
        dstring(d + 112, 128, "cabin");
        charspec(d + 240);
        dstring(d + 304, 32, "cabin");
        long_ad(d + 400, kBlock, 2);  // root directory ICB
        entity(d + 416, "*OSTA UDF Compliant");
        put16(d + 440, 0x0102);
        tag(d, 256, 0, 512);
        tag(sector(kPartStart + 1), 8, 1, 512);

        // Root directory: parent entry and hello.txt, inline in its File Entry.
        d = sector(kPartStart + 2);
        std::size_t dir_len = fid(d + 176, 2, 0x0a, "", 2);
        dir_len += fid(d + 176 + dir_len, 2, 0, "hello.txt", 3);
        file_entry(2, 4, 3, dir_len, dir_len);
        tag(d, 261, 2, 176 + dir_len);

        // hello.txt: one short_ad extent over blocks 4 and 5.
        d = file_entry(3, 5, 0, kFileSize, 8);
        put32(d + 176, kFileSize);
        put32(d + 180, 4);
        tag(d, 261, 3, 184);
        for (std::size_t i = 0; i < kFileSize; ++i) sector(kPartStart + 4)[i] = content(i);
    }

    unsigned char* sector(unsigned long lba) { return &bytes_[lba * kBlock]; }
    const std::vector<unsigned char>& bytes() const { return bytes_; }

private:
    void volume_descriptors(unsigned long base) {
        unsigned char* d = sector(base);  // Primary Volume Descriptor
        put32(d + 16, 1);
        dstring(d + 24, 32, kVolumeId);
        put16(d + 56, 1);
        put16(d + 58, 1);
        put16(d + 60, 2);
        put16(d + 62, 2);
        put32(d + 64, 1);
        put32(d + 68, 1);
        dstring(d + 72, 128, "0123456789ABCDEFcabin-ports");
        charspec(d + 200);
        charspec(d + 264);
        tag(d, 1, base, 512);

        d = sector(base + 1);  // Partition Descriptor, partition 0
        put32(d + 16, 2);
        put16(d + 20, 1);
        entity(d + 24, "+NSR02");
        put32(d + 184, 1);
        put32(d + 188, kPartStart);
        put32(d + 192, kPartLen);
        tag(d, 5, base + 1, 512);

        d = sector(base + 2);  // Logical Volume Descriptor
        put32(d + 16, 3);
        charspec(d + 20);
        dstring(d + 84, 128, "cabin");
        put32(d + 212, kBlock);
        entity(d + 216, "*OSTA UDF Compliant");
        put16(d + 240, 0x0102);
        long_ad(d + 248, kBlock, 0);  // File Set Descriptor
        put32(d + 264, 6);
        put32(d + 268, 1);
        d[440] = 1;  // type 1 partition map onto partition 0
        d[441] = 6;
        put16(d + 442, 1);
        tag(d, 6, base + 2, 446);

        tag(sector(base + 3), 8, base + 3, 512);  // Terminating
    }

    void anchor(unsigned long lba) {
        unsigned char* d = sector(lba);
        put32(d + 16, 4 * kBlock);
        put32(d + 20, 32);
        put32(d + 24, 4 * kBlock);
        put32(d + 28, 48);
        tag(d, 2, lba, 512);
    }

    unsigned char* file_entry(unsigned long block, unsigned type, unsigned ad_flags,
                              unsigned long size, unsigned long l_ad) {
        unsigned char* d = sector(kPartStart + block);
        put16(d + 20, 4);  // ICB strategy 4
        put16(d + 24, 1);
        d[27] = static_cast<unsigned char>(type);
        put16(d + 34, ad_flags);
        put16(d + 48, 1);
        put32(d + 56, size);
        put32(d + 64, ad_flags == 3 ? 0 : (size + kBlock - 1) / kBlock);
        entity(d + 128, "*cabin-ports");
        put32(d + 160, block);
        put32(d + 172, l_ad);
        return d;
    }

    static std::size_t fid(unsigned char* p, unsigned long block, unsigned flags,
                           const std::string& name, unsigned long icb) {
        std::size_t l_fi = name.empty() ? 0 : name.size() + 1;
        std::size_t len = (38 + l_fi + 3) / 4 * 4;
        put16(p + 16, 1);
        p[18] = static_cast<unsigned char>(flags);
        p[19] = static_cast<unsigned char>(l_fi);
        long_ad(p + 20, kBlock, icb);
        if (l_fi) {
            p[38] = 8;  // 8-bit CS0
            std::memcpy(p + 39, name.data(), name.size());
        }
        tag(p, 257, block, len);
        return len;
    }

    std::vector<unsigned char> bytes_;
};

struct MemoryInput {
    udfread_block_input input;  // first, so the callbacks can cast back
    const unsigned char* img;
    std::uint32_t blocks;
    int closed;

    MemoryInput(const unsigned char* image, std::uint32_t n) : img(image), blocks(n), closed(0) {
        input.close = &MemoryInput::close;
        input.read = &MemoryInput::read;
        input.size = &MemoryInput::size;
    }

    static int read(udfread_block_input* in, std::uint32_t lba, void* buf, std::uint32_t n, int) {
        MemoryInput* m = reinterpret_cast<MemoryInput*>(in);
        if (lba >= m->blocks) return -1;
        if (n > m->blocks - lba) n = m->blocks - lba;
        std::memcpy(buf, m->img + lba * kBlock, n * kBlock);
        return static_cast<int>(n);
    }
    static std::uint32_t size(udfread_block_input* in) {
        return reinterpret_cast<MemoryInput*>(in)->blocks;
    }
    static int close(udfread_block_input* in) {
        ++reinterpret_cast<MemoryInput*>(in)->closed;
        return 0;
    }
};

bool bytes_match(const unsigned char* buf, std::size_t from, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if (buf[i] != content(from + i)) return false;
    return true;
}

void read_volume(udfread* udf, const std::vector<unsigned char>& img) {
    const char* id = udfread_get_volume_id(udf);
    check(id != NULL && std::string(id) == "Caf\xc3\xa9 UDF", "volume id in MUTF-8");
    unsigned char set_id[128];
    check(udfread_get_volume_set_id(udf, set_id, sizeof set_id) == 128, "volume set id size");
    check(std::memcmp(set_id, &img[32 * kBlock + 72], 128) == 0, "volume set id bytes");

    if (UDFDIR* dir = udfread_opendir(udf, "/")) {
        udfread_dirent entry;
        check(udfread_readdir(dir, &entry) == &entry && std::string(entry.d_name) == "hello.txt" &&
                  entry.d_type == UDF_DT_REG,
              "readdir: hello.txt, a regular file");
        check(udfread_readdir(dir, &entry) == NULL, "readdir: one entry");
        udfread_rewinddir(dir);
        check(udfread_readdir(dir, &entry) != NULL && std::string(entry.d_name) == "hello.txt",
              "rewinddir");
        UDFFILE* f = udfread_file_openat(dir, "hello.txt");
        check(f != NULL && udfread_file_size(f) == static_cast<std::int64_t>(kFileSize), "file_openat");
        udfread_file_close(f);
        udfread_closedir(dir);
    } else {
        check(false, "opendir /");
    }

    check(udfread_file_open(udf, "/missing.txt") == NULL, "missing file");
    UDFFILE* f = udfread_file_open(udf, "/hello.txt");
    if (!check(f != NULL, "file_open /hello.txt")) return;
    std::vector<unsigned char> buf(2 * kBlock);
    check(udfread_file_size(f) == static_cast<std::int64_t>(kFileSize), "file size");
    check(udfread_file_lba(f, 0) == kPartStart + 4 && udfread_file_lba(f, 1) == kPartStart + 5,
          "file blocks map to the partition");
    check(udfread_read_blocks(f, &buf[0], 0, 2, 0) == 2 && bytes_match(&buf[0], 0, kFileSize),
          "read_blocks");
    check(udfread_file_read(f, &buf[0], buf.size()) == static_cast<ssize_t>(kFileSize) &&
              bytes_match(&buf[0], 0, kFileSize),
          "file_read: the whole file");
    check(udfread_file_tell(f) == static_cast<std::int64_t>(kFileSize) &&
              udfread_file_read(f, &buf[0], 1) == 0,
          "file_read: EOF");
    check(udfread_file_seek(f, 2040, UDF_SEEK_SET) == 2040 && udfread_file_read(f, &buf[0], 16) == 16 &&
              bytes_match(&buf[0], 2040, 16),
          "seek and read across the block boundary");
    check(udfread_file_seek(f, -4, UDF_SEEK_END) == static_cast<std::int64_t>(kFileSize - 4) &&
              udfread_file_read(f, &buf[0], 10) == 4 && bytes_match(&buf[0], kFileSize - 4, 4),
          "seek from the end and a short read");
    udfread_file_close(f);
}

void from_memory(const Image& image) {
    MemoryInput m(&image.bytes()[0], kSectors);
    udfread* udf = udfread_init();
    if (!check(udf != NULL, "udfread_init")) return;
    if (check(udfread_open_input(udf, &m.input) == 0, "open_input")) read_volume(udf, image.bytes());
    udfread_close(udf);
    check(m.closed == 1, "close calls the input's close once");
}

// `cabin run` keeps the caller's working directory, so the file goes in the
// probe's output directory.
void from_file(const Image& image) {
    const char* dir = std::getenv("CABIN_PROFILE_DIR");
    if (!check(dir != NULL, "CABIN_PROFILE_DIR is set")) return;
    const std::string path = std::string(dir) + "/port-probe-libudfread-cxx.iso";
    std::FILE* out = std::fopen(path.c_str(), "wb");
    if (!check(out != NULL, "create the image file")) return;
    check(std::fwrite(&image.bytes()[0], kBlock, kSectors, out) == kSectors, "write the image file");
    std::fclose(out);
    udfread* udf = udfread_init();
    if (check(udf != NULL && udfread_open(udf, path.c_str()) == 0, "udfread_open on the image file"))
        read_volume(udf, image.bytes());
    udfread_close(udf);
    std::remove(path.c_str());
}

void not_udf() {
    std::vector<unsigned char> zeros(kSectors * kBlock);
    MemoryInput m(&zeros[0], kSectors);
    udfread* udf = udfread_init();
    check(udf != NULL && udfread_open_input(udf, &m.input) < 0, "an all-zero image is refused");
    udfread_close(udf);
}

}  // namespace

int main() {
    int major = -1, minor = -1, micro = -1;
    udfread_get_version(&major, &minor, &micro);
    check(major == 1 && minor == 2 && micro == 0, "udfread_get_version");
    check(UDFREAD_VERSION == UDFREAD_VERSION_CODE(1, 2, 0) &&
              std::string(UDFREAD_VERSION_STRING) == "1.2.0",
          "version macros");

    Image image;
    from_memory(image);
    from_file(image);
    not_udf();
    if (failures) return 1;
    std::cout << "libudfread C++ consumer: OK\n";
    return 0;
}
