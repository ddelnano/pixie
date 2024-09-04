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

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <absl/container/flat_hash_map.h>

#include "src/stirling/obj_tools/elf_reader.h"

namespace px {
namespace stirling {
namespace obj_tools {

// Returns true if the executable is built by Golang.
bool IsGoExecutable(ElfReader* elf_reader);

// Returns the build version of a Golang executable. The executable is read through the input
// elf_reader.
// TODO(yzhao): We'll use this to determine the corresponding Golang executable's TLS data
// structures and their offsets.
StatusOr<std::string> ReadGoBuildVersion(ElfReader* elf_reader);

enum class PCNLVersion {
    ver11,
    ver12,
    ver116,
    ver118,
    ver120,
};

enum class Endianess {
    big,
    little,
};

/*
	textStart   uint64 // address of runtime.text symbol (1.18+)
	funcnametab []byte
	cutab       []byte
	funcdata    []byte
	functab     []byte
	nfunctab    uint32
	filetab     []byte
	pctab       []byte // points to the pctables.
	nfiletab    uint32
	funcNames   map[uint32]string // cache the function names
	strings     map[uint32]string // interned substrings of Data, keyed by offset
	// fileMap varies depending on the version of the object file.
	// For ver12, it maps the name to the index in the file table.
	// For ver116, it maps the name to the offset in filetab.
	fileMap map[string]uint32
*/

struct FuncInfo {
  uint64_t entry;
  uint64_t end;
  std::string sym; // This is a struct. Consider changing to a struct.
  // Params    []*Sym // nil for Go 1.3 and later binaries
  // Locals    []*Sym // nil for Go 1.3 and later binaries
  int frame_size;
  /* obj* obj; */
};

struct LineTable {
  PCNLVersion version;
  Endianess endianess;
  uint32_t quantum;
  uint32_t ptr_size;

  uint64_t text_start;
  utils::u8string_view funcnametab;
  utils::u8string_view cutab;
  utils::u8string_view funcdata;
  utils::u8string_view functab;
  uint32_t nfunctab;
  utils::u8string_view filetab;
  utils::u8string_view pctab;
  uint32_t nfiletab;

  int FunctabSize() {
    return nfunctab;
  }

  int FunctabFieldSize() {
    if (version >= PCNLVersion::ver118) {
      return 4;
    }
    return int(ptr_size);
  }

  uintptr_t Uintptr(utils::u8string_view data) {
    if (endianess == Endianess::big) {
      if (ptr_size == 4) {
        return utils::BEndianBytesToInt<uint32_t, 4>(data);
      }
      return utils::BEndianBytesToInt<uint64_t>(data);
    } else {
      if (ptr_size == 4) {
        return utils::LEndianBytesToInt<uint32_t, 4>(data);
      }
      return utils::LEndianBytesToInt<uint64_t>(data);
    }
  }
  uint64_t Offset(uint32_t word, utils::u8string_view data) {
    return Uintptr(data.substr(8 + word * ptr_size));
  }
  utils::u8string_view Data(uint32_t word, utils::u8string_view data) {
    uint64_t offset = Offset(word, data);
    return data.substr(offset);
  }

  uint64_t Pc(int i, utils::u8string_view data) {
    auto u = Uintptr(functab.substr(2 * i * FunctabFieldSize()));
    if (version >= PCNLVersion::ver118) {
      return u + text_start;
    }
    return u;
  }
};

// Describes a Golang type that implement an interface.
struct IntfImplTypeInfo {
  // The name of the type that implements a given interface.
  std::string type_name;

  // The address of the symbol that records this information.
  uint64_t address = 0;

  std::string ToString() const {
    return absl::Substitute("type_name=$0 address=$1", type_name, address);
  }
};

// Returns a map of all interfaces, and types that implement that interface in a go binary.
StatusOr<absl::flat_hash_map<std::string, std::vector<IntfImplTypeInfo>>> ExtractGolangInterfaces(
    ElfReader* elf_reader);

void PrintTo(const std::vector<IntfImplTypeInfo>& infos, std::ostream* os);

class GoSymbolizer {
 public:
  /**
   * Associate the address range [addr, addr+size] with the provided symbol name.
   * No checking is performed for overlapping regions, which will result in undefined behavior.
   */
  void AddEntry(uintptr_t addr, size_t size, std::string name);

  /**
   * Lookup the symbol for the specified address.
   */
  std::string_view Lookup(uintptr_t addr) const;

 private:
  struct SymbolAddrInfo {
    size_t size;
    std::string name;
  };

  // Key is an address.
  absl::btree_map<uintptr_t, SymbolAddrInfo> symbols_;
};

struct GoSymTable {
/*
 * Represents the symbol table of a Golang binary.

	Syms  []Sym // nil for Go 1.3 and later binaries
	Funcs []Func
	Files map[string]*Obj // for Go 1.2 and later all files map to one Obj
	Objs  []Obj           // for Go 1.2 and later only one Obj in slice
	// contains filtered or unexported fields
 */
  std::vector<std::string> syms;
  std::vector<std::string> funcs;
  std::map<std::string, std::string> files;
  std::vector<std::string> objs;
};

StatusOr<std::unique_ptr<GoSymTable>> GetGoSymtab(ElfReader* elf_reader);

StatusOr<std::unique_ptr<GoSymbolizer>> GetGoSymbolizer(ElfReader* elf_reader);

}  // namespace obj_tools
}  // namespace stirling
}  // namespace px
