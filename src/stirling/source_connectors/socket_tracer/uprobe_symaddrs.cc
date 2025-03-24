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

#include "src/stirling/source_connectors/socket_tracer/uprobe_symaddrs.h"
#include <dlfcn.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/common/base/base.h"
#include "src/common/fs/fs_wrapper.h"
#include "src/stirling/obj_tools/dwarf_reader.h"
#include "src/stirling/obj_tools/elf_reader.h"
#include "src/stirling/obj_tools/go_syms.h"
#include "src/stirling/utils/detect_application.h"

using ::px::stirling::obj_tools::DwarfReader;
using ::px::stirling::obj_tools::ElfReader;
using ::px::stirling::obj_tools::RawFptrManager;

DEFINE_bool(
    openssl_force_raw_fptrs, false,
    "Forces the openssl tracing to determine the openssl version without dlopen/dlsym. Used in the "
    "openssl_trace_bpf_test code for integration testing raw function pointers");

// TODO(ddelnano): Set this to disabled by default since using function pointers can cause
// segmentation faults. The default can be changed once this feature has been battle tested.
DEFINE_bool(openssl_raw_fptrs_enabled, false,
            "If true, allows the openssl tracing implementation to fall back to function pointers "
            "if dlopen/dlsym is unable to find symbols");

std::map<std::string, std::map<std::string, std::map<std::string, int32_t>>> g_structsOffsetMap;

std::map<std::string, std::map<std::string, std::map<std::string, std::unique_ptr<location_t>>>>
    g_funcsLocationMap;

namespace px {
namespace stirling {

namespace {
// A golang array consists of a pointer, a length and a capacity.
// These could come from DWARF information, but are hard-coded,
// since an array is a pretty stable type.
constexpr int kGoArrayPtrOffset = 0;
constexpr int kGoArrayLenOffset = 8;

StatusOr<int32_t> GetStructOffset(const std::string& st_name, const std::string& field_name,
                                  const std::string& go_version) {
  auto& structs_map = GetStructsOffsetMap();
  auto struct_map = structs_map.find(st_name);
  if (struct_map == structs_map.end()) {
    return error::Internal("Unable to find function location for $0", st_name);
  }
  auto field_map = struct_map->second.find(field_name);
  if (field_map == struct_map->second.end()) {
    return error::Internal("Unable to find fn fieldument location for $0", field_name);
  }
  auto version_map = field_map->second.find(go_version);
  if (version_map == field_map->second.end()) {
    return error::Internal("Unable to find fn location for version: $0", go_version);
  }
  return version_map->second;
}

StatusOr<location_t> GetArgOffset(const std::string& fn_name, const std::string& arg_name,
                                  const std::string& go_version) {
  constexpr int32_t kSPOffset = 8;

  auto& fns_map = GetFuncsLocationMap();
  auto fn_map = fns_map.find(fn_name);
  if (fn_map == fns_map.end()) {
    return error::Internal("Unable to find function location for $0", fn_name);
  }
  auto arg_map = fn_map->second.find(arg_name);
  if (arg_map == fn_map->second.end()) {
    return error::Internal("Unable to find fn argument location for $0", arg_name);
  }
  auto version_map = arg_map->second.find(go_version);
  if (version_map == arg_map->second.end()) {
    return error::Internal("Unable to find fn location for version: $0", go_version);
  }
  auto loc_ptr = version_map->second.get();
  if (loc_ptr == nullptr) {
    return error::Internal("Unable to find fn location for version: $0", go_version);
  }
  location_t location = *loc_ptr;

  if (location.type == kLocationTypeStack) {
    location.offset = location.offset + kSPOffset;
  }
  return location;
}

}  // namespace

//-----------------------------------------------------------------------------
// Symbol Population Functions
//-----------------------------------------------------------------------------

// The functions in this section populate structs that contain locations of necessary symbols,
// which are then passed through a BPF map to the uprobe.
// For example, locations of required struct members are communicated through this fasion.

// The following is a helper macro that is useful during debug.
// By changing VLOG to LOG, all assignments that use this macro are logged.
// Primarily used to record the symbol address and offset assignments.
#define LOG_ASSIGN(var, val)         \
  {                                  \
    var = val;                       \
    VLOG(1) << #var << " = " << var; \
  }

#define LOG_ASSIGN_STATUSOR(var, val) LOG_ASSIGN(var, val.ValueOr(-1))
#define LOG_ASSIGN_OPTIONAL(var, val) LOG_ASSIGN(var, val.value_or(-1))

namespace {

StatusOr<std::string> InferHTTP2SymAddrVendorPrefix(ElfReader* elf_reader) {
  // We now want to infer the vendor prefix directory. Use the list of symbols below as samples to
  // help infer. The iteration will stop after the first inference.
  const std::vector<std::string_view> kSampleSymbols = {
      "google.golang.org/grpc/internal/transport.(*http2Client).operateHeaders",
      "golang.org/x/net/http2/hpack.HeaderField.String",
      "golang.org/x/net/http2.(*Framer).WriteHeaders"};

  std::string vendor_prefix;
  for (std::string_view s : kSampleSymbols) {
    PX_ASSIGN_OR(std::vector<ElfReader::SymbolInfo> symbol_matches,
                 elf_reader->ListFuncSymbols(s, obj_tools::SymbolMatchType::kSuffix), continue);
    if (symbol_matches.size() > 1) {
      VLOG(1) << absl::Substitute(
          "Found multiple symbol matches for $0. Cannot infer vendor prefix.", s);
      continue;
    }
    if (!symbol_matches.empty()) {
      const auto& name = symbol_matches.front().name;
      DCHECK_GE(name.size(), s.size());
      vendor_prefix = name.substr(0, name.size() - s.size());
      break;
    }
  }

  VLOG_IF(1, !vendor_prefix.empty())
      << absl::Substitute("Inferred vendor prefix: $0", vendor_prefix);
  return vendor_prefix;
}

std::optional<int64_t> ResolveSymbolWithEachGoPrefix(ElfReader* elf_reader,
                                                     std::string_view symbol) {
  // In go version 1.20, the symbols for compiler generated types were switched from having a prefix
  // of `go.` to `go:`. See the go 1.20 release notes: https://tip.golang.org/doc/go1.20
  static constexpr std::array go_prefixes{"go.", "go:"};
  for (const auto& prefix : go_prefixes) {
    auto optional_addr = elf_reader->SymbolAddress(absl::StrCat(prefix, symbol));
    if (optional_addr != std::nullopt) {
      return optional_addr;
    }
  }
  return std::nullopt;
}

Status PopulateCommonTypeAddrs(ElfReader* elf_reader, std::string_view vendor_prefix,
                               struct go_common_symaddrs_t* symaddrs) {
  // Note: we only return error if a *mandatory* symbol is missing. Only TCPConn is mandatory.
  // Without TCPConn, the uprobe cannot resolve the FD, and becomes pointless.

  LOG_ASSIGN_OPTIONAL(
      symaddrs->internal_syscallConn,
      ResolveSymbolWithEachGoPrefix(
          elf_reader,
          absl::StrCat("itab.*", vendor_prefix,
                       "google.golang.org/grpc/credentials/internal.syscallConn,net.Conn")));
  LOG_ASSIGN_OPTIONAL(symaddrs->tls_Conn,
                      ResolveSymbolWithEachGoPrefix(elf_reader, "itab.*crypto/tls.Conn,net.Conn"));
  LOG_ASSIGN_OPTIONAL(symaddrs->net_TCPConn,
                      ResolveSymbolWithEachGoPrefix(elf_reader, "itab.*net.TCPConn,net.Conn"));

  // TODO(chengruizhe): Refer to setGStructOffsetElf in dlv for a more accurate way of setting
  // g_addr_offset using elf.
  symaddrs->g_addr_offset = -8;

  // TCPConn is mandatory by the HTTP2 uprobes probe, so bail if it is not found (-1).
  // It should be the last layer of nested interface, and contains the FD.
  // The other conns can be invalid, and will simply be ignored.
  if (symaddrs->net_TCPConn == -1) {
    return error::Internal("TCPConn not found");
  }

  return Status::OK();
}

Status PopulateCommonDebugSymbols(DwarfReader* dwarf_reader, std::string_view vendor_prefix,
                                  const std::string& go_version,
                                  const std::string& google_golang_grpc,
                                  struct go_common_symaddrs_t* symaddrs) {
  PX_UNUSED(dwarf_reader);
  PX_UNUSED(vendor_prefix);

  LOG_ASSIGN_STATUSOR(symaddrs->FD_Sysfd_offset,
                      GetStructOffset("internal/poll.FD", "Sysfd", go_version));
  LOG_ASSIGN_STATUSOR(symaddrs->tlsConn_conn_offset,
                      GetStructOffset("crypto/tls.Conn", "conn", go_version));
  LOG_ASSIGN_STATUSOR(symaddrs->syscallConn_conn_offset,
                      GetStructOffset("google.golang.org/grpc/credentials/internal.syscallConn",
                                      "conn", google_golang_grpc));
  LOG_ASSIGN_STATUSOR(symaddrs->g_goid_offset, GetStructOffset("runtime.g", "goid", go_version));

  // List mandatory symaddrs here (symaddrs without which all probes become useless).
  // Returning an error will prevent the probes from deploying.
  if (symaddrs->FD_Sysfd_offset == -1) {
    return error::Internal("FD_Sysfd_offset not found");
  }
  return Status::OK();
}

Status PopulateHTTP2TypeAddrs(ElfReader* elf_reader, std::string_view vendor_prefix,
                              struct go_http2_symaddrs_t* symaddrs) {
  // Note: we only return error if a *mandatory* symbol is missing. Only TCPConn is mandatory.
  // Without TCPConn, the uprobe cannot resolve the FD, and becomes pointless.

  LOG_ASSIGN_OPTIONAL(
      symaddrs->http_http2bufferedWriter,
      ResolveSymbolWithEachGoPrefix(elf_reader, "itab.*net/http.http2bufferedWriter,io.Writer"));
  LOG_ASSIGN_OPTIONAL(
      symaddrs->transport_bufWriter,
      ResolveSymbolWithEachGoPrefix(
          elf_reader,
          absl::StrCat("itab.*", vendor_prefix,
                       "google.golang.org/grpc/internal/transport.bufWriter,io.Writer")));

  return Status::OK();
}

Status PopulateHTTP2DebugSymbols(DwarfReader* /*dwarf_reader*/, const std::string& go_version,
                                 const std::string& vendor_prefix,
                                 const obj_tools::BuildInfo& build_info,
                                 struct go_http2_symaddrs_t* symaddrs) {
  // Note: we only return error if a *mandatory* symbol is missing. Currently none are mandatory,
  // because these multiple probes for multiple HTTP2/GRPC libraries. Even if a symbol for one
  // is missing it doesn't mean the other library's probes should not be deployed.
  std::string golang_x_net_version;
  std::string google_golang_grpc;
  for (const auto& dep : build_info.deps) {
    // Find the related dependencies and strip the "v" prefix
    if (dep.path == "golang.org/x/net") {
      golang_x_net_version = dep.version.substr(1);
    } else if (dep.path == "google.golang.org/grpc") {
      google_golang_grpc = dep.version.substr(1);
      LOG(INFO) << "Found grpc dependency: " << dep.path << " " << dep.version << " "
                << google_golang_grpc;
    }
  }

#define VENDOR_SYMBOL(symbol) absl::StrCat(vendor_prefix, symbol)

  // clang-format off
  // TODO(ddelnano): Figure out what to do with VENDOR_SYMBOL
  LOG_ASSIGN_STATUSOR(symaddrs->HeaderField_Name_offset,
                      GetStructOffset("golang.org/x/net/http2/hpack.HeaderField", "Name", golang_x_net_version));
  LOG_ASSIGN_STATUSOR(symaddrs->HeaderField_Value_offset,
                      GetStructOffset("golang.org/x/net/http2/hpack.HeaderField", "Value", golang_x_net_version));
  LOG_ASSIGN_STATUSOR(symaddrs->http2Server_conn_offset,
                      GetStructOffset("google.golang.org/grpc/internal/transport.http2Server", "conn", google_golang_grpc));

  LOG_ASSIGN_STATUSOR(symaddrs->http2Client_conn_offset,
                      GetStructOffset("google.golang.org/grpc/internal/transport.http2Client", "conn", google_golang_grpc));
  LOG_ASSIGN_STATUSOR(symaddrs->loopyWriter_framer_offset,
                      GetStructOffset("google.golang.org/grpc/internal/transport.loopyWriter", "framer", google_golang_grpc));

  LOG_ASSIGN_STATUSOR(symaddrs->Framer_w_offset,
                      GetStructOffset("golang.org/x/net/http2.Framer", "w", golang_x_net_version));

  LOG_ASSIGN_STATUSOR(symaddrs->MetaHeadersFrame_HeadersFrame_offset,
                      GetStructOffset("golang.org/x/net/http2.MetaHeadersFrame", "HeadersFrame", golang_x_net_version));
  LOG_ASSIGN_STATUSOR(symaddrs->MetaHeadersFrame_Fields_offset,
                      GetStructOffset("golang.org/x/net/http2.MetaHeadersFrame", "Fields", golang_x_net_version));
  LOG_ASSIGN_STATUSOR(symaddrs->HeadersFrame_FrameHeader_offset,
                      GetStructOffset("golang.org/x/net/http2.HeadersFrame", "FrameHeader", golang_x_net_version));
  LOG_ASSIGN_STATUSOR(symaddrs->FrameHeader_Type_offset,
                      GetStructOffset("golang.org/x/net/http2.FrameHeader", "Type", golang_x_net_version));
  LOG_ASSIGN_STATUSOR(symaddrs->FrameHeader_Flags_offset,
                      GetStructOffset("golang.org/x/net/http2.FrameHeader", "Flags", golang_x_net_version));
  LOG_ASSIGN_STATUSOR(symaddrs->FrameHeader_StreamID_offset,
                      GetStructOffset("golang.org/x/net/http2.FrameHeader", "StreamID", golang_x_net_version));
  LOG_ASSIGN_STATUSOR(symaddrs->DataFrame_data_offset,
                      GetStructOffset("golang.org/x/net/http2.DataFrame", "data", golang_x_net_version));
  LOG_ASSIGN_STATUSOR(symaddrs->bufWriter_conn_offset,
                      GetStructOffset("google.golang.org/grpc/internal/transport.bufWriter", "conn", google_golang_grpc));

  LOG_ASSIGN_STATUSOR(symaddrs->http2serverConn_conn_offset,
                      GetStructOffset("net/http.http2serverConn", "conn", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2serverConn_hpackEncoder_offset,
                      GetStructOffset("net/http.http2serverConn", "hpackEncoder", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2HeadersFrame_http2FrameHeader_offset,
                      GetStructOffset("net/http.http2HeadersFrame", "http2FrameHeader", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2FrameHeader_Type_offset,
                      GetStructOffset("net/http.http2FrameHeader", "Type", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2FrameHeader_Flags_offset,
                      GetStructOffset("net/http.http2FrameHeader", "Flags", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2FrameHeader_StreamID_offset,
                      GetStructOffset("net/http.http2FrameHeader", "StreamID", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2DataFrame_data_offset,
                      GetStructOffset("net/http.http2DataFrame", "data", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2writeResHeaders_streamID_offset,
                      GetStructOffset("net/http.http2writeResHeaders", "streamID", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2writeResHeaders_endStream_offset,
                      GetStructOffset("net/http.http2writeResHeaders", "endStream", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2MetaHeadersFrame_http2HeadersFrame_offset,
                      GetStructOffset("net/http.http2MetaHeadersFrame", "http2HeadersFrame", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2MetaHeadersFrame_Fields_offset,
                      GetStructOffset("net/http.http2MetaHeadersFrame", "Fields", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2Framer_w_offset,
                      GetStructOffset("net/http.http2Framer", "w", go_version));

  LOG_ASSIGN_STATUSOR(symaddrs->http2bufferedWriter_w_offset,
                      GetStructOffset("net/http.http2bufferedWriter", "w", go_version));
  // clang-format on

  const location_t invalid_loc = {kLocationTypeInvalid, -1};

  // Arguments of net/http.(*http2Framer).WriteDataPadded.
  {
    std::string fn = "net/http.(*http2Framer).WriteDataPadded";
    auto f_loc = GetArgOffset(fn, "f", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Framer_WriteDataPadded_f_loc, f_loc);

    auto streamID_loc = GetArgOffset(fn, "streamID", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Framer_WriteDataPadded_streamID_loc, streamID_loc);

    auto endStream_loc = GetArgOffset(fn, "endStream", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Framer_WriteDataPadded_endStream_loc, endStream_loc);

    auto data_ptr_loc = GetArgOffset(fn, "data", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Framer_WriteDataPadded_data_ptr_loc, data_ptr_loc);
    symaddrs->http2Framer_WriteDataPadded_data_ptr_loc.offset += kGoArrayPtrOffset;

    auto data_len_loc = GetArgOffset(fn, "data", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Framer_WriteDataPadded_data_len_loc, data_len_loc);
    symaddrs->http2Framer_WriteDataPadded_data_len_loc.offset += kGoArrayLenOffset;
  }

  // Arguments of golang.org/x/net/http2.(*Framer).WriteDataPadded.
  {
    std::string fn = VENDOR_SYMBOL("golang.org/x/net/http2.(*Framer).WriteDataPadded");
    LOG(INFO) << "WriteDataPadded fn: " << fn;
    // TODO(ddelnano): This offset is null and needs to be fixed
    auto f_loc = GetArgOffset(fn, "f", golang_x_net_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2_WriteDataPadded_f_loc, f_loc);

    auto streamID_loc = GetArgOffset(fn, "streamID", golang_x_net_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2_WriteDataPadded_streamID_loc, streamID_loc);

    auto endStream_loc = GetArgOffset(fn, "endStream", golang_x_net_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2_WriteDataPadded_endStream_loc, endStream_loc);

    auto data_ptr_loc = GetArgOffset(fn, "data", golang_x_net_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2_WriteDataPadded_data_ptr_loc, data_ptr_loc);
    symaddrs->http2_WriteDataPadded_data_ptr_loc.offset += kGoArrayPtrOffset;

    LOG_ASSIGN(symaddrs->http2_WriteDataPadded_data_len_loc, data_ptr_loc);
    symaddrs->http2_WriteDataPadded_data_len_loc.offset += kGoArrayLenOffset;
  }

  // Arguments of net/http.(*http2Framer).checkFrameOrder.
  {
    std::string fn = "net/http.(*http2Framer).checkFrameOrder";
    auto fr_loc = GetArgOffset(fn, "fr", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Framer_checkFrameOrder_fr_loc, fr_loc);

    auto f_loc = GetArgOffset(fn, "f", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Framer_checkFrameOrder_f_loc, f_loc);
  }

  // Arguments of golang.org/x/net/http2.(*Framer).checkFrameOrder.
  {
    std::string fn = VENDOR_SYMBOL("golang.org/x/net/http2.(*Framer).checkFrameOrder");
    auto fr_loc = GetArgOffset(fn, "fr", golang_x_net_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2_checkFrameOrder_fr_loc, fr_loc);

    auto f_loc = GetArgOffset(fn, "f", golang_x_net_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2_checkFrameOrder_f_loc, f_loc);
  }

  // Arguments of net/http.(*http2writeResHeaders).writeFrame.
  {
    std::string fn = "net/http.(*http2writeResHeaders).writeFrame";
    auto w_loc = GetArgOffset(fn, "w", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->writeFrame_w_loc, w_loc);

    auto ctx_loc = GetArgOffset(fn, "ctx", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->writeFrame_ctx_loc, ctx_loc);
  }

  // Arguments of golang.org/x/net/http2/hpack.(*Encoder).WriteField.
  {
    // TODO(ddelnano): This is vendor prefixed on the offsetgen side
    std::string fn = VENDOR_SYMBOL("vendor/golang.org/x/net/http2/hpack.(*Encoder).WriteField");
    LOG(INFO) << "WriteField fn: " << fn;
    auto write_field_e_loc = GetArgOffset(fn, "e", golang_x_net_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->WriteField_e_loc, write_field_e_loc);

    auto write_field_f_loc = GetArgOffset(fn, "f", golang_x_net_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->WriteField_f_name_loc, write_field_f_loc);
    symaddrs->WriteField_f_name_loc.offset += 0;

    LOG_ASSIGN(symaddrs->WriteField_f_value_loc, write_field_f_loc);
    symaddrs->WriteField_f_value_loc.offset += 16;
  }

  // Arguments of net/http.(*http2serverConn).processHeaders.
  {
    std::string fn = "net/http.(*http2serverConn).processHeaders";
    auto sc_loc = GetArgOffset(fn, "sc", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->processHeaders_sc_loc, sc_loc);

    auto f_loc = GetArgOffset(fn, "f", go_version).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->processHeaders_f_loc, f_loc);
  }

  // Arguments of google.golang.org/grpc/internal/transport.(*http2Server).operateHeaders.
  {
    std::string fn =
        VENDOR_SYMBOL("google.golang.org/grpc/internal/transport.(*http2Server).operateHeaders");

    auto t_loc = GetArgOffset(fn, "t", google_golang_grpc).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Server_operateHeaders_t_loc, t_loc);

    auto frame_loc = GetArgOffset(fn, "frame", google_golang_grpc).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Server_operateHeaders_frame_loc, frame_loc);
  }

  // Arguments of google.golang.org/grpc/internal/transport.(*http2Client).operateHeaders.
  {
    std::string fn =
        VENDOR_SYMBOL("google.golang.org/grpc/internal/transport.(*http2Client).operateHeaders");

    auto t_loc = GetArgOffset(fn, "t", google_golang_grpc).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Client_operateHeaders_t_loc, t_loc);

    auto frame_loc = GetArgOffset(fn, "frame", google_golang_grpc).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->http2Client_operateHeaders_frame_loc, frame_loc);
  }

  // Arguments of google.golang.org/grpc/internal/transport.(*loopyWriter).writeHeader.
  {
    // TODO(ddelnano): This offset is null and needs to be fixed
    std::string fn =
        VENDOR_SYMBOL("google.golang.org/grpc/internal/transport.(*loopyWriter).writeHeader");
    auto l_loc = GetArgOffset(fn, "l", google_golang_grpc).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->writeHeader_l_loc, l_loc);

    auto streamID_loc = GetArgOffset(fn, "streamID", google_golang_grpc).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->writeHeader_streamID_loc, streamID_loc);

    auto endStream_loc = GetArgOffset(fn, "endStream", google_golang_grpc).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->writeHeader_endStream_loc, endStream_loc);

    auto hf_loc = GetArgOffset(fn, "hf", google_golang_grpc).ValueOr(invalid_loc);
    LOG_ASSIGN(symaddrs->writeHeader_hf_ptr_loc, hf_loc);
    symaddrs->writeHeader_hf_ptr_loc.offset += kGoArrayPtrOffset;

    LOG_ASSIGN(symaddrs->writeHeader_hf_len_loc, hf_loc);
    symaddrs->writeHeader_hf_len_loc.offset += kGoArrayLenOffset;
  }

#undef VENDOR_SYMBOL

  return Status::OK();
}

}  // namespace

Status PopulateGoTLSDebugSymbols(ElfReader* elf_reader, DwarfReader* dwarf_reader,
                                 const std::string& go_version,
                                 struct go_tls_symaddrs_t* symaddrs) {
  PX_UNUSED(elf_reader);
  PX_UNUSED(dwarf_reader);

  PX_ASSIGN_OR_RETURN(SemVer sem_ver_go, GetSemVer(go_version, false));

  std::string retval0_arg = "~r1";
  std::string retval1_arg = "~r2";

  const SemVer kZeroIndexReturnValVersion{.major = 1, .minor = 18, .patch = 0};
  // Return value naming in dwarf changed since Go1.18.
  if (!(sem_ver_go < kZeroIndexReturnValVersion)) {
    retval0_arg = "~r0";
    retval1_arg = "~r1";
  }

  PX_ASSIGN_OR_RETURN(auto write_c_loc, GetArgOffset("crypto/tls.(*Conn).Write", "c", go_version));
  symaddrs->Write_c_loc.type = write_c_loc.type;
  symaddrs->Write_c_loc.offset = write_c_loc.offset;

  PX_ASSIGN_OR_RETURN(auto write_b_loc, GetArgOffset("crypto/tls.(*Conn).Write", "b", go_version));
  symaddrs->Write_b_loc.type = write_b_loc.type;
  symaddrs->Write_b_loc.offset = write_b_loc.offset;

  PX_ASSIGN_OR_RETURN(auto write_retval0_loc,
                      GetArgOffset("crypto/tls.(*Conn).Write", retval0_arg, go_version));
  symaddrs->Write_retval0_loc.type = write_retval0_loc.type;
  symaddrs->Write_retval0_loc.offset = write_retval0_loc.offset;

  PX_ASSIGN_OR_RETURN(auto write_retval1_loc,
                      GetArgOffset("crypto/tls.(*Conn).Write", retval0_arg, go_version));
  symaddrs->Write_retval1_loc.type = write_retval1_loc.type;
  symaddrs->Write_retval1_loc.offset = write_retval1_loc.offset;

  PX_ASSIGN_OR_RETURN(auto read_c_loc, GetArgOffset("crypto/tls.(*Conn).Read", "c", go_version));
  symaddrs->Read_c_loc.type = read_c_loc.type;
  symaddrs->Read_c_loc.offset = read_c_loc.offset;

  PX_ASSIGN_OR_RETURN(auto read_b_loc, GetArgOffset("crypto/tls.(*Conn).Read", "b", go_version));
  symaddrs->Read_b_loc.type = read_b_loc.type;
  symaddrs->Read_b_loc.offset = read_b_loc.offset;

  PX_ASSIGN_OR_RETURN(auto read_retval0,
                      GetArgOffset("crypto/tls.(*Conn).Read", retval0_arg, go_version));
  symaddrs->Read_retval0_loc.type = read_retval0.type;
  symaddrs->Read_retval0_loc.offset = read_retval0.offset;

  PX_ASSIGN_OR_RETURN(auto read_retval1,
                      GetArgOffset("crypto/tls.(*Conn).Read", retval1_arg, go_version));
  symaddrs->Read_retval1_loc.type = read_retval1.type;
  symaddrs->Read_retval1_loc.offset = read_retval1.offset;

  // List mandatory symaddrs here (symaddrs without which all probes become useless).
  // Returning an error will prevent the probes from deploying.
  if (symaddrs->Write_b_loc.type == kLocationTypeInvalid ||
      symaddrs->Read_b_loc.type == kLocationTypeInvalid) {
    return error::Internal("Go TLS Read/Write arguments not found.");
  }

  return Status::OK();
}

StatusOr<struct go_common_symaddrs_t> GoCommonSymAddrs(ElfReader* elf_reader,
                                                       DwarfReader* dwarf_reader,
                                                       const std::string& go_version,
                                                       const obj_tools::BuildInfo& build_info) {
  PX_UNUSED(go_version);
  PX_UNUSED(build_info);
  struct go_common_symaddrs_t symaddrs;

  PX_ASSIGN_OR_RETURN(std::string vendor_prefix, InferHTTP2SymAddrVendorPrefix(elf_reader));

  std::string google_golang_grpc;
  for (const auto& dep : build_info.deps) {
    if (dep.path == "google.golang.org/grpc") {
      google_golang_grpc = dep.version.substr(1);
    }
  }

  PX_RETURN_IF_ERROR(PopulateCommonTypeAddrs(elf_reader, vendor_prefix, &symaddrs));
  PX_RETURN_IF_ERROR(PopulateCommonDebugSymbols(dwarf_reader, vendor_prefix, go_version,
                                                google_golang_grpc, &symaddrs));

  return symaddrs;
}

StatusOr<struct go_http2_symaddrs_t> GoHTTP2SymAddrs(ElfReader* elf_reader,
                                                     DwarfReader* dwarf_reader,
                                                     const std::string& go_version,
                                                     const obj_tools::BuildInfo& build_info) {
  PX_UNUSED(go_version);
  PX_UNUSED(build_info);
  PX_UNUSED(dwarf_reader);
  PX_UNUSED(elf_reader);
  struct go_http2_symaddrs_t symaddrs;

  PX_ASSIGN_OR_RETURN(std::string vendor_prefix, InferHTTP2SymAddrVendorPrefix(elf_reader));
  PX_RETURN_IF_ERROR(PopulateHTTP2TypeAddrs(elf_reader, vendor_prefix, &symaddrs));

  PX_RETURN_IF_ERROR(
      PopulateHTTP2DebugSymbols(dwarf_reader, go_version, vendor_prefix, build_info, &symaddrs));

  return symaddrs;
}

using FuncArgMap =
    std::map<std::string,
             std::map<std::string, std::map<std::string, std::unique_ptr<location_t>>>>;
using StructOffsetMap =
    std::map<std::string, std::map<std::string, std::map<std::string, int32_t>>>;

StatusOr<struct go_tls_symaddrs_t> GoTLSSymAddrs(ElfReader* elf_reader, DwarfReader* dwarf_reader,
                                                 const std::string& go_version,
                                                 const obj_tools::BuildInfo& build_info) {
  PX_UNUSED(go_version);
  PX_UNUSED(build_info);
  struct go_tls_symaddrs_t symaddrs;

  PX_RETURN_IF_ERROR(PopulateGoTLSDebugSymbols(elf_reader, dwarf_reader, go_version, &symaddrs));

  return symaddrs;
}

namespace {

// Returns a function pointer from a dlopen handle.
template <class T>
StatusOr<T*> DLSymbolToFptr(void* handle, const std::string& symbol_name) {
  // The templated form compares nicely to c-style function pointer typedefs.
  // Example usage:
  // auto myFunction = DLSymbolToFptr<int (FooQux &, const Baz *)>( h, "somesymbol");
  T* fptr = reinterpret_cast<T*>(dlsym(handle, symbol_name.c_str()));

  const char* dlsym_error = dlerror();
  if (dlsym_error) {
    return error::Internal("Failed to find symbol: $0, $1", symbol_name, dlsym_error);
  }

  return fptr;
}

StatusOr<uint64_t> GetOpenSSLVersionNumUsingDLOpen(const std::filesystem::path& lib_openssl_path) {
  if (!fs::Exists(lib_openssl_path)) {
    return error::Internal("Path to OpenSSL so is not valid: $0", lib_openssl_path.string());
  }

  void* h = dlopen(lib_openssl_path.c_str(), RTLD_LAZY);

  if (h == nullptr) {
    return error::Internal("Failed to dlopen OpenSSL so file: $0, $1", lib_openssl_path.string(),
                           dlerror());
  }
  DEFER(dlclose(h));

  const std::string version_num_symbol = "OpenSSL_version_num";

  // NOLINTNEXTLINE(runtime/int): 'unsigned long' is from upstream, match that here (vs. uint64_t)
  PX_ASSIGN_OR_RETURN(auto version_num_f, DLSymbolToFptr<unsigned long()>(h, version_num_symbol));

  const uint64_t version_num = version_num_f();
  return version_num;
}

StatusOr<uint64_t> GetOpenSSLVersionNumUsingFptr(RawFptrManager* fptr_manager) {
  const std::string symbol = "OpenSSL_version_num";
  // NOLINTNEXTLINE(runtime/int): 'unsigned long' is from upstream, match that here (vs. uint64_t)
  PX_ASSIGN_OR_RETURN(auto version_num_f, fptr_manager->RawSymbolToFptr<unsigned long()>(symbol));
  return version_num_f();
}

// Returns the "fix" version number for OpenSSL.
StatusOr<uint32_t> OpenSSLFixSubversionNum(RawFptrManager* fptrManager,
                                           const std::filesystem::path& lib_openssl_path,
                                           uint32_t pid) {
  // Current use case:
  // switch for the correct number of bytes offset for the socket fd.
  //
  // Basic version number format: "major.minor.fix".
  // In more detail:
  // MNNFFPPS: major minor fix patch status
  // From https://www.openssl.org/docs/man1.1.1/man3/OPENSSL_VERSION_NUMBER.html.
  union open_ssl_version_num_t {
    struct __attribute__((packed)) {
      uint32_t status : 4;
      uint32_t patch : 8;
      uint32_t fix : 8;
      uint32_t minor : 8;
      uint32_t major : 8;
      uint32_t unused : 64 - (4 + 4 * 8);
    };  // NOLINT(readability/braces) False claim that ';' is unnecessary.
    uint64_t packed;
  };
  open_ssl_version_num_t version_num;

  StatusOr<uint64_t> openssl_version_packed = GetOpenSSLVersionNumUsingDLOpen(lib_openssl_path);
  if (FLAGS_openssl_force_raw_fptrs ||
      (FLAGS_openssl_raw_fptrs_enabled && !openssl_version_packed.ok())) {
    LOG(WARNING) << absl::Substitute(
        "Unable to find openssl symbol 'OpenSSL_version_num' using dlopen/dlsym. Attempting to "
        "find address manually for pid $0",
        pid);
    openssl_version_packed = GetOpenSSLVersionNumUsingFptr(fptrManager);

    if (!openssl_version_packed.ok())
      LOG(WARNING) << absl::StrFormat(
          "Unable to find openssl symbol 'OpenSSL_version_num' with raw function pointer: %s",
          openssl_version_packed.ToString());
  }
  PX_ASSIGN_OR_RETURN(version_num.packed, openssl_version_packed);

  const uint32_t major = version_num.major;
  const uint32_t minor = version_num.minor;
  const uint32_t fix = version_num.fix;

  VLOG(1) << absl::StrFormat("Found OpenSSL version: 0x%016lx (%d.%d.%d:%x.%x), %s",
                             version_num.packed, major, minor, fix, version_num.patch,
                             version_num.status, lib_openssl_path.string());

  constexpr uint32_t min_fix_version = 0;
  constexpr uint32_t max_fix_version = 1;

  if (major != 1) {
    return error::Internal("Unsupported OpenSSL major version: $0.$1.$2", major, minor, fix);
  }
  if (minor != 1) {
    return error::Internal("Unsupported OpenSSL minor version: $0.$1.$2", major, minor, fix);
  }
  if (fix != std::clamp(fix, min_fix_version, max_fix_version)) {
    return error::Internal("Unsupported OpenSSL fix version: $0.$1.$2", major, minor, fix);
  }
  return fix;
}

}  // namespace

// Used for determining if a given tracing target is using
// borginssl or not. At this time it's difficult to generalize this
// to other use cases until more boringssl integrations are made. This
// interface will likely change as we learn more about other ssl library
// integrations.
bool IsBoringSSL(const std::filesystem::path& openssl_lib) {
  if (absl::StrContains(openssl_lib.string(), kLibNettyTcnativePrefix)) {
    return true;
  }

  return false;
}

StatusOr<struct openssl_symaddrs_t> OpenSSLSymAddrs(RawFptrManager* fptrManager,
                                                    const std::filesystem::path& openssl_lib,
                                                    uint32_t pid) {
  // Some useful links, for different OpenSSL versions:
  // 1.1.0a:
  // https://github.com/openssl/openssl/blob/ac2c44c6289f9716de4c4beeb284a818eacde517/<filename>
  // 1.1.0l:
  // https://github.com/openssl/openssl/blob/7ea5bd2b52d0e81eaef3d109b3b12545306f201c/<filename>
  // 1.1.1a:
  // https://github.com/openssl/openssl/blob/d1c28d791a7391a8dc101713cd8646df96491d03/<filename>
  // 1.1.1e:
  // https://github.com/openssl/openssl/blob/a61eba4814fb748ad67e90e81c005ffb09b67d3d/<filename>

  // Offset of rbio in struct ssl_st.
  // Struct is defined in ssl/ssl_local.h, ssl/ssl_locl.h, ssl/ssl_lcl.h, depending on the version.
  // Verified to be valid for following versions:
  //  - 1.1.0a to 1.1.0k
  //  - 1.1.1a to 1.1.1e
  constexpr int32_t kSSL_RBIO_offset = 0x10;

  // BoringSSL was originally derived from OpenSSL 1.0.2. For now we
  // are only supporting the offsets of its current release, which tracks
  // OpenSSL 1.1.0 at this time (Sept 2022). See it's PORTING.md doc for
  // more details.
  // https://github.com/google/boringssl/blob/0cc51a793eef6b5295b9e0de8aafb1d87a39e210/PORTING.md
  //
  // TODO(yzhao): Determine the offsets for BoringSSL's openssl 1.0.x compatibility.
  // See https://github.com/pixie-io/pixie/issues/588 for more details.
  //
  // These were calculated from compiling libnetty_tcnative locally
  // with symbols and attaching gdb to walk the data structures.
  // https://pixie-community.slack.com/files/U027UA1MRPA/F03FA23U8FQ/untitled.txt
  constexpr int32_t kBoringSSL_RBIO_offset = 0x18;
  constexpr int32_t kBoringSSL_1_1_1_RBIO_num_offset = 0x18;

  // Offset of num in struct bio_st.
  // Struct is defined in crypto/bio/bio_lcl.h, crypto/bio/bio_local.h depending on the version.
  //  - In 1.1.1a to 1.1.1e, the offset appears to be 0x30
  //  - In 1.1.0, the value appears to be 0x28.
  constexpr int32_t kOpenSSL_1_1_0_RBIO_num_offset = 0x28;
  constexpr int32_t kOpenSSL_1_1_1_RBIO_num_offset = 0x30;

  struct openssl_symaddrs_t symaddrs;
  symaddrs.SSL_rbio_offset = kSSL_RBIO_offset;

  PX_ASSIGN_OR_RETURN(uint32_t openssl_fix_sub_version,
                      OpenSSLFixSubversionNum(fptrManager, openssl_lib, pid));

  if (!IsBoringSSL(openssl_lib)) {
    switch (openssl_fix_sub_version) {
      case 0:
        symaddrs.RBIO_num_offset = kOpenSSL_1_1_0_RBIO_num_offset;
        break;
      case 1:
        symaddrs.RBIO_num_offset = kOpenSSL_1_1_1_RBIO_num_offset;
        break;
      default:
        // Supported versions are checked in function OpenSSLFixSubversionNum(),
        // should not fall through to here, ever.
        DCHECK(false);
        return error::Internal("Unsupported openssl_fix_sub_version: $0", openssl_fix_sub_version);
    }
  } else {
    switch (openssl_fix_sub_version) {
      case 1:
        symaddrs.RBIO_num_offset = kBoringSSL_1_1_1_RBIO_num_offset;
        symaddrs.SSL_rbio_offset = kBoringSSL_RBIO_offset;
        break;
      default:
        // Supported versions are checked in function OpenSSLFixSubversionNum(),
        // should not fall through to here, ever.
        DCHECK(false);
        return error::Internal("Unsupported openssl_fix_sub_version: $0", openssl_fix_sub_version);
    }
  }

  // Using GDB to confirm member offsets on OpenSSL 1.1.1:
  // (gdb) p s
  // $18 = (SSL *) 0x55ea646953c0
  // (gdb) p &s.rbio
  // $22 = (BIO **) 0x55ea646953d0
  // (gdb) p s.rbio
  // $23 = (BIO *) 0x55ea64698b10
  // (gdb) p &s.rbio.num
  // $24 = (int *) 0x55ea64698b40
  // (gdb) p s.rbio.num
  // $25 = 3
  // (gdb) p &s.wbio
  // $26 = (BIO **) 0x55ea646953d8
  // (gdb) p s.wbio
  // $27 = (BIO *) 0x55ea64698b10
  // (gdb) p &s.wbio.num
  // $28 = (int *) 0x55ea64698b40
  // (gdb) p s.wbio.num
  return symaddrs;
}

// Instructions of get symbol offsets for nodejs.
//   git clone nodejs repo.
//   git checkout v<version>  # Checkout the tagged release
//   ./configure --debug && make -j8  # build the debug version
//   sudo out/Debug/node src/stirling/.../containers/ssl/https_server.js
//   Launch stirling_wrapper, log the output of NodeTLSWrapSymAddrsFromDwarf() from inside
//   UProbeManager::UpdateNodeTLSWrapSymAddrs().
constexpr struct node_tlswrap_symaddrs_t kNodeSymaddrsV12_3_1 = {
    .TLSWrap_StreamListener_offset = 0x0130,
    .StreamListener_stream_offset = 0x08,
    .StreamBase_StreamResource_offset = 0x00,
    .LibuvStreamWrap_StreamBase_offset = 0x50,
    .LibuvStreamWrap_stream_offset = 0x90,
    .uv_stream_s_io_watcher_offset = 0x88,
    .uv__io_s_fd_offset = 0x30,
};

constexpr struct node_tlswrap_symaddrs_t kNodeSymaddrsV12_16_2 = {
    .TLSWrap_StreamListener_offset = 0x138,
    .StreamListener_stream_offset = 0x08,
    .StreamBase_StreamResource_offset = 0x00,
    .LibuvStreamWrap_StreamBase_offset = 0x58,
    .LibuvStreamWrap_stream_offset = 0x98,
    .uv_stream_s_io_watcher_offset = 0x88,
    .uv__io_s_fd_offset = 0x30,
};

constexpr struct node_tlswrap_symaddrs_t kNodeSymaddrsV13_0_0 = {
    .TLSWrap_StreamListener_offset = 0x130,
    .StreamListener_stream_offset = 0x8,
    .StreamBase_StreamResource_offset = 0x00,
    .LibuvStreamWrap_StreamBase_offset = 0x50,
    .LibuvStreamWrap_stream_offset = 0x90,
    .uv_stream_s_io_watcher_offset = 0x88,
    .uv__io_s_fd_offset = 0x30,
};

constexpr struct node_tlswrap_symaddrs_t kNodeSymaddrsV13_2_0 = {
    .TLSWrap_StreamListener_offset = 0x138,
    .StreamListener_stream_offset = 0x08,
    .StreamBase_StreamResource_offset = 0x00,
    .LibuvStreamWrap_StreamBase_offset = 0x58,
    .LibuvStreamWrap_stream_offset = 0x98,
    .uv_stream_s_io_watcher_offset = 0x88,
    .uv__io_s_fd_offset = 0x30,
};

constexpr struct node_tlswrap_symaddrs_t kNodeSymaddrsV13_10_1 = {
    .TLSWrap_StreamListener_offset = 0x140,
    .StreamListener_stream_offset = 0x8,
    .StreamBase_StreamResource_offset = 0x00,
    .LibuvStreamWrap_StreamBase_offset = 0x60,
    .LibuvStreamWrap_stream_offset = 0xa0,
    .uv_stream_s_io_watcher_offset = 0x88,
    .uv__io_s_fd_offset = 0x30,
};

constexpr struct node_tlswrap_symaddrs_t kNodeSymaddrsV14_5_0 = {
    .TLSWrap_StreamListener_offset = 0x138,
    .StreamListener_stream_offset = 0x08,
    .StreamBase_StreamResource_offset = 0x00,
    .LibuvStreamWrap_StreamBase_offset = 0x58,
    .LibuvStreamWrap_stream_offset = 0x98,
    .uv_stream_s_io_watcher_offset = 0x88,
    .uv__io_s_fd_offset = 0x30,
};

// This works for version from 15.0 to 16.9 as tested. Versions newer than 16.9 should still be
// compatible, but requires testing.
constexpr struct node_tlswrap_symaddrs_t kNodeSymaddrsV15_0_0 = {
    .TLSWrap_StreamListener_offset = 0x78,
    .StreamListener_stream_offset = 0x08,
    .StreamBase_StreamResource_offset = 0x00,
    .LibuvStreamWrap_StreamBase_offset = 0x58,
    .LibuvStreamWrap_stream_offset = 0x98,
    .uv_stream_s_io_watcher_offset = 0x88,
    .uv__io_s_fd_offset = 0x30,
};

StatusOr<struct node_tlswrap_symaddrs_t> NodeTLSWrapSymAddrsFromVersion(const SemVer& ver) {
  LOG(INFO) << "Getting symbol offsets for version: " << ver.ToString();
  static const std::map<SemVer, struct node_tlswrap_symaddrs_t> kNodeVersionSymaddrs = {
      {SemVer{12, 3, 1}, kNodeSymaddrsV12_3_1},   {SemVer{12, 16, 2}, kNodeSymaddrsV12_16_2},
      {SemVer{13, 0, 0}, kNodeSymaddrsV13_0_0},   {SemVer{13, 2, 0}, kNodeSymaddrsV13_2_0},
      {SemVer{13, 10, 1}, kNodeSymaddrsV13_10_1}, {SemVer{14, 5, 0}, kNodeSymaddrsV14_5_0},
      {SemVer{15, 0, 0}, kNodeSymaddrsV15_0_0},
  };
  auto iter = Floor(kNodeVersionSymaddrs, ver);
  if (iter == kNodeVersionSymaddrs.end()) {
    return error::NotFound("Found no symbol offsets for version '$0'", ver.ToString());
  }
  return iter->second;
}

StatusOr<struct node_tlswrap_symaddrs_t> NodeTLSWrapSymAddrsFromDwarf(DwarfReader* dwarf_reader) {
  struct node_tlswrap_symaddrs_t symaddrs = {};

  PX_ASSIGN_OR_RETURN(symaddrs.TLSWrap_StreamListener_offset,
                      dwarf_reader->GetClassParentOffset("TLSWrap", "StreamListener"));

  PX_ASSIGN_OR_RETURN(symaddrs.StreamListener_stream_offset,
                      dwarf_reader->GetClassMemberOffset("StreamListener", "stream_"));

  PX_ASSIGN_OR_RETURN(symaddrs.StreamBase_StreamResource_offset,
                      dwarf_reader->GetClassParentOffset("StreamBase", "StreamResource"));

  PX_ASSIGN_OR_RETURN(symaddrs.LibuvStreamWrap_StreamBase_offset,
                      dwarf_reader->GetClassParentOffset("LibuvStreamWrap", "StreamBase"));

  PX_ASSIGN_OR_RETURN(symaddrs.LibuvStreamWrap_stream_offset,
                      dwarf_reader->GetClassMemberOffset("LibuvStreamWrap", "stream_"));

  PX_ASSIGN_OR_RETURN(symaddrs.uv_stream_s_io_watcher_offset,
                      dwarf_reader->GetStructMemberOffset("uv_stream_s", "io_watcher"));

  PX_ASSIGN_OR_RETURN(symaddrs.uv__io_s_fd_offset,
                      dwarf_reader->GetStructMemberOffset("uv__io_s", "fd"));

  return symaddrs;
}

StatusOr<struct node_tlswrap_symaddrs_t> NodeTLSWrapSymAddrs(const std::filesystem::path& node_exe,
                                                             const SemVer& ver) {
  // Indexing is disabled, because nodejs has 700+MB debug info file, and it takes >100 seconds to
  // index them.
  //
  // TODO(yzhao): We can implement "selective caching". The input needs to be a collection of symbol
  // patterns, which means only indexing the matched symbols.
  auto dwarf_reader_or = DwarfReader::CreateWithoutIndexing(node_exe);

  // Creation might fail if source language cannot be detected, which means that there is no dwarf
  // info.
  if (dwarf_reader_or.ok()) {
    auto symaddrs_or = NodeTLSWrapSymAddrsFromDwarf(dwarf_reader_or.ValueOrDie().get());
    if (symaddrs_or.ok()) {
      return symaddrs_or.ConsumeValueOrDie();
    }
  }

  // Try to lookup hard-coded symbol offsets with version.
  auto symaddrs_or = NodeTLSWrapSymAddrsFromVersion(ver);
  if (symaddrs_or.ok()) {
    return symaddrs_or.ConsumeValueOrDie();
  }

  return error::NotFound("Nodejs version cannot be older than 12.3.1, got '$0'", ver.ToString());
}

std::map<std::string, std::map<std::string, std::map<std::string, int32_t>>>&
GetStructsOffsetMap() {
  return g_structsOffsetMap;
}

std::map<std::string, std::map<std::string, std::map<std::string, std::unique_ptr<location_t>>>>&
GetFuncsLocationMap() {
  return g_funcsLocationMap;
}

}  // namespace stirling
}  // namespace px
