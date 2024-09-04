/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "src/stirling/obj_tools/go_syms.h"
#include "src/stirling/utils/binary_decoder.h"

#include <utility>

namespace px {
namespace stirling {
namespace obj_tools {

using px::utils::u8string_view;
using read_ptr_func_t = std::function<uint64_t(u8string_view)>;

namespace {

// This is defined in src/stirling/bpf_tools/bcc_bpf_intf/go_types.h as well.
// Duplicated to avoid undesired dependency.
struct gostring {
  const char* ptr;
  int64_t len;
};


// This symbol points to a static string variable that describes the Golang tool-chain version used
// to build the executable. This symbol is embedded in a Golang executable's data section.
constexpr std::string_view kGoBuildVersionSymbol = "runtime.buildVersion";

constexpr std::string_view kGoSymtabSection = ".gosymtab";
constexpr std::string_view kROGoSymtabSection = ".data.rel.ro.gosymtab";
constexpr std::string_view kGoPCLNTab = ".gopclntab";
constexpr std::string_view kGoROPCLNTab = ".data.rel.ro.gopclntab";

constexpr std::string_view kGoRuntimeSymtabSymbol = "runtime.symtab";
constexpr std::string_view kGoRuntimeESymtabSymbol = "runtime.esymtab";
constexpr std::string_view kGoRuntimePCLNTabSymbol = "runtime.pclntab";
constexpr std::string_view kGoRuntimeEPCLNTabSymbol = "runtime.epclntab";

constexpr uint32_t kGo12Magic = 0xfffffffb;
constexpr uint32_t kGo116Magic = 0xfffffffa;
constexpr uint32_t kGo118Magic = 0xfffffff0;
constexpr uint32_t kGo120Magic = 0xfffffff1;
constexpr int kMinPCLNTabSize = 16;

std::string_view kLittleEndianSymtab =
    CreateStringView<char>("\xfd\xff\xff\xff\x00\x00\x00");

std::string_view kBigEndianSymtab =
    CreateStringView<char>("\xff\xff\xff\xfd\x00\x00\x00");

std::string_view kOldLittleEndianSymtab =
    CreateStringView<char>("\xfe\xff\xff\xff\x00\x00");

/*
 *      littleEndianSymtab    = []byte{0xFD, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00}
 *              bigEndianSymtab       = []byte{0xFF, 0xFF, 0xFF, 0xFD, 0x00, 0x00, 0x00}
 *                      oldLittleEndianSymtab = []byte{0xFE, 0xFF, 0xFF, 0xFF, 0x00, 0x00}
 */

}  // namespace

bool IsGoExecutable(ElfReader* elf_reader) {
  return elf_reader->SearchTheOnlySymbol(obj_tools::kGoBuildVersionSymbol).ok();
}

constexpr std::string_view kGoBuildInfoSection = ".go.buildinfo";
// kGoBuildInfoMagic corresponds to "\xff Go buildinf:"
// https://github.com/golang/go/blob/1dbbafc70fd3e2c284469ab3e0936c1bb56129f6/src/debug/buildinfo/buildinfo.go#L49
std::string_view kGoBuildInfoMagic =
    CreateStringView<char>("\xff\x20\x47\x6f\x20\x62\x75\x69\x6c\x64\x69\x6e\x66\x3a");

// Reads a Go string encoded within a buildinfo header. This function is meant to provide the same
// functionality as
// https://github.com/golang/go/blob/aa97a012b4be393c1725c16a78b92dea81632378/src/debug/buildinfo/buildinfo.go#L282
StatusOr<std::string> ReadGoString(ElfReader* elf_reader, uint64_t ptr_size, uint64_t ptr_addr,
                                   read_ptr_func_t read_ptr) {
  PX_ASSIGN_OR_RETURN(u8string_view data_addr, elf_reader->BinaryByteCode(ptr_addr, ptr_size));
  PX_ASSIGN_OR_RETURN(u8string_view data_len,
                      elf_reader->BinaryByteCode(ptr_addr + ptr_size, ptr_size));

  PX_ASSIGN_OR_RETURN(ptr_addr, elf_reader->VirtualAddrToBinaryAddr(read_ptr(data_addr)));
  uint64_t str_length = read_ptr(data_len);

  PX_ASSIGN_OR_RETURN(std::string_view go_version_bytecode,
                      elf_reader->BinaryByteCode<char>(ptr_addr, str_length));
  return std::string(go_version_bytecode);
}

// Reads the buildinfo header embedded in the .go.buildinfo ELF section in order to determine the go
// toolchain version. This function emulates what the go version cli performs as seen
// https://github.com/golang/go/blob/cb7a091d729eab75ccfdaeba5a0605f05addf422/src/debug/buildinfo/buildinfo.go#L151-L221
StatusOr<std::string> ReadGoBuildVersion(ElfReader* elf_reader) {
  PX_ASSIGN_OR_RETURN(ELFIO::section * section, elf_reader->SectionWithName(kGoBuildInfoSection));
  int offset = section->get_offset();
  PX_ASSIGN_OR_RETURN(std::string_view buildInfoByteCode,
                      elf_reader->BinaryByteCode<char>(offset, 64 * 1024));

  BinaryDecoder binary_decoder(buildInfoByteCode);

  PX_CHECK_OK(binary_decoder.ExtractStringUntil(kGoBuildInfoMagic));
  PX_ASSIGN_OR_RETURN(uint8_t ptr_size, binary_decoder.ExtractBEInt<uint8_t>());
  PX_ASSIGN_OR_RETURN(uint8_t endianness, binary_decoder.ExtractBEInt<uint8_t>());

  // If the endianness has its second bit set, then the go version immediately follows the 32 bit
  // header specified by the varint encoded string data
  if ((endianness & 0x2) != 0) {
    // Skip the remaining 16 bytes of buildinfo header
    PX_CHECK_OK(binary_decoder.ExtractBufIgnore(16));

    PX_ASSIGN_OR_RETURN(uint64_t size, binary_decoder.ExtractUVarInt());
    PX_ASSIGN_OR_RETURN(std::string_view go_version, binary_decoder.ExtractString(size));
    return std::string(go_version);
  }

  read_ptr_func_t read_ptr;
  switch (endianness) {
    case 0x0: {
      if (ptr_size == 4) {
        read_ptr = [&](u8string_view str_view) {
          return utils::LEndianBytesToInt<uint32_t, 4>(str_view);
        };
      } else if (ptr_size == 8) {
        read_ptr = [&](u8string_view str_view) {
          return utils::LEndianBytesToInt<uint64_t, 8>(str_view);
        };
      } else {
        return error::NotFound(absl::Substitute(
            "Binary reported pointer size=$0, refusing to parse non go binary", ptr_size));
      }
      break;
    }
    case 0x1:
      if (ptr_size == 4) {
        read_ptr = [&](u8string_view str_view) {
          return utils::BEndianBytesToInt<uint64_t, 4>(str_view);
        };
      } else if (ptr_size == 8) {
        read_ptr = [&](u8string_view str_view) {
          return utils::BEndianBytesToInt<uint64_t, 8>(str_view);
        };
      } else {
        return error::NotFound(absl::Substitute(
            "Binary reported pointer size=$0, refusing to parse non go binary", ptr_size));
      }
      break;
    default: {
      auto msg =
          absl::Substitute("Invalid endianness=$0, refusing to parse non go binary", endianness);
      DCHECK(false) << msg;
      return error::NotFound(msg);
    }
  }

  // Reads the virtual address location of the runtime.buildVersion symbol.
  PX_ASSIGN_OR_RETURN(auto runtime_version_vaddr,
                      binary_decoder.ExtractString<u8string_view::value_type>(ptr_size));
  PX_ASSIGN_OR_RETURN(uint64_t ptr_addr,
                      elf_reader->VirtualAddrToBinaryAddr(read_ptr(runtime_version_vaddr)));

  return ReadGoString(elf_reader, ptr_size, ptr_addr, read_ptr);
}

StatusOr<absl::flat_hash_map<std::string, std::vector<IntfImplTypeInfo>>> ExtractGolangInterfaces(
    ElfReader* elf_reader) {
  absl::flat_hash_map<std::string, std::vector<IntfImplTypeInfo>> interface_types;

  // All itable objects in the symbols are prefixed with this string.
  const std::string_view kITablePrefix("go.itab.");

  PX_ASSIGN_OR_RETURN(std::vector<ElfReader::SymbolInfo> itable_symbols,
                      elf_reader->SearchSymbols(kITablePrefix, SymbolMatchType::kPrefix,
                                                /*symbol_type*/ ELFIO::STT_OBJECT));

  for (const auto& sym : itable_symbols) {
    // Expected format is:
    //  go.itab.<type_name>,<interface_name>
    std::vector<std::string_view> sym_split = absl::StrSplit(sym.name, ",");
    if (sym_split.size() != 2) {
      LOG(WARNING) << absl::Substitute("Ignoring unexpected itable format: $0", sym.name);
      continue;
    }

    std::string_view interface_name = sym_split[1];
    std::string_view type = sym_split[0];
    type.remove_prefix(kITablePrefix.size());

    IntfImplTypeInfo info;

    info.type_name = type;
    info.address = sym.address;

    interface_types[std::string(interface_name)].push_back(std::move(info));
  }

  return interface_types;
}

void PrintTo(const std::vector<IntfImplTypeInfo>& infos, std::ostream* os) {
  *os << "[";
  for (auto& info : infos) {
    *os << info.ToString() << ", ";
  }
  *os << "]";
}

StatusOr<LineTable> GetLineTableFromPcln(u8string_view pclnByteCode) {
    auto magic = pclnByteCode.substr(0, 4);
    uint32_t le_magic =  utils::LEndianBytesToInt<uint32_t, 4>(magic);
    uint32_t be_magic = utils::BEndianBytesToInt<uint32_t, 4>(magic);

    LineTable line_table;
    bool magic_found = false;
    switch (le_magic) {
      case kGo12Magic:
        line_table.version = PCNLVersion::ver12;
        line_table.endianess = Endianess::little;
        magic_found = true;
        LOG(INFO) << "Go 1.2 magic number detected";
        break;
      case kGo116Magic:
        line_table.version = PCNLVersion::ver116;
        line_table.endianess = Endianess::little;
        magic_found = true;
        LOG(INFO) << "Go 1.16 magic number detected";
        break;
      case kGo118Magic:
        line_table.version = PCNLVersion::ver118;
        line_table.endianess = Endianess::little;
        magic_found = true;
        LOG(INFO) << "Go 1.18 magic number detected";
        break;
      case kGo120Magic:
        line_table.version = PCNLVersion::ver120;
        line_table.endianess = Endianess::little;
        magic_found = true;
        LOG(INFO) << "Go 1.20 magic number detected";
        break;
      default:
        LOG(WARNING) << "LE magic number not recognized, trying BE";
    }

    if (!magic_found) {
      switch (be_magic) {
        case kGo12Magic:
          line_table.version = PCNLVersion::ver12;
          line_table.endianess = Endianess::big;
          LOG(INFO) << "Go 1.2 magic number detected";
          break;
        case kGo116Magic:
          line_table.version = PCNLVersion::ver116;
          line_table.endianess = Endianess::big;
          LOG(INFO) << "Go 1.16 magic number detected";
          break;
        case kGo118Magic:
          line_table.version = PCNLVersion::ver118;
          line_table.endianess = Endianess::big;
          LOG(INFO) << "Go 1.18 magic number detected";
          break;
        case kGo120Magic:
          line_table.version = PCNLVersion::ver120;
          line_table.endianess = Endianess::big;
          LOG(INFO) << "Go 1.20 magic number detected";
          break;
        default:
          LOG(ERROR) << "BE magic number not recognized";
          return error::NotFound("Invalid pclntab magic");
      }
    }

    // quantum and ptr_size are the same between 1.2, 1.16, and 1.18
    line_table.quantum = pclnByteCode[6];
    line_table.ptr_size = pclnByteCode[7];

    /* std::function<uint64_t(uint32_t)> offset = [&](uint32_t word) { */
    /*   return decoder->ExtractUintptr(offset).ConsumeValueOrDie(); */
    /* }; */
    /* std::function<u8string_view(uint32_t)> data = [&](uint32_t word) { */
    /*   return decoder->ExtractString<u8string_view::value_type>(offset); */
    /* }; */

    switch (line_table.version) {
      case PCNLVersion::ver11:
        break;
      case PCNLVersion::ver12:
        /* line_table.Uintptr = [&](u8string_view bytes) { */
        /*   return utils::LEndianBytesToInt<uint64_t, 8>(bytes); */
        /* }; */
        break;
      case PCNLVersion::ver116:
        /* line_table.Uintptr = [&](u8string_view bytes) { */
        /*   return utils::LEndianBytesToInt<uint64_t, 8>(bytes); */
        /* }; */
        break;
      case PCNLVersion::ver118:
      case PCNLVersion::ver120:
        line_table.nfunctab = uint32_t(line_table.Offset(0, pclnByteCode));
        line_table.nfiletab = uint32_t(line_table.Offset(1, pclnByteCode));
        /* line_table.text_start = offset(2); */ // This needs to be t.PC
        line_table.funcnametab = line_table.Data(3, pclnByteCode);
        line_table.cutab = line_table.Data(4, pclnByteCode);
        line_table.filetab = line_table.Data(5, pclnByteCode);
        line_table.pctab = line_table.Data(6, pclnByteCode);
        line_table.funcdata = line_table.Data(7, pclnByteCode);
        line_table.functab = line_table.Data(7, pclnByteCode);
        auto functab_size = int(line_table.nfunctab * 2 + 1) * line_table.FunctabFieldSize();
        line_table.functab = line_table.functab.substr(0, functab_size);
        break;
    }

    return line_table;
}

StatusOr<std::vector<FuncInfo>> ParseFuncTab(const LineTable& line_table) {
  std::vector<FuncInfo> funcs;
  funcs.reserve(line_table.FunctabSize());
  for (int i = 0; i < line_table.FunctabSize(); i++) {
    FuncInfo func;
    func.entry = line_table.Offset(i, line_table.functab);
    func.end = line_table.Offset(i + 1, line_table.functab);
    func.sym = line_table.Data(i, line_table.funcnametab);
    func.frame_size = 0;
    funcs.push_back(std::move(func));
  }
  return std::vector<FuncInfo>();
}

StatusOr<std::unique_ptr<GoSymTable>> GetGoSymtab(ElfReader* elf_reader) {
    PX_UNUSED(kGoRuntimeSymtabSymbol);
    PX_UNUSED(kGoRuntimeESymtabSymbol);
    PX_UNUSED(kGoRuntimeEPCLNTabSymbol);
    PX_UNUSED(kGoRuntimePCLNTabSymbol);

    auto go_symtab_status = elf_reader->SectionWithName(kGoSymtabSection);
    if (!go_symtab_status.ok()) {
        go_symtab_status = elf_reader->SectionWithName(kROGoSymtabSection);
    }

    if (!go_symtab_status.ok()) {
        // Try bytecode between kGoRuntimeSymtabSymbol and kGoRuntimeESymtabSymbol
    }

    auto go_symtab_section = go_symtab_status.ConsumeValueOrDie();
    // Starting with Go 1.3, the Go symbol table no longer includes symbol data.
    // Therefore this is expected to be 0.
    auto size = go_symtab_section->get_size();
    if (size > 0) {
      LOG(WARNING) << ".gosymtab section size is not 0. This occurs for Go 1.2 and earlier binaries and is not supported at this time";

      int offset = go_symtab_section->get_offset();
      PX_ASSIGN_OR_RETURN(std::string_view symtabByteCode,
                          elf_reader->BinaryByteCode<char>(offset, size));

      if (symtabByteCode.find(kLittleEndianSymtab) == 0) {
        LOG(INFO) << "Little endian symtab format detected";
      } else if (symtabByteCode.find(kBigEndianSymtab) == 0) {
        LOG(INFO) << "Big endian symtab format detected";
      } else if (symtabByteCode.find(kOldLittleEndianSymtab) == 0) {
        LOG(INFO) << "Old symtab format detected";
      } else {
        return error::NotFound("Invalid symtab magic");
      }
    }

    auto go_pclntab_status = elf_reader->SectionWithName(kGoPCLNTab);
    if (!go_pclntab_status.ok()) {
        go_pclntab_status = elf_reader->SectionWithName(kGoROPCLNTab);
    }

    if (!go_pclntab_status.ok()) {
        // Try bytecode between kGoRuntimePCLNTabSymbol and kGoRuntimeEPCLNTabSymbol
    }

    auto go_pclntab_section = go_pclntab_status.ConsumeValueOrDie();
    // Check for pclntab 4-byte magic number: two zeros, pc quantum, pointer size.
    auto pclntab_size = go_pclntab_section->get_size();
    if (pclntab_size < kMinPCLNTabSize) {
      LOG(ERROR) << ".pclntab section size is less than 16 bytes. This is required for go symbolization";
      return error::NotFound("Invalid pclntab size, expected it to be more than 16 bytes, received $0", pclntab_size);
    }

    int pcln_offset = go_pclntab_section->get_offset();
    PX_ASSIGN_OR_RETURN(u8string_view pclnByteCode,
                        elf_reader->BinaryByteCode(pcln_offset, pclntab_size));

    // Check header: 4-byte magic, two zeros, pc quantum, pointer size.
    if (pclnByteCode[4] != 0 || pclnByteCode[5] != 0 ||
        (pclnByteCode[6] != 1 && pclnByteCode[6] != 2 && pclnByteCode[6] != 4) ||  // pc quantum
        (pclnByteCode[7] != 4 && pclnByteCode[7] != 8)) { // ptr size
      return error::NotFound("Invalid pclntab magic");
    }

    PX_ASSIGN_OR_RETURN(auto line_table, GetLineTableFromPcln(pclnByteCode));
    auto sym_table = std::make_unique<GoSymTable>();
    PX_ASSING_OR_RETURN(sym_table->funcs, ParseFuncTab(line_table));
    PX_UNUSED(line_table);

    return std::make_unique<GoSymTable>();
}

StatusOr<std::unique_ptr<GoSymbolizer>> GetGoSymbolizer(ElfReader* elf_reader) {
    PX_UNUSED(elf_reader);
    return std::make_unique<GoSymbolizer>();
}

}  // namespace obj_tools
}  // namespace stirling
}  // namespace px
