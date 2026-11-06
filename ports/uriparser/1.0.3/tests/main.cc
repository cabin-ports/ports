// Consumer test from C++98: the C API through its extern "C" headers, with
// results gathered into standard library strings and containers.
#include <uriparser/Uri.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, what);
    ++failures;
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

std::string str(const UriTextRangeA& range) {
  return range.first ? std::string(range.first, range.afterLast) : std::string();
}

std::wstring wstr(const UriTextRangeW& range) {
  return range.first ? std::wstring(range.first, range.afterLast) : std::wstring();
}

std::string to_string(const UriUriA& uri) {
  int required = 0;
  if (uriToStringCharsRequiredA(&uri, &required) != URI_SUCCESS) return std::string();
  std::vector<char> text(required + 1);
  if (uriToStringA(&text[0], &uri, required + 1, NULL) != URI_SUCCESS) return std::string();
  return std::string(&text[0]);
}

std::string normalized(const std::string& text) {
  UriUriA uri;
  std::string result;
  if (uriParseSingleUriA(&uri, text.c_str(), NULL) != URI_SUCCESS) return result;
  if (uriNormalizeSyntaxA(&uri) == URI_SUCCESS) result = to_string(uri);
  uriFreeUriMembersA(&uri);
  return result;
}

std::string resolved(const std::string& base, const std::string& reference) {
  UriUriA b, r, out;
  std::string result;
  if (uriParseSingleUriA(&b, base.c_str(), NULL) != URI_SUCCESS) return result;
  if (uriParseSingleUriA(&r, reference.c_str(), NULL) == URI_SUCCESS) {
    if (uriAddBaseUriA(&out, &r, &b) == URI_SUCCESS) {
      result = to_string(out);
      uriFreeUriMembersA(&out);
    }
    uriFreeUriMembersA(&r);
  }
  uriFreeUriMembersA(&b);
  return result;
}

typedef std::vector<std::pair<std::string, std::string> > Query;

Query dissect(const std::string& query) {
  Query items;
  UriQueryListA* list = NULL;
  int count = 0;
  const char* first = query.c_str();
  if (uriDissectQueryMallocA(&list, &count, first, first + query.size()) != URI_SUCCESS)
    return items;
  for (UriQueryListA* item = list; item; item = item->next)
    items.push_back(std::make_pair(item->key, item->value ? item->value : "<null>"));
  uriFreeQueryListA(list);
  return items;
}

}  // namespace

int main() {
  CHECK(std::strcmp(uriBaseRuntimeVersionA(), "1.0.3") == 0);

  UriUriA uri;
  const char* error = NULL;
  const std::string text = "ftp://anonymous@[2001:db8::7]:21/pub/file.txt;type=i";
  CHECK(uriParseSingleUriA(&uri, text.c_str(), &error) == URI_SUCCESS);
  CHECK(str(uri.scheme) == "ftp");
  CHECK(str(uri.userInfo) == "anonymous");
  CHECK(str(uri.hostText) == "2001:db8::7");
  CHECK(uri.hostData.ip6 != NULL && uri.hostData.ip6->data[0] == 0x20 &&
        uri.hostData.ip6->data[1] == 0x01 && uri.hostData.ip6->data[15] == 7);
  CHECK(str(uri.portText) == "21");
  CHECK(uri.pathTail != NULL && str(uri.pathTail->text) == "file.txt;type=i");
  // An IPv6 host is written back as all 16 bytes in hex (UriRecompose.c).
  CHECK(to_string(uri) ==
        "ftp://anonymous@[2001:0db8:0000:0000:0000:0000:0000:0007]:21/pub/file.txt;type=i");
  uriFreeUriMembersA(&uri);

  CHECK(uriParseSingleUriA(&uri, "mailto:a@b c", &error) == URI_ERROR_SYNTAX);

  CHECK(normalized("hTtP://a/b/../%7e/./c%2f") == "http://a/~/c%2F");
  CHECK(resolved("http://a/b/c/d;p?q", "../../g?h#i") == "http://a/g?h#i");
  CHECK(resolved("http://a/b/c/d;p?q", "//g") == "http://g");

  Query q = dissect("k=v&empty=&flag&plus=a+b");
  CHECK(q.size() == 4);
  if (q.size() == 4) {
    CHECK(q[0].first == "k" && q[0].second == "v");
    CHECK(q[1].first == "empty" && q[1].second.empty());
    CHECK(q[2].first == "flag" && q[2].second == "<null>");
    CHECK(q[3].first == "plus" && q[3].second == "a b");
  }

  UriUriW wide;
  const wchar_t* wide_error = NULL;
  CHECK(uriParseSingleUriW(&wide, L"urn:isbn:0451450523", &wide_error) == URI_SUCCESS);
  CHECK(wstr(wide.scheme) == L"urn");
  CHECK(wide.pathHead != NULL && wstr(wide.pathHead->text) == L"isbn:0451450523");
  CHECK(!uriHasHostW(&wide));
  uriFreeUriMembersW(&wide);

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("uriparser C++ consumer test passed");
  return 0;
}
