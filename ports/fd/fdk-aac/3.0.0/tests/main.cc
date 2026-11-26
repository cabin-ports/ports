// The C++ consumer: a mono 48 kHz AAC-LC round trip in raw access units,
// with the decoder configured out of band from the encoder's
// AudioSpecificConfig, and the libraries' version reports.
#include <fdk-aac/aacdecoder_lib.h>
#include <fdk-aac/aacenc_lib.h>
#include <fdk-xhe-aac/xHEAACEnc.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

const int kRate = 48000;
const long kFrames = 48000;  // one second
const long kWinFrom = 9600;  // 600 periods of the tone, from 0.2 s in
const long kWinLen = 28800;
const double kPi = 3.14159265358979323846;
int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

std::string module_version(const LIB_INFO* info, FDK_MODULE_ID id) {
  for (int i = 0; i < FDK_MODULE_LAST && info[i].module_id != FDK_NONE; ++i) {
    if (info[i].module_id == id) {
      return std::string(info[i].title) + " " + info[i].versionStr;
    }
  }
  return "";
}

void check_versions() {
  LIB_INFO info[FDK_MODULE_LAST];
  FDKinitLibInfo(info);
  check(aacEncGetLibInfo(info) == AACENC_OK, "aacEncGetLibInfo");
  check(module_version(info, FDK_AACENC) == "AAC Encoder 4.0.1",
        "aacEncGetLibInfo reports AAC Encoder 4.0.1");
  FDKinitLibInfo(info);
  check(aacDecoder_GetLibInfo(info) == 0, "aacDecoder_GetLibInfo");
  check(module_version(info, FDK_AACDEC) == "AAC Decoder Lib 3.2.0",
        "aacDecoder_GetLibInfo reports AAC Decoder Lib 3.2.0");
  check(std::string(IIS_xHEAACEnc_GetVersionInfoStr()) == "1.0AOSP",
        "IIS_xHEAACEnc_GetVersionInfoStr is 1.0AOSP");
}

}  // namespace

int main() {
  std::vector<INT_PCM> pcm(kFrames);
  for (long n = 0; n < kFrames; ++n) {
    pcm[n] = static_cast<INT_PCM>(std::lrint(0.25 * 32767.0 * std::sin(2.0 * kPi * 1000.0 * n / kRate)));
  }
  check_versions();

  HANDLE_AACENCODER enc = nullptr;
  check(aacEncOpen(&enc, 0, 1) == AACENC_OK, "aacEncOpen");
  check(aacEncoder_SetParam(enc, AACENC_AOT, AOT_AAC_LC) == AACENC_OK, "set AACENC_AOT");
  check(aacEncoder_SetParam(enc, AACENC_SAMPLERATE, kRate) == AACENC_OK, "set AACENC_SAMPLERATE");
  check(aacEncoder_SetParam(enc, AACENC_CHANNELMODE, MODE_1) == AACENC_OK,
        "set AACENC_CHANNELMODE");
  check(aacEncoder_SetParam(enc, AACENC_BITRATE, 96000) == AACENC_OK, "set AACENC_BITRATE");
  check(aacEncoder_SetParam(enc, AACENC_TRANSMUX, TT_MP4_RAW) == AACENC_OK,
        "set AACENC_TRANSMUX");
  check(aacEncEncode(enc, nullptr, nullptr, nullptr, nullptr) == AACENC_OK,
        "encoder initialisation");
  AACENC_InfoStruct info;
  check(aacEncInfo(enc, &info) == AACENC_OK, "aacEncInfo");
  check(info.frameLength == 1024 && info.inputChannels == 1,
        "encoder frame length 1024 with 1 input channel");
  // AudioSpecificConfig: object type 2, frequency index 3 (48 kHz), 1 channel.
  check(info.confSize == 2 && info.confBuf[0] == 0x11 && info.confBuf[1] == 0x88,
        "AudioSpecificConfig 11 88");

  std::vector<std::vector<UCHAR>> units;
  long fed = 0;
  for (int i = 0; i < 1000; ++i) {
    long chunk = kFrames - fed < 1024 ? kFrames - fed : 1024;
    void* in_ptr = pcm.data() + fed;
    INT in_id = IN_AUDIO_DATA, in_size = static_cast<INT>(chunk * sizeof(INT_PCM)),
        in_el = sizeof(INT_PCM);
    UCHAR out[8192];
    void* out_ptr = out;
    INT out_id = OUT_BITSTREAM_DATA, out_size = sizeof out, out_el = 1;
    AACENC_BufDesc in_buf = {1, &in_ptr, &in_id, &in_size, &in_el};
    AACENC_BufDesc out_buf = {1, &out_ptr, &out_id, &out_size, &out_el};
    AACENC_InArgs in_args = {chunk > 0 ? static_cast<INT>(chunk) : -1, 0};
    AACENC_OutArgs out_args = {};
    AACENC_ERROR err = aacEncEncode(enc, &in_buf, &out_buf, &in_args, &out_args);
    if (err == AACENC_ENCODE_EOF) break;
    if (err != AACENC_OK) {
      check(false, "aacEncEncode returns AACENC_OK until AACENC_ENCODE_EOF");
      break;
    }
    fed += out_args.numInSamples;
    if (out_args.numOutBytes > 0) units.emplace_back(out, out + out_args.numOutBytes);
  }
  check(fed == kFrames, "the encoder consumed every input sample");
  check(!units.empty(), "the encoder produced access units");
  check(aacEncClose(&enc) == AACENC_OK && enc == nullptr, "aacEncClose");

  HANDLE_AACDECODER dec = aacDecoder_Open(TT_MP4_RAW, 1);
  check(dec != nullptr, "aacDecoder_Open");
  UCHAR* conf[] = {info.confBuf};
  const UINT conf_size[] = {info.confSize};
  check(aacDecoder_ConfigRaw(dec, conf, conf_size) == AAC_DEC_OK, "aacDecoder_ConfigRaw");
  check(aacDecoder_SetParam(dec, AAC_PCM_LIMITER_ENABLE, 0) == AAC_DEC_OK,
        "set AAC_PCM_LIMITER_ENABLE");
  std::vector<INT_PCM> decoded;
  bool info_ok = true;
  long delay = info.nDelay;
  for (auto& unit : units) {
    UCHAR* ptr = unit.data();
    UINT size = static_cast<UINT>(unit.size()), valid = size;
    check(aacDecoder_Fill(dec, &ptr, &size, &valid) == AAC_DEC_OK && valid == 0,
          "aacDecoder_Fill takes the whole access unit");
    INT_PCM frame[4096];
    AAC_DECODER_ERROR err = aacDecoder_DecodeFrame(dec, frame, 4096, 0);
    if (err != AAC_DEC_OK) {
      std::fprintf(stderr, "aacDecoder_DecodeFrame: 0x%x\n", static_cast<unsigned>(err));
      check(false, "aacDecoder_DecodeFrame returns AAC_DEC_OK");
      break;
    }
    const CStreamInfo* si = aacDecoder_GetStreamInfo(dec);
    info_ok = info_ok && si->sampleRate == 48000 && si->numChannels == 1 &&
              si->frameSize == 1024 && si->aot == AOT_AAC_LC;
    // The decoder delays its output by outputDelay beyond the encoder's delay.
    if (decoded.empty()) delay += si->outputDelay;
    decoded.insert(decoded.end(), frame, frame + si->frameSize);
  }
  aacDecoder_Close(dec);
  check(info_ok, "stream info: AAC LC, 48000 Hz, 1 channel, 1024 samples a frame");

  check(decoded.size() == units.size() * 1024, "the decoder decoded every access unit");
  bool window = static_cast<long>(decoded.size()) >= kWinFrom + kWinLen + delay;
  check(window, "the decoder returned the comparison window");
  if (window) {
    double in = 0, out = 0, noise = 0;
    for (long i = kWinFrom; i < kWinFrom + kWinLen; ++i) {
      double r = pcm[i], d = decoded[i + delay];
      in += r * r;
      out += d * d;
      noise += (r - d) * (r - d);
    }
    double ratio = out / in, snr = 10.0 * std::log10(in / (noise + 1.0));
    std::printf("aac-lc mono: delay %ld, energy ratio %.5f, SNR %.2f dB\n", delay, ratio, snr);
    // 96 kb/s keeps a pure tone's energy within 2% and its SNR above 40 dB.
    check(std::fabs(ratio - 1.0) <= 0.02, "decoded energy within 2% of the input");
    check(snr >= 40.0, "decoded tone at least 40 dB above the coding noise");
  }

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("fdk-aac C++ consumer: all checks passed");
  return 0;
}
