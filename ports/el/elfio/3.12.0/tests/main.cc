// elfio: an x86-64 relocatable object with code, a symbol, a relocation and
// a note, written to memory and read back; a big-endian 32-bit executable
// with a loadable segment; a big-endian 64-bit object with a relocation and
// a dynamic section; and streams that are not ELF.
#include <elfio/elfio.hpp>
#include <elfio/elfio_dump.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char* expr, int line) {
  if (!ok) {
    std::fprintf(stderr, "main.cc:%d: check failed: %s\n", line, expr);
    ++failures;
  }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

}  // namespace

int main() {
  using namespace ELFIO;
  CHECK(std::string(ELFIO_VERSION) == "3.12");

  std::stringstream object;
  {
    elfio writer;
    writer.create(ELFCLASS64, ELFDATA2LSB);
    writer.set_os_abi(ELFOSABI_LINUX);
    writer.set_type(ET_REL);
    writer.set_machine(EM_X86_64);

    section* text = writer.sections.add(".text");
    text->set_type(SHT_PROGBITS);
    text->set_flags(SHF_ALLOC | SHF_EXECINSTR);
    text->set_addr_align(16);
    text->set_data("\x48\x31\xC0\xC3", 4);  // xor rax, rax; ret

    section* strtab = writer.sections.add(".strtab");
    strtab->set_type(SHT_STRTAB);
    string_section_accessor strings(strtab);

    section* symtab = writer.sections.add(".symtab");
    symtab->set_type(SHT_SYMTAB);
    symtab->set_info(1);
    symtab->set_addr_align(8);
    symtab->set_entry_size(writer.get_default_entry_size(SHT_SYMTAB));
    symtab->set_link(strtab->get_index());
    symbol_section_accessor symbols(writer, symtab);
    const Elf_Word zero = symbols.add_symbol(
        strings, "zero", 0, 4, STB_GLOBAL, STT_FUNC, 0, text->get_index());
    symbols.add_symbol(strings, "ret", 3, 1, STB_GLOBAL, STT_FUNC, STV_HIDDEN,
                       text->get_index());

    section* rela = writer.sections.add(".rela.text");
    rela->set_type(SHT_RELA);
    rela->set_info(text->get_index());
    rela->set_addr_align(8);
    rela->set_entry_size(writer.get_default_entry_size(SHT_RELA));
    rela->set_link(symtab->get_index());
    relocation_section_accessor relocations(writer, rela);
    relocations.add_entry(1, zero, R_X86_64_PC32, -4);

    section* note = writer.sections.add(".note.cabin");
    note->set_type(SHT_NOTE);
    note_section_accessor notes(writer, note);
    notes.add_note(7, "Cabin", "cab", 3);

    CHECK(writer.save(object));
    // validate() checks file offsets, which save() assigns.
    CHECK(writer.validate().empty());
  }

  // The identification bytes and e_machine (62) as the header stores them.
  const std::string object_bytes = object.str();
  CHECK(object_bytes.compare(0, 8, "\x7F" "ELF\x02\x01\x01\x03") == 0);
  CHECK(object_bytes[18] == 62 && object_bytes[19] == 0);

  elfio reader;
  CHECK(reader.load(object));
  CHECK(reader.get_class() == ELFCLASS64 &&
        reader.get_encoding() == ELFDATA2LSB && reader.get_type() == ET_REL &&
        reader.get_machine() == EM_X86_64 &&
        reader.get_os_abi() == ELFOSABI_LINUX);
  // create() adds the null section and .shstrtab to the five above.
  CHECK(reader.sections.size() == 7);
  section* text = reader.sections[".text"];
  CHECK(text != nullptr && text->get_size() == 4 &&
        std::memcmp(text->get_data(), "\x48\x31\xC0\xC3", 4) == 0 &&
        text->get_flags() == (SHF_ALLOC | SHF_EXECINSTR) &&
        text->get_addr_align() == 16);

  const symbol_section_accessor symbols(reader, reader.sections[".symtab"]);
  Elf64_Addr value = 1;
  Elf_Xword size = 0;
  unsigned char bind = 0;
  unsigned char type = 0;
  unsigned char other = 1;
  Elf_Half shndx = 0;
  CHECK(reader.sections[".symtab"]->get_info() == 1 &&
        reader.sections[".rela.text"]->get_info() == text->get_index());
  CHECK(symbols.get_symbols_num() == 3);
  CHECK(symbols.get_symbol("zero", value, size, bind, type, shndx, other) &&
        value == 0 && size == 4 && bind == STB_GLOBAL && type == STT_FUNC &&
        other == 0 && shndx == text->get_index());
  CHECK(symbols.get_symbol("ret", value, size, bind, type, shndx, other) &&
        value == 3 && size == 1 && other == STV_HIDDEN);
  CHECK(!symbols.get_symbol("missing", value, size, bind, type, shndx, other));

  const relocation_section_accessor relocations(reader,
                                                reader.sections[".rela.text"]);
  Elf64_Addr offset = 0;
  Elf_Word symbol = 0;
  unsigned relocation = 0;
  Elf_Sxword addend = 0;
  CHECK(relocations.get_entries_num() == 1 &&
        relocations.get_entry(0, offset, symbol, relocation, addend) &&
        offset == 1 && symbol == 1 && relocation == R_X86_64_PC32 &&
        addend == -4);

  const note_section_accessor notes(reader, reader.sections[".note.cabin"]);
  Elf_Word note_type = 0;
  std::string note_name;
  char* desc = nullptr;
  Elf_Word desc_size = 0;
  CHECK(notes.get_notes_num() == 1 &&
        notes.get_note(0, note_type, note_name, desc, desc_size) &&
        note_type == 7 && note_name == "Cabin" && desc_size == 3 &&
        std::memcmp(desc, "cab", 3) == 0);

  std::ostringstream dumped;
  dump::header(dumped, reader);
  CHECK(dumped.str().find("  Class:      ELF64\n") != std::string::npos &&
        dumped.str().find("  Type:       Relocatable file\n") !=
            std::string::npos &&
        dumped.str().find("  Machine:    Advanced Micro Devices X86-64 "
                          "processor\n") != std::string::npos);

  // A big-endian 32-bit executable: the segment maps .text and .bss.
  std::stringstream executable;
  {
    elfio writer;
    writer.create(ELFCLASS32, ELFDATA2MSB);
    writer.set_type(ET_EXEC);
    writer.set_machine(EM_PPC);
    writer.set_entry(0x10000000);

    section* code = writer.sections.add(".text");
    code->set_type(SHT_PROGBITS);
    code->set_flags(SHF_ALLOC | SHF_EXECINSTR);
    code->set_addr_align(4);
    code->set_address(0x10000000);
    code->set_data("\x38\x60\x00\x00\x4E\x80\x00\x20", 8);  // li r3, 0; blr

    segment* load = writer.segments.add();
    load->set_type(PT_LOAD);
    load->set_virtual_address(0x10000000);
    load->set_physical_address(0x10000000);
    load->set_flags(PF_X | PF_R);
    load->set_align(0x1000);
    load->add_section(code, code->get_addr_align());

    section* bss = writer.sections.add(".bss");
    bss->set_type(SHT_NOBITS);
    bss->set_flags(SHF_ALLOC | SHF_WRITE);
    bss->set_addr_align(4);
    bss->set_size(16);
    load->add_section(bss, bss->get_addr_align());

    CHECK(writer.save(executable));
    CHECK(writer.validate().empty());
  }
  const std::string executable_bytes = executable.str();
  CHECK(executable_bytes.compare(0, 6, "\x7F" "ELF\x01\x02") == 0);
  CHECK(executable_bytes[18] == 0 && executable_bytes[19] == 20);
  // e_entry and e_phoff, then the first program header's p_offset.
  CHECK(executable_bytes.compare(
            24, 8, std::string("\x10\x00\x00\x00\x00\x00\x00\x34", 8)) == 0);
  CHECK(executable_bytes.compare(56, 4, std::string("\x00\x00\x10\x00", 4)) ==
        0);

  elfio program;
  CHECK(program.load(executable));
  CHECK(program.get_class() == ELFCLASS32 &&
        program.get_encoding() == ELFDATA2MSB &&
        program.get_machine() == EM_PPC && program.get_entry() == 0x10000000);
  CHECK(program.segments.size() == 1);
  const segment* load = program.segments[0];
  CHECK(load->get_type() == PT_LOAD && load->get_flags() == (PF_X | PF_R) &&
        load->get_virtual_address() == 0x10000000 &&
        load->get_physical_address() == 0x10000000 &&
        load->get_align() == 0x1000 && load->get_sections_num() == 2 &&
        load->get_file_size() == 8 && load->get_memory_size() == 24 &&
        load->get_offset() == 0x1000);
  const section* code = program.sections[load->get_section_index_at(0)];
  CHECK(code->get_name() == ".text" && code->get_offset() == load->get_offset() &&
        std::memcmp(code->get_data(), "\x38\x60\x00\x00\x4E\x80\x00\x20", 8) == 0);

  // Lazy loading reads section and segment data on first access.
  elfio lazy;
  CHECK(lazy.load(executable, true));
  CHECK(lazy.sections[".text"] != nullptr &&
        std::memcmp(lazy.sections[".text"]->get_data(),
                    "\x38\x60\x00\x00\x4E\x80\x00\x20", 8) == 0 &&
        std::memcmp(lazy.segments[0]->get_data(),
                    "\x38\x60\x00\x00\x4E\x80\x00\x20", 8) == 0);

  // A big-endian 64-bit object: e_entry and a negative addend in file byte
  // order, and a dynamic section naming a needed library.
  std::stringstream object64;
  std::size_t rela_offset = 0;
  {
    elfio writer;
    writer.create(ELFCLASS64, ELFDATA2MSB);
    writer.set_type(ET_REL);
    writer.set_machine(EM_PPC64);
    writer.set_entry(0x0102030405060708);

    section* rela = writer.sections.add(".rela");
    rela->set_type(SHT_RELA);
    rela->set_entry_size(writer.get_default_entry_size(SHT_RELA));
    relocation_section_accessor relocations(writer, rela);
    relocations.add_entry(0x10, 1, 2, -4);

    section* dynstr = writer.sections.add(".dynstr");
    dynstr->set_type(SHT_STRTAB);
    section* dynamic = writer.sections.add(".dynamic");
    dynamic->set_type(SHT_DYNAMIC);
    dynamic->set_entry_size(writer.get_default_entry_size(SHT_DYNAMIC));
    dynamic->set_link(dynstr->get_index());
    dynamic_section_accessor entries(writer, dynamic);
    entries.add_entry(DT_NEEDED, "libc.so.6");
    entries.add_entry(DT_NULL, 0);

    CHECK(writer.save(object64));
    rela_offset = static_cast<std::size_t>(rela->get_offset());
  }
  const std::string object64_bytes = object64.str();
  CHECK(object64_bytes.compare(
            24, 8, std::string("\x01\x02\x03\x04\x05\x06\x07\x08", 8)) == 0);
  // r_addend follows r_offset and r_info.
  CHECK(object64_bytes.compare(
            rela_offset + 16, 8,
            std::string("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFC", 8)) == 0);
  elfio reader64;
  CHECK(reader64.load(object64) &&
        reader64.get_entry() == 0x0102030405060708);
  const relocation_section_accessor relocations64(reader64,
                                                  reader64.sections[".rela"]);
  CHECK(relocations64.get_entry(0, offset, symbol, relocation, addend) &&
        offset == 0x10 && symbol == 1 && relocation == 2 && addend == -4);
  const dynamic_section_accessor entries(reader64,
                                         reader64.sections[".dynamic"]);
  Elf_Xword tag = 0;
  Elf_Xword entry_value = 0;
  std::string needed;
  CHECK(entries.get_entries_num() == 2 &&
        entries.get_entry(0, tag, entry_value, needed) && tag == DT_NEEDED &&
        needed == "libc.so.6");

  // A script, objects whose magic or byte order is corrupted, and a
  // truncated header.
  std::stringstream not_elf("#!/bin/sh\necho not an ELF file\n");
  std::string corrupted = object_bytes;
  corrupted[3] = 'G';
  std::stringstream bad_magic(corrupted);
  corrupted = object_bytes;
  corrupted[5] = 3;
  std::stringstream bad_encoding(corrupted);
  std::stringstream truncated(object_bytes.substr(0, 20));
  elfio script_reader;
  elfio bad_magic_reader;
  elfio bad_encoding_reader;
  elfio truncated_reader;
  CHECK(!script_reader.load(not_elf));
  CHECK(!bad_magic_reader.load(bad_magic));
  CHECK(!bad_encoding_reader.load(bad_encoding));
  CHECK(!truncated_reader.load(truncated));

  if (failures != 0) {
    return 1;
  }
  std::puts("elfio ok");
  return 0;
}
