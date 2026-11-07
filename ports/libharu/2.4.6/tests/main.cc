// The C++ consumer: an owning wrapper around HPDF_Doc with a C-linkage
// error handler, Courier's fixed advance widths, a Latin-1 character in
// WinAnsiEncoding, a URI link annotation, nested outlines, graphics-state
// depth, HPDF_GetContents, and the handler's error codes.
#include <hpdf.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

bool ok = true;

void check(bool pass, const char* what) {
  if (!pass) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ok = false;
  }
}

std::vector<HPDF_STATUS> reported;

}  // namespace

extern "C" {
static void HPDF_STDCALL record_error(HPDF_STATUS error_no, HPDF_STATUS, void*) {
  reported.push_back(error_no);
}
}

namespace {

using Document = std::unique_ptr<std::remove_pointer<HPDF_Doc>::type, void (*)(HPDF_Doc)>;

Document make_document() {
  return Document(HPDF_New(record_error, nullptr), HPDF_Free);
}

// The whole file, through HPDF_GetContents rather than the doc's stream.
std::string contents(HPDF_Doc pdf) {
  if (HPDF_SaveToStream(pdf) != HPDF_OK) return std::string();
  const HPDF_UINT32 expected = HPDF_GetStreamSize(pdf);
  std::vector<HPDF_BYTE> buffer(expected + 64);
  HPDF_UINT32 size = static_cast<HPDF_UINT32>(buffer.size());
  const HPDF_STATUS status = HPDF_GetContents(pdf, buffer.data(), &size);
  if ((status != HPDF_OK && status != HPDF_STREAM_EOF) || size != expected) {
    return std::string();
  }
  return std::string(buffer.begin(), buffer.begin() + size);
}

bool has(const std::string& pdf, const std::string& needle) {
  return pdf.find(needle) != std::string::npos;
}

void document() {
  Document pdf = make_document();
  check(pdf != nullptr, "HPDF_New");
  if (!pdf) return;

  HPDF_Page page = HPDF_AddPage(pdf.get());
  check(HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_LETTER, HPDF_PAGE_LANDSCAPE) == HPDF_OK,
        "HPDF_Page_SetSize");
  check(HPDF_Page_GetWidth(page) == 792 && HPDF_Page_GetHeight(page) == 612,
        "Letter landscape is 792 x 612 points");

  HPDF_Font courier = HPDF_GetFont(pdf.get(), "Courier", "WinAnsiEncoding");
  check(courier != nullptr, "Courier in WinAnsiEncoding");
  check(HPDF_Page_SetFontAndSize(page, courier, 10) == HPDF_OK, "HPDF_Page_SetFontAndSize");
  // Every Courier glyph advances 600 thousandths of the size.
  check(std::fabs(HPDF_Page_TextWidth(page, "Cabin") - 30) < 0.001, "Courier's widths");
  check(HPDF_Page_BeginText(page) == HPDF_OK &&
            HPDF_Page_TextOut(page, 72, 500, "caf\xe9") == HPDF_OK &&
            HPDF_Page_EndText(page) == HPDF_OK,
        "a Latin-1 string");

  check(HPDF_Page_GSave(page) == HPDF_OK && HPDF_Page_GetGStateDepth(page) == 2,
        "HPDF_Page_GSave nests the graphics state");
  check(HPDF_Page_SetRGBFill(page, 0.5f, 0.25f, 1) == HPDF_OK &&
            HPDF_Page_Rectangle(page, 10, 10, 30, 30) == HPDF_OK &&
            HPDF_Page_Fill(page) == HPDF_OK,
        "a filled rectangle");
  check(HPDF_Page_GRestore(page) == HPDF_OK && HPDF_Page_GetGStateDepth(page) == 1,
        "HPDF_Page_GRestore");

  HPDF_Rect rect = {72, 480, 200, 520};
  check(HPDF_Page_CreateURILinkAnnot(page, rect, "https://example.org") != nullptr,
        "a URI link annotation");

  HPDF_Outline root = HPDF_CreateOutline(pdf.get(), nullptr, "Chapter", nullptr);
  check(root != nullptr &&
            HPDF_CreateOutline(pdf.get(), root, "Section", nullptr) != nullptr,
        "nested outlines");
  check(reported.empty(), "no errors while building");

  const std::string out = contents(pdf.get());
  check(out.compare(0, 7, "%PDF-1.") == 0 && out.size() > 7 &&
            out.compare(out.size() - 7, 7, "\n%%EOF\n") == 0,
        "HPDF_GetContents returns the whole file");
  check(has(out, "/MediaBox [ 0 0 792 612 ]"), "the landscape media box");
  check(has(out, "/BaseFont /Courier") && has(out, "/Encoding /WinAnsiEncoding"),
        "the font resource");
  check(has(out, "(caf\\351) Tj"), "the Latin-1 byte, escaped in octal");
  check(has(out, "0.5 0.25 1 rg\n"), "the fill color operator");
  check(has(out, "q\n") && has(out, "Q\n"), "the save and restore operators");
  check(has(out, "/S /URI") && has(out, "https:\\057\\057example.org"),
        "the link's URI action");
  check(has(out, "/Title (Chapter)") && has(out, "/Title (Section)"), "the outline titles");
}

void errors() {
  reported.clear();
  Document pdf = make_document();
  check(pdf != nullptr, "HPDF_New");
  if (!pdf) return;
  HPDF_Page page = HPDF_AddPage(pdf.get());

  check(HPDF_Page_SetRGBFill(page, 2, 0, 0) == HPDF_PAGE_OUT_OF_RANGE,
        "a color component above 1 is rejected");
  check(reported.size() == 1 && reported.back() == HPDF_PAGE_OUT_OF_RANGE,
        "the handler receives HPDF_PAGE_OUT_OF_RANGE");
  HPDF_ResetError(pdf.get());

  check(HPDF_Page_GRestore(page) != HPDF_OK && reported.size() == 2,
        "restoring past the initial graphics state is an error");
  HPDF_ResetError(pdf.get());

  check(HPDF_GetFont(pdf.get(), "Courier", "No-Such-Encoding") == nullptr &&
            reported.size() == 3,
        "an unknown encoding");
}

}  // namespace

int main() {
  check(std::string(HPDF_GetVersion()) == "2.4.6", "HPDF_GetVersion");
  document();
  errors();
  std::printf("libharu (C++): %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
