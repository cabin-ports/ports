// The C++ consumer, through Lerc_types.h's enumerations: doubles encoded
// losslessly and restored bit for bit, integers beyond 32 bits passed as
// doubles, a pixel of NaNs moved to the mask, a NaN beside valid values
// refused without a noData value, and a noData value at some depths of
// valid pixels, which only the 4D functions handle.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include <Lerc_c_api.h>
#include <Lerc_types.h>

using LercNS::DataType;
using LercNS::ErrCode;
using LercNS::InfoArrOrder;

namespace {

bool ok = true;

void check(bool cond, const char *what) {
  if (!cond) {
    std::printf("FAIL: %s\n", what);
    ok = false;
  }
}

unsigned int type(DataType t) { return static_cast<unsigned int>(t); }
lerc_status status(ErrCode e) { return static_cast<lerc_status>(e); }
std::size_t at(InfoArrOrder i) { return static_cast<std::size_t>(i); }

const int info_size = static_cast<int>(InfoArrOrder::_last);
const int range_size = static_cast<int>(LercNS::DataRangeArrOrder::_last);

struct Blob {
  std::vector<unsigned char> bytes;
  std::vector<unsigned int> info;
  std::vector<double> range;
};

// Encodes with the 4D encoder and reads the blob's info back.
template <typename T>
Blob encode(const std::vector<T> &data, DataType t, int depth, int cols, int rows, int masks,
            const unsigned char *valid, double max_error, const unsigned char *uses_no_data = nullptr,
            const double *no_data = nullptr) {
  Blob blob;
  unsigned int size = 0, written = 0;
  if (lerc_computeCompressedSize_4D(data.data(), type(t), depth, cols, rows, 1, masks, valid, max_error,
                                    &size, uses_no_data, no_data) != status(ErrCode::Ok) ||
      size == 0)
    return blob;
  blob.bytes.resize(size);
  if (lerc_encode_4D(data.data(), type(t), depth, cols, rows, 1, masks, valid, max_error, blob.bytes.data(),
                     size, &written, uses_no_data, no_data) != status(ErrCode::Ok) ||
      written != size) {
    blob.bytes.clear();
    return blob;
  }
  blob.info.resize(info_size);
  blob.range.resize(range_size);
  if (lerc_getBlobInfo(blob.bytes.data(), size, blob.info.data(), blob.range.data(), info_size, range_size) !=
      status(ErrCode::Ok))
    blob.bytes.clear();
  return blob;
}

void lossless_doubles() {
  const int cols = 24, rows = 17;
  std::vector<double> data(cols * rows);
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) data[r * cols + c] = 1000.0 / (1 + c + 3 * r) + c * 0.001;
  Blob blob = encode(data, DataType::dt_double, 1, cols, rows, 0, nullptr, 0.0);
  check(!blob.bytes.empty(), "doubles encode losslessly");
  if (blob.bytes.empty()) return;
  check(blob.info[at(InfoArrOrder::dataType)] == type(DataType::dt_double) &&
            blob.info[at(InfoArrOrder::nCols)] == cols && blob.info[at(InfoArrOrder::nRows)] == rows &&
            blob.range[static_cast<std::size_t>(LercNS::DataRangeArrOrder::maxZErrUsed)] == 0,
        "the blob is a lossless 24 x 17 double raster");
  std::vector<double> out(data.size());
  check(lerc_decode(blob.bytes.data(), static_cast<unsigned int>(blob.bytes.size()), 0, nullptr, 1, cols, rows,
                    1, type(DataType::dt_double), out.data()) == status(ErrCode::Ok) &&
            std::memcmp(out.data(), data.data(), data.size() * sizeof(double)) == 0,
        "the doubles come back bit for bit");
}

void large_integers() {
  const int cols = 9, rows = 8;
  std::vector<double> data(cols * rows);
  for (int i = 0; i < cols * rows; ++i) data[i] = 1099511627776.0 + 1000.0 * (i % 5) + 3.0 * i;  // 2^40 + ...
  // Lerc finds the doubles all integer and floors the error bound, as for
  // integer types, so 0.9 is lossless as 0.5 is.
  Blob blob = encode(data, DataType::dt_double, 1, cols, rows, 0, nullptr, 0.9);
  check(!blob.bytes.empty(), "integers beyond 32 bits encode as doubles");
  if (blob.bytes.empty()) return;
  std::vector<double> out(data.size());
  check(lerc_decode_4D(blob.bytes.data(), static_cast<unsigned int>(blob.bytes.size()), 0, nullptr, 1, cols, rows,
                       1, type(DataType::dt_double), out.data(), nullptr, nullptr) == status(ErrCode::Ok) &&
            out == data,
        "the integers come back exactly");
}

void nan_pixels() {
  const int cols = 10, rows = 7;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  std::vector<float> data(cols * rows);
  unsigned int nans = 0;
  for (int i = 0; i < cols * rows; ++i) {
    data[i] = i % 11 == 4 ? nan : 20.0f + 0.5f * i;
    nans += i % 11 == 4;
  }
  Blob blob = encode(data, DataType::dt_float, 1, cols, rows, 0, nullptr, 0.0);
  check(!blob.bytes.empty(), "a raster with NaN pixels encodes");
  if (blob.bytes.empty()) return;
  check(nans == 6 && blob.info[at(InfoArrOrder::nValidPixels)] == cols * rows - nans &&
            blob.info[at(InfoArrOrder::nMasks)] == 1 && blob.info[at(InfoArrOrder::nUsesNoDataValue)] == 0,
        "the 6 NaN pixels are in the mask, with no noData value");
  std::vector<float> out(data.size());
  std::vector<unsigned char> valid(data.size());
  check(lerc_decode(blob.bytes.data(), static_cast<unsigned int>(blob.bytes.size()), 1, valid.data(), 1, cols, rows,
                    1, type(DataType::dt_float), out.data()) == status(ErrCode::Ok),
        "the raster decodes");
  bool same = true;
  for (int i = 0; i < cols * rows; ++i) {
    if (valid[i] != !std::isnan(data[i]) || (valid[i] && out[i] != data[i])) same = false;
  }
  check(same, "the NaN pixels are invalid and the others exact");

  // A NaN at one depth of a pixel whose other depth is valid has no place
  // in the mask, and Lerc invents no noData value for it.
  std::vector<float> mixed(cols * rows * 2, 1.0f);
  mixed[7] = nan;
  unsigned int size = 0;
  check(lerc_computeCompressedSize_4D(mixed.data(), type(DataType::dt_float), 2, cols, rows, 1, 0, nullptr, 0.0,
                                      &size, nullptr, nullptr) == status(ErrCode::NaN),
        "a NaN beside a valid value needs a noData value");
}

void no_data() {
  const int cols = 10, rows = 8, depth = 2;
  const double max_error = 0.05, missing = -9999.0;
  std::vector<float> data(cols * rows * depth);
  for (int i = 0; i < cols * rows; ++i) {
    data[i * depth] = 5.0f + 0.25f * i;
    data[i * depth + 1] = i % 6 == 1 ? static_cast<float>(missing) : 3.0f - 0.125f * i;
  }
  const unsigned char uses = 1;
  Blob blob = encode(data, DataType::dt_float, depth, cols, rows, 0, nullptr, max_error, &uses, &missing);
  check(!blob.bytes.empty(), "a raster with a noData value encodes");
  if (blob.bytes.empty()) return;
  check(blob.info[at(InfoArrOrder::nDepth)] == depth && blob.info[at(InfoArrOrder::nUsesNoDataValue)] == 1,
        "the blob uses a noData value in its band");
  const unsigned int size = static_cast<unsigned int>(blob.bytes.size());
  std::vector<float> out(data.size());
  check(lerc_decode(blob.bytes.data(), size, 0, nullptr, depth, cols, rows, 1, type(DataType::dt_float),
                    out.data()) == status(ErrCode::HasNoData),
        "the plain decoder refuses a blob with noData");
  double lo[depth], hi[depth];
  check(lerc_getDataRanges(blob.bytes.data(), size, depth, 1, lo, hi) == status(ErrCode::HasNoData),
        "the data ranges are refused too");
  unsigned char used = 0;
  double value = 0;
  check(lerc_decode_4D(blob.bytes.data(), size, 0, nullptr, depth, cols, rows, 1, type(DataType::dt_float),
                       out.data(), &used, &value) == status(ErrCode::Ok) &&
            used == 1 && value == missing,
        "the 4D decoder returns the noData value");
  bool same = true;
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (data[i] == missing ? out[i] != data[i] : !(std::fabs(out[i] - data[i]) <= max_error + 1e-5)) same = false;
  }
  check(same, "noData stays exact and the rest within the error bound");
}

}  // namespace

int main() {
  lossless_doubles();
  large_integers();
  nan_pixels();
  no_data();
  if (!ok) return 1;
  std::puts("lerc (C++): ok");
  return 0;
}
