// The C++ consumer: jansson.h from C++ with the reference counting
// jansson_config.h selects, hash seeding raced by threads, concurrent
// read-only encoding, concurrent reference counting in this code and in
// the library where it is thread-safe, iteration macros, recursive
// updates, keys and strings holding NUL bytes, and unpack errors.
#include <jansson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// Compilers defining __GNUC__ (GCC, and Clang unless it targets MSVC) count
// references atomically; cl, which has no __atomic or __sync builtins, and
// clang-cl, which may be paired with it, do not.
#if defined(__GNUC__)
#ifndef JANSSON_THREAD_SAFE_REFCOUNT
#error "GCC and Clang count references atomically"
#endif
#elif defined(JANSSON_THREAD_SAFE_REFCOUNT)
#error "compilers without __GNUC__ count references without atomics"
#endif

namespace {

bool ok = true;

void check(bool pass, const char* what) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

struct Ref {
    explicit Ref(json_t* json) : json(json) {}
    ~Ref() { json_decref(json); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    json_t* json;
};

std::string dumps(const json_t* json, size_t flags) {
    char* text = json_dumps(json, flags);
    std::string result = text ? text : "(null)";
    std::free(text);
    return result;
}

const int threads = 4;

// The first objects of the process, made on four threads at once, race to
// seed the hash function.
void concurrent_seeding() {
    bool built[threads] = {false, false, false, false};
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&built, t] {
            Ref object(json_object());
            bool good = object.json != nullptr;
            for (int i = 0; i < 200 && good; i++) {
                const std::string key = "key" + std::to_string(i);
                good = json_object_set_new(object.json, key.c_str(), json_integer(i * t)) == 0;
            }
            good = good && json_object_size(object.json) == 200 &&
                   json_integer_value(json_object_get(object.json, "key150")) == 150 * t;
            built[t] = good;
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    for (int t = 0; t < threads; t++) {
        check(built[t], "objects built while seeding concurrently work");
    }
}

void shared_document() {
    json_error_t error;
    Ref doc(json_loads("{\"b\": [1, 2, {\"c\": null}], \"a\": \"text\"}", 0, &error));
    const std::string expected = "{\"a\":\"text\",\"b\":[1,2,{\"c\":null}]}";
    bool same[threads] = {false, false, false, false};
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&doc, &expected, &same, t] {
            bool all = true;
            for (int i = 0; i < 2000; i++) {
#ifdef JANSSON_THREAD_SAFE_REFCOUNT
                json_incref(doc.json);
#endif
                all = dumps(doc.json, JSON_COMPACT | JSON_SORT_KEYS) == expected && all;
#ifdef JANSSON_THREAD_SAFE_REFCOUNT
                json_decref(doc.json);
#endif
            }
            same[t] = all;
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    for (int t = 0; t < threads; t++) {
        check(same[t], "concurrent encoding of one document agrees");
    }
    check(doc.json && doc.json->refcount == 1, "concurrent reference counting balances");
}

// Shallow copies made and released on four threads at once take and drop
// references to the shared elements inside the library (json_array_copy,
// array_delete), so the library's own counting must be atomic too.
void library_side_references() {
#ifdef JANSSON_THREAD_SAFE_REFCOUNT
    Ref shared(json_pack("[s, s, s, i]", "a", "b", "c", 4));
    bool copied[threads] = {false, false, false, false};
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; t++) {
        workers.emplace_back([&shared, &copied, t] {
            bool all = true;
            for (int i = 0; i < 20000; i++) {
                json_t* copy = json_copy(shared.json);
                all = copy != nullptr && json_array_size(copy) == 4 && all;
                json_decref(copy);
            }
            copied[t] = all;
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    bool balanced = true;
    for (size_t i = 0; i < json_array_size(shared.json); i++) {
        balanced = json_array_get(shared.json, i)->refcount == 1 && balanced;
    }
    for (int t = 0; t < threads; t++) {
        check(copied[t], "concurrent shallow copies");
    }
    check(balanced, "the library's reference counting balances under concurrency");
#endif
}

void iterate_and_update() {
    json_error_t error;
    Ref base(json_loads("{\"name\": \"base\", \"opts\": {\"x\": 1, \"y\": 2}, \"n\": [1]}", 0,
                        &error));
    Ref patch(json_loads("{\"opts\": {\"y\": 20, \"z\": 30}, \"n\": [2], \"new\": true}", 0,
                         &error));

    std::string keys;
    const char* key;
    json_t* value;
    json_object_foreach(base.json, key, value) {
        keys += key;
        keys += ',';
    }
    check(keys == "name,opts,n,", "json_object_foreach walks members in insertion order");

    check(json_object_update_recursive(base.json, patch.json) == 0, "recursive update");
    check(dumps(base.json, JSON_COMPACT) ==
              "{\"name\":\"base\",\"opts\":{\"x\":1,\"y\":20,\"z\":30},\"n\":[2],\"new\":true}",
          "nested objects merge; other values are replaced");

    long long sum = 0;
    size_t index;
    json_t* item;
    Ref numbers(json_pack("[i, i, i, I]", 1, 2, 3, (json_int_t)4000000000LL));
    json_array_foreach(numbers.json, index, item) {
        sum += json_integer_value(item) * static_cast<long long>(index + 1);
    }
    check(sum == 1 + 4 + 9 + 16000000000LL, "json_array_foreach");
}

void nul_bytes() {
    json_error_t error;
    check(json_loads("[\"a\\u0000b\"]", 0, &error) == nullptr,
          "a NUL escape is rejected without JSON_ALLOW_NUL");
    Ref array(json_loads("[\"a\\u0000b\"]", JSON_ALLOW_NUL, &error));
    json_t* text = json_array_get(array.json, 0);
    check(text && json_string_length(text) == 3 &&
              std::memcmp(json_string_value(text), "a\0b", 3) == 0,
          "JSON_ALLOW_NUL keeps it");

    Ref object(json_object());
    json_object_setn_new(object.json, "k\0one", 5, json_integer(1));
    json_object_setn_new(object.json, "k\0two", 5, json_integer(2));
    check(json_object_size(object.json) == 2 &&
              json_integer_value(json_object_getn(object.json, "k\0two", 5)) == 2 &&
              json_object_get(object.json, "k") == nullptr,
          "keys with NUL bytes are distinct");
    check(dumps(object.json, JSON_COMPACT) == "{\"k\\u0000one\":1,\"k\\u0000two\":2}",
          "and encode as escapes");
}

void unpack_errors() {
    json_error_t error;
    Ref value(json_pack("{s:s, s:i}", "name", "x", "count", 3));
    int count = 0;
    const char* name = nullptr;
    const int unpacked =
        json_unpack_ex(value.json, &error, 0, "{s:i, s:s}", "count", &count, "name", &name);
    check(unpacked == 0 && count == 3 && std::strcmp(name, "x") == 0, "json_unpack_ex");
    check(json_unpack_ex(value.json, &error, 0, "{s:s}", "count", &name) == -1 &&
              json_error_code(&error) == json_error_wrong_type,
          "a type mismatch is reported");
    check(json_unpack_ex(value.json, &error, 0, "{s:i}", "missing", &count) == -1 &&
              json_error_code(&error) == json_error_item_not_found,
          "a missing key is reported");
    check(json_unpack(value.json, "{s?i}", "missing", &count) == 0 && count == 3,
          "an optional key may be missing");
}

}  // namespace

int main() {
    concurrent_seeding();
    shared_document();
    library_side_references();
    iterate_and_update();
    nul_bytes();
    unpack_errors();
    std::printf("jansson (C++): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
