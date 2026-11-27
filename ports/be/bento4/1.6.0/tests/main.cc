#include <bento4/Ap4.h>

#include <cstdio>
#include <cstring>

static int failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

int main() {
  // Three audio samples of 4, 5 and 6 bytes, 1024 ticks each at 44100 Hz.
  static const AP4_UI08 payload[15] = {1, 2, 3,  4,  10, 11, 12, 13,
                                       14, 20, 21, 22, 23, 24, 25};
  static const AP4_Size sizes[3] = {4, 5, 6};

  AP4_ByteStream* samples = new AP4_MemoryByteStream(payload, sizeof(payload));
  AP4_SyntheticSampleTable* table = new AP4_SyntheticSampleTable();
  table->AddSampleDescription(
      new AP4_GenericAudioSampleDescription(AP4_ATOM_TYPE('O', 'p', 'u', 's'),
                                            44100, 16, 2, NULL));
  AP4_Position offset = 0;
  for (int i = 0; i < 3; ++i) {
    CHECK(AP4_SUCCEEDED(
        table->AddSample(*samples, offset, sizes[i], 1024, 0, 1024 * i, 0, true)));
    offset += sizes[i];
  }

  AP4_Movie* movie = new AP4_Movie(1000);
  movie->AddTrack(new AP4_Track(AP4_Track::TYPE_AUDIO, table, 7, 1000, 70,
                                44100, 3072, "eng", 0, 0));
  AP4_File file(movie);
  AP4_UI32 brands[2] = {AP4_FTYP_BRAND_ISOM, AP4_FTYP_BRAND_MP42};
  file.SetFileType(AP4_FTYP_BRAND_MP42, 1, brands, 2);

  AP4_MemoryByteStream* out = new AP4_MemoryByteStream();
  CHECK(AP4_SUCCEEDED(AP4_FileWriter::Write(file, *out)));
  samples->Release();

  // The file starts with a 24-byte "ftyp" box whose major brand is "mp42".
  const AP4_UI08* bytes = out->GetData();
  CHECK(out->GetDataSize() > 24);
  CHECK(std::memcmp(bytes, "\0\0\0\x18" "ftypmp42", 12) == 0);

  // Parse the bytes back and compare with what was written.
  AP4_ByteStream* in = new AP4_MemoryByteStream(bytes, out->GetDataSize());
  out->Release();
  AP4_File parsed(*in);
  AP4_Movie* pm = parsed.GetMovie();
  CHECK(pm != NULL);
  if (pm != NULL) {
    CHECK(pm->GetTimeScale() == 1000);
    CHECK(pm->GetTracks().ItemCount() == 1);
    AP4_Track* track = pm->GetTrack(AP4_Track::TYPE_AUDIO);
    CHECK(track != NULL);
    if (track != NULL) {
      CHECK(track->GetId() == 7);
      CHECK(track->GetMediaTimeScale() == 44100);
      CHECK(track->GetMediaDuration() == 3072);
      CHECK(track->GetSampleCount() == 3);
      AP4_SampleDescription* sd = track->GetSampleDescription(0);
      CHECK(sd != NULL);
      if (sd != NULL) {
        CHECK(sd->GetFormat() == AP4_ATOM_TYPE('O', 'p', 'u', 's'));
        AP4_AudioSampleDescription* audio =
            AP4_DYNAMIC_CAST(AP4_AudioSampleDescription, sd);
        CHECK(audio != NULL);
        if (audio != NULL) {
          CHECK(audio->GetSampleRate() == 44100);
          CHECK(audio->GetChannelCount() == 2);
          CHECK(audio->GetSampleSize() == 16);
        }
      }
      const AP4_UI08* expect = payload;
      for (AP4_Ordinal i = 0; i < 3; ++i) {
        AP4_Sample sample;
        AP4_DataBuffer data;
        CHECK(AP4_SUCCEEDED(track->ReadSample(i, sample, data)));
        CHECK(sample.GetDts() == 1024u * i);
        CHECK(sample.GetSize() == sizes[i]);
        CHECK(data.GetDataSize() == sizes[i]);
        CHECK(std::memcmp(data.GetData(), expect, sizes[i]) == 0);
        expect += sizes[i];
      }
    }
  }
  in->Release();

  // AES-128 CBC with a zero IV: its first block is the FIPS-197 appendix C.1 vector.
  static const AP4_UI08 key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                   0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                   0x0c, 0x0d, 0x0e, 0x0f};
  static const AP4_UI08 plain[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
                                     0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                     0xcc, 0xdd, 0xee, 0xff};
  static const AP4_UI08 cipher[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b,
                                      0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80,
                                      0x70, 0xb4, 0xc5, 0x5a};
  AP4_BlockCipher* aes = NULL;
  CHECK(AP4_SUCCEEDED(AP4_DefaultBlockCipherFactory::Instance.CreateCipher(
      AP4_BlockCipher::AES_128, AP4_BlockCipher::ENCRYPT,
      AP4_BlockCipher::CBC, NULL, key, 16, aes)));
  if (aes != NULL) {
    AP4_UI08 got[16];
    CHECK(AP4_SUCCEEDED(aes->Process(plain, 16, got, NULL)));
    CHECK(std::memcmp(got, cipher, 16) == 0);
    delete aes;
  }

  std::printf(failures == 0 ? "ok\n" : "FAILED\n");
  return failures == 0 ? 0 : 1;
}
