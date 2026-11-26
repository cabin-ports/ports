// person.hh is what the build script had avrogencpp write from
// schema/person.json.
#include <avro/Decoder.hh>
#include <avro/Encoder.hh>
#include <avro/Stream.hh>

#include "person.hh"

int main() {
  demo::Person in;
  in.id = 27;
  in.name = "foo";

  std::unique_ptr<avro::OutputStream> out = avro::memoryOutputStream();
  avro::EncoderPtr encoder = avro::binaryEncoder();
  encoder->init(*out);
  avro::encode(*encoder, in);
  encoder->flush();

  std::unique_ptr<avro::InputStream> data = avro::memoryInputStream(*out);
  avro::DecoderPtr decoder = avro::binaryDecoder();
  decoder->init(*data);
  demo::Person back;
  avro::decode(*decoder, back);
  return back.id != 27 || back.name != "foo";
}
