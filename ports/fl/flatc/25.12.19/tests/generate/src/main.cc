// monster_generated.h is what the build script had flatc write; it refuses
// to compile against the headers of another FlatBuffers version.
#include <cstring>

#include "monster_generated.h"

int main() {
  flatbuffers::FlatBufferBuilder builder;
  const flatbuffers::Offset<flatbuffers::String> name = builder.CreateString("orc");
  builder.Finish(demo::CreateMonster(builder, name));
  flatbuffers::Verifier verifier(builder.GetBufferPointer(), builder.GetSize());
  if (!demo::VerifyMonsterBuffer(verifier)) {
    return 1;
  }
  const demo::Monster *monster = demo::GetMonster(builder.GetBufferPointer());
  return std::strcmp(monster->name()->c_str(), "orc") != 0 || monster->hp() != 100;
}
