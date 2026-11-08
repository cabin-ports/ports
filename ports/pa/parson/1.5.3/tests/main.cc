// The C++ consumer, in C++98.  parson's allocations go through counting
// functions installed before any other call.  A parsed document is walked
// into a key-sorted std::string.  A document built from a std::map is
// serialized with unescaped slashes and a custom number format, then read
// back.  At the end no allocation is left live.
#include <parson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, int line, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(expr) check((expr), __LINE__, #expr)

std::set<void*> live;
std::size_t allocations = 0;
std::size_t unknown_frees = 0;

std::string canonical(const JSON_Value* value) {
  std::ostringstream number;
  std::string out;
  switch (json_value_get_type(value)) {
    case JSONObject: {
      const JSON_Object* object = json_value_get_object(value);
      std::map<std::string, std::string> fields;
      for (std::size_t i = 0; i < json_object_get_count(object); ++i) {
        fields[json_object_get_name(object, i)] = canonical(json_object_get_value_at(object, i));
      }
      out = "{";
      for (std::map<std::string, std::string>::const_iterator it = fields.begin();
           it != fields.end(); ++it) {
        out += (it == fields.begin() ? "" : ",") + it->first + "=" + it->second;
      }
      return out + "}";
    }
    case JSONArray: {
      const JSON_Array* array = json_value_get_array(value);
      out = "[";
      for (std::size_t i = 0; i < json_array_get_count(array); ++i) {
        out += (i == 0 ? "" : ",") + canonical(json_array_get_value(array, i));
      }
      return out + "]";
    }
    case JSONString:
      return "'" + std::string(json_value_get_string(value), json_value_get_string_len(value)) +
             "'";
    case JSONNumber:
      number << json_value_get_number(value);
      return number.str();
    case JSONBoolean:
      return json_value_get_boolean(value) ? "true" : "false";
    case JSONNull:
      return "null";
    default:
      return "error";
  }
}

}  // namespace

// parson's function pointer types have C language linkage.
extern "C" {

static void* counting_malloc(std::size_t size) {
  void* pointer = std::malloc(size);
  if (pointer != NULL) {
    live.insert(pointer);
    ++allocations;
  }
  return pointer;
}

static void counting_free(void* pointer) {
  if (pointer != NULL && live.erase(pointer) == 0) {
    ++unknown_frees;
  }
  std::free(pointer);
}

static int two_decimals(double number, char* buf) {
  std::ostringstream text;
  text << std::fixed << std::setprecision(2) << number;
  const std::string written = text.str();
  if (buf != NULL) {
    std::memcpy(buf, written.c_str(), written.size() + 1);
  }
  return static_cast<int>(written.size());
}
}

int main() {
  json_set_allocation_functions(counting_malloc, counting_free);

  JSON_Value* parsed = json_parse_string(
      "{\"zeta\": [3, {\"b\": false, \"a\": null}], \"alpha\": \"x\\/y\", \"mid\": 2.5}");
  CHECK(canonical(parsed) == "{alpha='x/y',mid=2.5,zeta=[3,{a=null,b=false}]}");
  json_value_free(parsed);

  std::map<std::string, std::vector<double> > scores;
  scores["ann"].push_back(1);
  scores["ann"].push_back(2.5);
  scores["bob/c"];
  JSON_Value* root = json_value_init_object();
  for (std::map<std::string, std::vector<double> >::const_iterator it = scores.begin();
       it != scores.end(); ++it) {
    JSON_Value* array = json_value_init_array();
    for (std::size_t i = 0; i < it->second.size(); ++i) {
      json_array_append_number(json_array(array), it->second[i]);
    }
    CHECK(json_object_set_value(json_object(root), it->first.c_str(), array) == JSONSuccess);
  }

  json_set_escape_slashes(0);
  json_set_number_serialization_function(two_decimals);
  char* text = json_serialize_to_string(root);
  json_set_number_serialization_function(NULL);
  json_set_escape_slashes(1);
  CHECK(text != NULL && std::string(text) == "{\"ann\":[1.00,2.50],\"bob/c\":[]}");

  JSON_Value* reread = json_parse_string(text);
  CHECK(canonical(reread) == "{ann=[1,2.5],bob/c=[]}");
  CHECK(json_value_equals(root, reread));
  json_value_free(reread);
  json_free_serialized_string(text);
  json_value_free(root);

  CHECK(allocations > 0);
  CHECK(live.empty());
  CHECK(unknown_frees == 0);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("parson C++ consumer test passed");
  return 0;
}
