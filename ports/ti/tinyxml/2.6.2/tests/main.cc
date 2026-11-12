// Parses a document and walks it through handles, typed attribute queries
// and source locations; builds one, serializes it with entity escaping and
// parses that back; and checks the error and position of a mismatched end
// tag.  C++98, the interface standard the port declares.
#include <tinyxml.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#ifndef TIXML_USE_STL
#error "the port's interface defines must select the STL interface"
#endif

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "tinyxml test failed: %s\n", what);
        ++failures;
    }
}

static bool equals(const char* actual, const char* expected) {
    return actual != 0 && std::strcmp(actual, expected) == 0;
}

static void parse_and_query() {
    const char xml[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!-- ports -->\n"
        "<catalog>\n"
        "  <port name=\"zlib\" version=\"1.3.1\" weight=\"0.5\"/>\n"
        "  <port name=\"tinyxml\" version=\"2.6.2\" year=\"2011\">a &lt; b</port>\n"
        "</catalog>\n";

    TiXmlDocument doc;
    doc.Parse(xml);
    check(!doc.Error(), "parse a well-formed document");
    if (doc.Error()) {
        std::fprintf(stderr, "  %s\n", doc.ErrorDesc());
        return;
    }

    const TiXmlDeclaration* decl = doc.FirstChild()->ToDeclaration();
    check(decl != 0 && equals(decl->Version(), "1.0") &&
              equals(decl->Encoding(), "UTF-8"),
          "declaration version and encoding");
    const TiXmlComment* comment = decl ? decl->NextSibling()->ToComment() : 0;
    check(comment != 0 && equals(comment->Value(), " ports "), "comment text");

    TiXmlHandle catalog = TiXmlHandle(&doc).FirstChildElement("catalog");
    const TiXmlElement* zlib = catalog.Child("port", 0).ToElement();
    const TiXmlElement* tiny = catalog.Child("port", 1).ToElement();
    check(catalog.Child("port", 2).ToElement() == 0, "third port is absent");
    if (zlib == 0 || tiny == 0) {
        check(false, "both port elements found");
        return;
    }

    int ports = 0;
    for (const TiXmlElement* e = catalog.FirstChildElement().ToElement(); e;
         e = e->NextSiblingElement("port"))
        ++ports;
    check(ports == 2, "sibling iteration counts two ports");

    check(equals(zlib->Attribute("name"), "zlib"), "name attribute");
    const std::string* version = tiny->Attribute(std::string("version"));
    check(version != 0 && *version == "2.6.2", "std::string attribute lookup");
    check(zlib->Attribute("year") == 0, "missing attribute is null");

    double weight = 0;
    check(zlib->QueryDoubleAttribute("weight", &weight) == TIXML_SUCCESS &&
              weight == 0.5,
          "double attribute");
    int year = 0;
    check(tiny->QueryIntAttribute("year", &year) == TIXML_SUCCESS && year == 2011,
          "int attribute");
    check(zlib->QueryIntAttribute("name", &year) == TIXML_WRONG_TYPE,
          "non-numeric attribute is the wrong type");
    check(zlib->QueryIntAttribute("year", &year) == TIXML_NO_ATTRIBUTE,
          "absent attribute is reported");

    check(equals(tiny->GetText(), "a < b"), "text with an entity is decoded");
    check(tiny->Row() == 5 && tiny->Column() == 3, "element source location");
}

static void build_print_reparse() {
    TiXmlDocument doc;
    doc.LinkEndChild(new TiXmlDeclaration("1.0", "UTF-8", ""));
    TiXmlElement* root = new TiXmlElement("ports");
    doc.LinkEndChild(root);
    TiXmlElement* port = new TiXmlElement("port");
    port->SetAttribute("name", "tinyxml");
    port->SetAttribute("year", 2011);
    port->SetAttribute(std::string("note"), std::string("a<b & \"c\""));
    port->LinkEndChild(new TiXmlText("x > y"));
    root->LinkEndChild(port);
    root->LinkEndChild(new TiXmlElement("empty"));

    const char expected[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>"
        "<ports>"
        "<port name=\"tinyxml\" year=\"2011\""
        " note='a&lt;b &amp; &quot;c&quot;'>x &gt; y</port>"
        "<empty />"
        "</ports>";

    TiXmlPrinter printer;
    printer.SetStreamPrinting();
    doc.Accept(&printer);
    check(printer.Str() == expected, "printer output");
    if (printer.Str() != expected)
        std::fprintf(stderr, "  got: %s\n", printer.CStr());

    std::ostringstream out;
    out << doc;
    check(out.str() == expected, "stream output matches the printer");

    TiXmlDocument back;
    back.Parse(out.str().c_str());
    check(!back.Error(), "reparse the printed document");
    const TiXmlElement* reread =
        TiXmlHandle(&back).FirstChildElement("ports").FirstChildElement("port").ToElement();
    check(reread != 0 && equals(reread->Attribute("note"), "a<b & \"c\"") &&
              equals(reread->GetText(), "x > y"),
          "escaped attribute and text round-trip");
}

static void report_errors() {
    TiXmlDocument doc;
    doc.Parse("<root>\n  <item>\n</root>");
    check(doc.Error(), "mismatched end tag is an error");
    check(doc.ErrorId() == TiXmlBase::TIXML_ERROR_READING_END_TAG,
          "error is a bad end tag");
    check(doc.ErrorRow() == 3 && doc.ErrorCol() == 1, "error position");
    check(doc.ErrorDesc() != 0 && doc.ErrorDesc()[0] != '\0',
          "error description");

    TiXmlDocument empty;
    empty.Parse("");
    check(empty.ErrorId() == TiXmlBase::TIXML_ERROR_DOCUMENT_EMPTY,
          "empty input is reported");
}

int main() {
    parse_and_query();
    build_print_reparse();
    report_errors();
    if (failures != 0)
        return 1;
    std::printf("tinyxml %d.%d.%d ok\n", TIXML_MAJOR_VERSION, TIXML_MINOR_VERSION,
                TIXML_PATCH_VERSION);
    return 0;
}
