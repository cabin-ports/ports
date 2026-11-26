#include <re.h>
#include <re_sha.h>

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                     \
	do {                                                            \
		if (!(cond)) {                                          \
			fprintf(stderr, "%s:%d: check failed: %s\n",   \
				__FILE__, __LINE__, #cond);             \
			return 1;                                       \
		}                                                       \
	} while (0)

static int hex_equals(const uint8_t *buf, size_t len, const char *hex)
{
	char str[2 * 64 + 1];

	if (re_snprintf(str, sizeof(str), "%w", buf, len) < 0)
		return 0;
	return strcmp(str, hex) == 0;
}

static int worker(void *arg)
{
	int *value = arg;

	*value = 42;
	return 7;
}

static int check_platform(void)
{
	const char *arch = sys_arch_get();

	CHECK(strcmp(sys_libre_version_get(), "4.13.0") == 0);
#if defined(_WIN32)
	CHECK(strcmp(sys_os_get(), "Windows") == 0);
	CHECK(poll_method_best() == METHOD_SELECT);
#if defined(_M_X64)
	CHECK(strcmp(arch, "AMD64") == 0);
#elif defined(_M_ARM64)
	CHECK(strcmp(arch, "ARM64") == 0);
#endif
#elif defined(__APPLE__)
	CHECK(strcmp(sys_os_get(), "Darwin") == 0);
	CHECK(poll_method_best() == METHOD_KQUEUE);
#if defined(__x86_64__)
	CHECK(strcmp(arch, "x86_64") == 0);
#elif defined(__aarch64__)
	CHECK(strcmp(arch, "arm64") == 0);
#endif
#elif defined(__linux__)
	CHECK(strcmp(sys_os_get(), "Linux") == 0);
	CHECK(poll_method_best() == METHOD_EPOLL);
#if defined(__x86_64__)
	CHECK(strcmp(arch, "x86_64") == 0);
#elif defined(__aarch64__)
	CHECK(strcmp(arch, "aarch64") == 0);
#endif
#endif
	(void)arch;
	return 0;
}

static int check_codecs(void)
{
	const uint8_t check[] = "123456789";
	char b64[16];
	uint8_t raw[16];
	size_t len = sizeof(b64);

	/* zlib's crc32() behind re_crc32 */
	CHECK(re_crc32(0, check, 9) == 0xcbf43926u);

	CHECK(base64_encode((const uint8_t *)"hello", 5, b64, &len) == 0);
	CHECK(len == 8 && memcmp(b64, "aGVsbG8=", 8) == 0);
	len = sizeof(raw);
	CHECK(base64_decode(b64, 8, raw, &len) == 0);
	CHECK(len == 5 && memcmp(raw, "hello", 5) == 0);
	return 0;
}

static int check_addresses(void)
{
	struct sa sa;
	char str[64];

	CHECK(re_snprintf(str, sizeof(str), "%u-%s", 42u, "ok") == 5);
	CHECK(strcmp(str, "42-ok") == 0);

	CHECK(sa_set_str(&sa, "127.0.0.1", 5060) == 0);
	CHECK(sa_af(&sa) == AF_INET && sa_port(&sa) == 5060);
	CHECK(sa_is_loopback(&sa));
	CHECK(re_snprintf(str, sizeof(str), "%J", &sa) > 0);
	CHECK(strcmp(str, "127.0.0.1:5060") == 0);

	CHECK(sa_set_str(&sa, "2001:db8::1", 443) == 0);
	CHECK(sa_af(&sa) == AF_INET6);
	CHECK(re_snprintf(str, sizeof(str), "%J", &sa) > 0);
	CHECK(strcmp(str, "[2001:db8::1]:443") == 0);
	return 0;
}

static int check_crypto(void)
{
	/* RFC 4231, test case 2 */
	static const char data[] = "what do ya want for nothing?";
	/* NIST SP 800-38A, F.5.1 (CTR-AES128.Encrypt), first block */
	static const uint8_t key[16] = {
		0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
		0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
	static const uint8_t ctr[16] = {
		0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
		0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
	static const uint8_t plain[16] = {
		0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
		0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
	uint8_t md[SHA256_DIGEST_SIZE];
	uint8_t out[16];
	struct hmac *hmac = NULL;
	struct aes *aes = NULL;
	struct tls *tls = NULL;

	sha1((const uint8_t *)"abc", 3, md);
	CHECK(hex_equals(md, SHA1_DIGEST_SIZE,
			 "a9993e364706816aba3e25717850c26c9cd0d89d"));

	hmac_sha1((const uint8_t *)"Jefe", 4, (const uint8_t *)data,
		  strlen(data), md, SHA1_DIGEST_SIZE);
	CHECK(hex_equals(md, SHA1_DIGEST_SIZE,
			 "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));

	CHECK(hmac_create(&hmac, HMAC_HASH_SHA256, (const uint8_t *)"Jefe",
			  4) == 0);
	CHECK(hmac_digest(hmac, md, sizeof(md), (const uint8_t *)data,
			  strlen(data)) == 0);
	hmac = mem_deref(hmac);
	CHECK(hex_equals(md, sizeof(md),
			 "5bdcc146bf60754e6a042426089575c7"
			 "5a003f089d2739839dec58b964ec3843"));

	CHECK(aes_alloc(&aes, AES_MODE_CTR, key, 128, ctr) == 0);
	CHECK(aes_encr(aes, out, plain, sizeof(plain)) == 0);
	aes = mem_deref(aes);
	CHECK(hex_equals(out, sizeof(out),
			 "874d6191b620e3261bef6864990db6ce"));

	/* OpenSSL's TLS and DTLS contexts */
	CHECK(tls_alloc(&tls, TLS_METHOD_TLS, NULL, NULL) == 0);
	tls = mem_deref(tls);
	CHECK(tls_alloc(&tls, TLS_METHOD_DTLS, NULL, NULL) == 0);
	tls = mem_deref(tls);
	return 0;
}

static int check_threads(void)
{
	thrd_t thr;
	mtx_t *mtx = NULL;
	int value = 0;
	int res = 0;

	CHECK(mutex_alloc(&mtx) == 0);
	CHECK(mtx_lock(mtx) == thrd_success);
	CHECK(mtx_unlock(mtx) == thrd_success);
	mtx = mem_deref(mtx);

	CHECK(thread_create_name(&thr, "worker", worker, &value) == 0);
	CHECK(thrd_join(thr, &res) == thrd_success);
	CHECK(res == 7 && value == 42);
	return 0;
}

int main(void)
{
	int err;

	CHECK(libre_init() == 0);

	err = check_platform() || check_codecs() || check_addresses() ||
	      check_crypto() || check_threads();

	libre_close();
	if (err)
		return 1;

	puts("baresip-libre C consumer: ok");
	return 0;
}
