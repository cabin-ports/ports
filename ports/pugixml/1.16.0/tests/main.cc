// Parses a document, queries it through the DOM and XPath, edits and
// re-serializes it, and checks that malformed input reports the error
// and its offset.  C++98, the interface standard the port declares.
#include <pugixml.hpp>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

static bool fail(const char* what) {
    std::fprintf(stderr, "pugixml test failed: %s\n", what);
    return false;
}

int main() {
    const char xml[] =
        "<?xml version=\"1.0\"?>"
        "<catalog>"
        "<port name=\"zlib\" version=\"1.3.1\"><links>z</links></port>"
        "<port name=\"lz4\" version=\"1.11.0\"><links>lz4</links></port>"
        "<port name=\"pugixml\" version=\"1.16.0\"/>"
        "</catalog>";

    pugi::xml_document doc;
    const pugi::xml_parse_result parsed = doc.load_string(xml);
    if (!parsed) {
        fail(parsed.description());
        return 1;
    }

    bool ok = true;
    pugi::xml_node catalog = doc.child("catalog");
    int ports = 0;
    for (pugi::xml_node p = catalog.child("port"); p; p = p.next_sibling("port")) ++ports;
    ok = ok && (ports == 3 || fail("port count"));
    ok = ok && (std::strcmp(catalog.find_child_by_attribute("port", "name", "lz4")
                                .child_value("links"),
                            "lz4") == 0 ||
                fail("find_child_by_attribute"));

    // An attribute step over several parents yields an unsorted set.
    pugi::xpath_node_set linked = doc.select_nodes("/catalog/port[links]/@name");
    linked.sort();
    ok = ok && (linked.size() == 2 || fail("xpath node set"));
    ok = ok && (std::strcmp(linked[1].attribute().value(), "lz4") == 0 || fail("xpath order"));
    const double count = pugi::xpath_query("count(//port[starts-with(@version, '1.1')])")
                             .evaluate_number(doc);
    ok = ok && (count == 2.0 || fail("xpath count"));

    catalog.append_child("port").append_attribute("name").set_value("fmt");
    std::ostringstream out;
    doc.save(out, "", pugi::format_raw | pugi::format_no_declaration);
    const std::string expected =
        "<catalog><port name=\"zlib\" version=\"1.3.1\"><links>z</links></port>"
        "<port name=\"lz4\" version=\"1.11.0\"><links>lz4</links></port>"
        // format_raw writes empty elements without a space before "/>".
        "<port name=\"pugixml\" version=\"1.16.0\"/><port name=\"fmt\"/></catalog>";
    ok = ok && (out.str() == expected || fail(("serialization: " + out.str()).c_str()));

    pugi::xml_document broken;
    const char bad[] = "<a><b></a>";
    const pugi::xml_parse_result error = broken.load_string(bad);
    ok = ok && ((error.status == pugi::status_end_element_mismatch && error.offset == 8) ||
                fail("mismatch not reported"));
    return ok ? 0 : 1;
}
