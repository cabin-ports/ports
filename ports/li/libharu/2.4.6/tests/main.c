/* The C consumer: the version and the generated hpdf_config.h, a
   document written to the in-memory stream with base-14 text measured
   from the font's metrics, escaped string operands, vector paths, a PNG
   decoded through libpng and embedded as raw RGB samples, info
   attributes, page labels and outlines; Flate compression through zlib;
   RC4 128-bit encryption; and the error codes the handler receives. */
#include <hpdf.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(LIBHPDF_HAVE_LIBPNG) || !defined(LIBHPDF_HAVE_ZLIB)
#error "hpdf_config.h must enable libpng and zlib"
#endif

static int ok = 1;

static void check(int pass, const char *what) {
    if (!pass) {
        fprintf(stderr, "FAIL: %s\n", what);
        ok = 0;
    }
}

struct errors {
    HPDF_STATUS last;
    int count;
};

static void HPDF_STDCALL on_error(HPDF_STATUS error_no, HPDF_STATUS detail_no,
                                  void *user_data) {
    struct errors *errors = (struct errors *)user_data;
    (void)detail_no;
    errors->last = error_no;
    errors->count++;
}

/* An original 2x2 RGB image: red, green / blue, white. */
static const HPDF_BYTE png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
    0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a, 0x73, 0x00, 0x00, 0x00,
    0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xc0,
    0x00, 0xc2, 0x0c, 0xff, 0x81, 0x00, 0x00, 0x1f, 0xee, 0x05, 0xfb, 0xf1,
    0xab, 0xba, 0x77, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
    0x42, 0x60, 0x82};

static const char pixels[] = "\xff\x00\x00\x00\xff\x00\x00\x00\xff\xff\xff\xff";

struct pdf_bytes {
    char *data;
    size_t size;
};

/* Saves the document to its memory stream and copies the stream out. */
static struct pdf_bytes save(HPDF_Doc pdf) {
    struct pdf_bytes out = {NULL, 0};
    HPDF_UINT32 size;
    HPDF_STATUS status;

    if (HPDF_SaveToStream(pdf) != HPDF_OK) {
        return out;
    }
    size = HPDF_GetStreamSize(pdf);
    out.data = (char *)malloc(size);
    if (out.data == NULL) {
        return out;
    }
    out.size = size;
    status = HPDF_ReadFromStream(pdf, (HPDF_BYTE *)out.data, &size);
    if ((status != HPDF_OK && status != HPDF_STREAM_EOF) || size != out.size) {
        free(out.data);
        out.data = NULL;
        out.size = 0;
    }
    return out;
}

/* PDF output holds NUL bytes, so this cannot be strstr. */
static int contains(struct pdf_bytes pdf, const char *needle, size_t length) {
    size_t i;
    for (i = 0; pdf.data != NULL && i + length <= pdf.size; i++) {
        if (memcmp(pdf.data + i, needle, length) == 0) {
            return 1;
        }
    }
    return 0;
}
#define CONTAINS(pdf, literal) contains((pdf), (literal), sizeof(literal) - 1)

static int is_complete_pdf(struct pdf_bytes pdf) {
    return pdf.data != NULL && pdf.size > 16 && memcmp(pdf.data, "%PDF-1.", 7) == 0 &&
           memcmp(pdf.data + pdf.size - 7, "\n%%EOF\n", 7) == 0;
}

static void version(void) {
    check(strcmp(HPDF_GetVersion(), "2.4.6") == 0, "HPDF_GetVersion");
    check(strcmp(HPDF_VERSION_TEXT, "2.4.6") == 0, "HPDF_VERSION_TEXT");
}

/* One A4 page of text, a rectangle and the image, then a second page. */
static void fill(HPDF_Doc pdf, struct errors *errors) {
    HPDF_Page page;
    HPDF_Font font;
    HPDF_Image image;

    check(HPDF_SetInfoAttr(pdf, HPDF_INFO_TITLE, "Port check") == HPDF_OK,
          "HPDF_SetInfoAttr");
    page = HPDF_AddPage(pdf);
    check(HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT) == HPDF_OK,
          "HPDF_Page_SetSize");
    check(fabs(HPDF_Page_GetWidth(page) - 595.276) < 0.001 &&
              fabs(HPDF_Page_GetHeight(page) - 841.89) < 0.001,
          "A4 portrait is 595.276 x 841.89 points");

    font = HPDF_GetFont(pdf, "Helvetica", NULL);
    check(font != NULL && strcmp(HPDF_Font_GetFontName(font), "Helvetica") == 0,
          "the Helvetica base-14 font");
    check(HPDF_Page_SetFontAndSize(page, font, 12) == HPDF_OK, "HPDF_Page_SetFontAndSize");
    /* H 722 + e 556 + l 222 + l 222 + o 556 thousandths of 12 points. */
    check(fabs(HPDF_Page_TextWidth(page, "Hello") - 27.336) < 0.001,
          "text width from the Helvetica metrics");
    check(HPDF_Page_BeginText(page) == HPDF_OK &&
              HPDF_Page_TextOut(page, 72, 760, "Hello (PDF)") == HPDF_OK &&
              HPDF_Page_EndText(page) == HPDF_OK,
          "a text object");
    check(HPDF_Page_SetLineWidth(page, 2) == HPDF_OK &&
              HPDF_Page_Rectangle(page, 50, 50, 100, 40) == HPDF_OK &&
              HPDF_Page_Stroke(page) == HPDF_OK,
          "a stroked rectangle");

    image = HPDF_LoadPngImageFromMem(pdf, png, sizeof(png));
    check(image != NULL, "HPDF_LoadPngImageFromMem");
    if (image != NULL) {
        check(HPDF_Image_GetWidth(image) == 2 && HPDF_Image_GetHeight(image) == 2,
              "the PNG's dimensions");
        check(HPDF_Image_GetBitsPerComponent(image) == 8 &&
                  strcmp(HPDF_Image_GetColorSpace(image), "DeviceRGB") == 0,
              "the PNG's sample format");
        check(HPDF_Page_DrawImage(page, image, 200, 200, 20, 20) == HPDF_OK,
              "HPDF_Page_DrawImage");
    }

    check(HPDF_AddPage(pdf) != NULL, "a second page");
    check(HPDF_AddPageLabel(pdf, 0, HPDF_PAGE_NUM_STYLE_LOWER_ROMAN, 1, "") == HPDF_OK,
          "a page label");
    check(HPDF_CreateOutline(pdf, NULL, "Start", NULL) != NULL, "an outline entry");
    check(errors->count == 0, "no errors while building");
}

static void plain(void) {
    struct errors errors = {HPDF_OK, 0};
    HPDF_Doc pdf = HPDF_New(on_error, &errors);
    struct pdf_bytes out;

    check(pdf != NULL, "HPDF_New");
    if (pdf == NULL) {
        return;
    }
    fill(pdf, &errors);
    check(strcmp(HPDF_GetInfoAttr(pdf, HPDF_INFO_TITLE), "Port check") == 0,
          "HPDF_GetInfoAttr");

    out = save(pdf);
    check(is_complete_pdf(out), "an uncompressed PDF from header to %%EOF");
    check(CONTAINS(out, "/Title (Port check)"), "the info title");
    check(CONTAINS(out, "(Hello \\050PDF\\051) Tj"), "the text, its parentheses escaped");
    check(CONTAINS(out, "2 w\n50 50 100 40 re\nS\n"), "the rectangle's operators");
    check(CONTAINS(out, "/Count 2"), "two pages");
    check(CONTAINS(out, "/Subtype /Image") && CONTAINS(out, "/ColorSpace /DeviceRGB"),
          "an image XObject");
    check(contains(out, pixels, sizeof(pixels) - 1), "libpng's decoded samples");
    check(CONTAINS(out, "/S /r"), "lower-roman page numbers");
    check(CONTAINS(out, "/Title (Start)"), "the outline title");
    check(!CONTAINS(out, "/FlateDecode"), "nothing compressed by default");
    free(out.data);
    HPDF_Free(pdf);
}

static void compressed(void) {
    struct errors errors = {HPDF_OK, 0};
    HPDF_Doc pdf = HPDF_New(on_error, &errors);
    struct pdf_bytes out;

    check(pdf != NULL, "HPDF_New");
    if (pdf == NULL) {
        return;
    }
    check(HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL) == HPDF_OK,
          "zlib makes every compression mode available");
    fill(pdf, &errors);
    out = save(pdf);
    check(is_complete_pdf(out), "a compressed PDF from header to %%EOF");
    check(CONTAINS(out, "/Filter [ /FlateDecode ]"), "Flate-compressed streams");
    check(!CONTAINS(out, "(Hello \\050PDF\\051) Tj") &&
              !contains(out, pixels, sizeof(pixels) - 1),
          "neither the text nor the samples appear uncompressed");
    free(out.data);
    HPDF_Free(pdf);
}

static void encrypted(void) {
    struct errors errors = {HPDF_OK, 0};
    HPDF_Doc pdf = HPDF_New(on_error, &errors);
    struct pdf_bytes out;

    check(pdf != NULL, "HPDF_New");
    if (pdf == NULL) {
        return;
    }
    fill(pdf, &errors);
    check(HPDF_SetPassword(pdf, "owner-secret", "reader") == HPDF_OK &&
              HPDF_SetEncryptionMode(pdf, HPDF_ENCRYPT_R3, 16) == HPDF_OK &&
              HPDF_SetPermission(pdf, HPDF_ENABLE_PRINT) == HPDF_OK,
          "RC4 128-bit encryption");
    out = save(pdf);
    check(is_complete_pdf(out) && memcmp(out.data, "%PDF-1.4", 8) == 0,
          "revision 3 raises the PDF version to 1.4");
    check(CONTAINS(out, "/Filter /Standard") && CONTAINS(out, "/R 3") &&
              CONTAINS(out, "/Length 128"),
          "the standard security handler's dictionary");
    check(!CONTAINS(out, "(Port check)") && !CONTAINS(out, "(Hello"),
          "strings and content are encrypted");
    check(errors.count == 0, "no errors while encrypting");
    free(out.data);
    HPDF_Free(pdf);
}

static void error_codes(void) {
    struct errors errors = {HPDF_OK, 0};
    HPDF_Doc pdf = HPDF_New(on_error, &errors);
    HPDF_Page page;

    check(pdf != NULL, "HPDF_New");
    if (pdf == NULL) {
        return;
    }
    page = HPDF_AddPage(pdf);

    check(HPDF_Page_SetLineWidth(page, -1) == HPDF_PAGE_OUT_OF_RANGE,
          "a negative line width is rejected");
    check(errors.count == 1 && errors.last == HPDF_PAGE_OUT_OF_RANGE,
          "the handler receives HPDF_PAGE_OUT_OF_RANGE");
    check(HPDF_GetError(pdf) == HPDF_PAGE_OUT_OF_RANGE, "HPDF_GetError");
    HPDF_ResetError(pdf);
    check(HPDF_GetError(pdf) == HPDF_OK, "HPDF_ResetError");

    check(HPDF_GetFont(pdf, "No-Such-Font", NULL) == NULL &&
              errors.last == HPDF_INVALID_FONT_NAME,
          "an unknown font name");
    HPDF_ResetError(pdf);

    check(HPDF_LoadPngImageFromMem(pdf, (const HPDF_BYTE *)"not a png image", 15) ==
                  NULL &&
              errors.last == HPDF_INVALID_PNG_IMAGE,
          "bytes without the PNG signature");
    HPDF_Free(pdf);
}

int main(void) {
    version();
    plain();
    compressed();
    encrypted();
    error_codes();
    printf("libharu (C): %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
