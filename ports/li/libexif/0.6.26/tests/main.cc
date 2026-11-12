// Writes exposure time (as a signed rational, which exif_data_fix converts),
// f-number and aperture into the EXIF IFD in Motorola order, reloads the
// saved data, checks the formatted values, then converts it to Intel order.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>

namespace {

int fail(const char* what) {
  std::fprintf(stderr, "%s\n", what);
  return 1;
}

ExifEntry* add_entry(ExifData* d, ExifTag tag) {
  ExifEntry* e = exif_entry_new();
  exif_content_add_entry(d->ifd[EXIF_IFD_EXIF], e);
  exif_entry_initialize(e, tag);
  exif_entry_unref(e);  // The content holds the reference.
  return e;
}

std::string value(ExifData* d, ExifTag tag) {
  char val[64] = "";
  ExifEntry* e = exif_content_get_entry(d->ifd[EXIF_IFD_EXIF], tag);
  if (e == nullptr) return "(missing)";
  exif_entry_get_value(e, val, sizeof val);
  return val;
}

}  // namespace

int main() {
  const ExifByteOrder mm = EXIF_BYTE_ORDER_MOTOROLA;
  unsigned char raw[8];
  ExifSRational sr = {-3, 2};
  exif_set_srational(raw, mm, sr);
  static const unsigned char want_raw[8] = {0xFF, 0xFF, 0xFF, 0xFD,
                                            0x00, 0x00, 0x00, 0x02};
  if (std::memcmp(raw, want_raw, 8) != 0 ||
      exif_get_srational(raw, mm).numerator != -3) {
    return fail("srational encoding");
  }
  if (exif_tag_from_name("FNumber") != EXIF_TAG_FNUMBER ||
      std::strcmp(exif_tag_get_name_in_ifd(EXIF_TAG_APERTURE_VALUE,
                                           EXIF_IFD_EXIF),
                  "ApertureValue") != 0) {
    return fail("tag names");
  }

  ExifData* d = exif_data_new();
  if (d == nullptr) return fail("exif_data_new");
  exif_data_set_byte_order(d, mm);

  ExifEntry* e = add_entry(d, EXIF_TAG_EXPOSURE_TIME);
  e->format = EXIF_FORMAT_SRATIONAL;  // Against the spec; fixed on load.
  ExifSRational exposure = {1, 250};
  exif_set_srational(e->data, mm, exposure);
  e = add_entry(d, EXIF_TAG_FNUMBER);
  ExifRational fnumber = {28, 10};
  exif_set_rational(e->data, mm, fnumber);
  e = add_entry(d, EXIF_TAG_APERTURE_VALUE);
  ExifRational aperture = {5, 1};  // APEX: f/2^(5/2), about f/5.7.
  exif_set_rational(e->data, mm, aperture);

  unsigned char* buf = nullptr;
  unsigned int len = 0;
  exif_data_save_data(d, &buf, &len);
  exif_data_unref(d);
  if (buf == nullptr || len < 14 ||
      std::memcmp(buf, "Exif\0\0MM\0*", 10) != 0) {
    return fail("saved header");
  }

  ExifData* back = exif_data_new_from_data(buf, len);
  std::free(buf);
  if (back == nullptr) return fail("exif_data_new_from_data");
  ExifEntry* exposure_entry =
      exif_content_get_entry(back->ifd[EXIF_IFD_EXIF], EXIF_TAG_EXPOSURE_TIME);
  if (exposure_entry == nullptr ||
      exposure_entry->format != EXIF_FORMAT_RATIONAL ||
      value(back, EXIF_TAG_EXPOSURE_TIME) != "1/250 sec." ||
      value(back, EXIF_TAG_FNUMBER) != "f/2.8" ||
      value(back, EXIF_TAG_APERTURE_VALUE) != "5.00 EV (f/5.7)") {
    std::fprintf(stderr, "values: %s | %s | %s\n",
                 value(back, EXIF_TAG_EXPOSURE_TIME).c_str(),
                 value(back, EXIF_TAG_FNUMBER).c_str(),
                 value(back, EXIF_TAG_APERTURE_VALUE).c_str());
    return 1;
  }

  exif_data_set_byte_order(back, EXIF_BYTE_ORDER_INTEL);
  e = exif_content_get_entry(back->ifd[EXIF_IFD_EXIF], EXIF_TAG_FNUMBER);
  ExifRational intel = exif_get_rational(e->data, EXIF_BYTE_ORDER_INTEL);
  if (e->data[0] != 28 || intel.numerator != 28 || intel.denominator != 10 ||
      value(back, EXIF_TAG_FNUMBER) != "f/2.8") {
    return fail("byte order conversion");
  }

  exif_data_unref(back);
  std::printf("libexif: rational fix, formatting and byte order ok\n");
  return 0;
}
