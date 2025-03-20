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

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include "src/common/base/base.h"
#include "src/common/base/env.h"
#include "src/common/json/json.h"
#include "src/stirling/obj_tools/dwarf_reader.h"
#include "src/stirling/source_connectors/socket_tracer/uprobe_symaddrs.h"

using px::StatusOr;
using px::stirling::obj_tools::ArgInfo;
using px::stirling::obj_tools::DwarfReader;
using px::stirling::obj_tools::ElfReader;
using px::utils::ToJSONString;

//-----------------------------------------------------------------------------
// This utility is designed to isolate parsing the debug symbols of a Go binary. This
// verifies that the go version detection code is functioning as well. This is useful
// for debugging when the Go elf/DWARF parsing is not working correctly and has been the
// source of a few PEM crashes (gh#1300, gh#1646). This makes it easy for asking end users to run
// against their binaries when they are sensitive (proprietary) and we can't debug them ourselves.
//-----------------------------------------------------------------------------

/* DEFINE_string(binary, "", "The binary to parse. Required argument"); */
/* DEFINE_string(func_names, "", "Comma separated list of function args to parse. Required argument. Example: foo,bar"); */
/* DEFINE_string(output_pb_file, "", "File path for uprobepb protobuf output. If empty, arg details will be printed to stdout."); */
/* DEFINE_bool(json_output, true, "Whether to use JSON output when printing to stdout"); */

// Global maps for "structs" offsets & location data:
static std::map<std::string,
    std::map<std::string,
        std::map<std::string, int32_t>
    >
> g_structsOffsetMap;

static std::map<std::string,
    std::map<std::string,
        std::map<std::string, std::unique_ptr<location_t>>
    >
> g_funcsLocationMap;

#include <string>
#include <cstdint>

location_type_t parseLocationType(const std::string& loc) {
    if (loc == "stack") {
        return kLocationTypeStack;
    } else if (loc == "registers") {
        return kLocationTypeRegisters;
    }
    return kLocationTypeInvalid;
}

/**
 * parseOffsetOnly:
 *   - For a JSON leaf representing a struct's offset (where we ignore location):
 *     - If `null`, return -1
 *     - If a bare integer, return that
 *     - If an object with {offset:N}, return N
 *     - Otherwise, return -1
 */
int32_t parseOffsetOnly(const rapidjson::Value& val) {
    if (val.IsNull()) {
        return -1;
    }
    else if (val.IsNumber()) {
        // bare integer
        int64_t bigVal = val.GetInt64();
        return static_cast<int32_t>(bigVal); // watch for overflow
    }
    else if (val.IsObject()) {
        // e.g. { "location": "...", "offset": 42 }
        const auto& obj = val.GetObject();
        if (obj.HasMember("offset") && obj["offset"].IsNumber()) {
            int64_t bigVal = obj["offset"].GetInt64();
            return static_cast<int32_t>(bigVal);
        } else {
            return -1;
        }
    }
    // If it's a string, array, etc., treat it as unknown => -1
    return -1;
}

/**
 * parseOffsetAndLocation:
 *   - For a JSON leaf representing a func's offset & location:
 *     - offsetOut => -1 if null; store the integer or the object's "offset" otherwise
 *     - locOut => nullptr if null, otherwise a valid location_t pointer
 */
void parseOffsetAndLocation(const rapidjson::Value& val,
                            int32_t& offsetOut,
                            std::unique_ptr<location_t>& locOut)
{
    offsetOut = -1;
    locOut.reset(); // ensures nullptr

    if (val.IsNull()) {
        // offset = -1, loc => nullptr
        return;
    }
    else if (val.IsNumber()) {
        // bare integer
        int64_t bigVal = val.GetInt64();
        offsetOut = static_cast<int32_t>(bigVal);

        auto tmp = std::make_unique<location_t>();
        tmp->type = kLocationTypeInvalid; // no explicit location
        tmp->offset = offsetOut;
        locOut = std::move(tmp);
    }
    else if (val.IsObject()) {
        // e.g. { "location":"stack", "offset": 42 }
        const auto& obj = val.GetObject();

        int32_t tmpOffset = -1;
        if (obj.HasMember("offset") && obj["offset"].IsNumber()) {
            int64_t bigVal = obj["offset"].GetInt64();
            tmpOffset = static_cast<int32_t>(bigVal);
        }
        location_type_t t = kLocationTypeInvalid;
        if (obj.HasMember("location") && obj["location"].IsString()) {
            t = parseLocationType(obj["location"].GetString());
        }

        offsetOut = tmpOffset;
        auto tmp = std::make_unique<location_t>();
        tmp->type = t;
        tmp->offset = tmpOffset;
        locOut = std::move(tmp);
    }
    // else string/array => offsetOut=-1, loc=nullptr
}

/**
 * parseStructsObject:
 *   - The RapidJSON value is doc["structs"], an object
 *   - For each "structName" -> "fieldName" -> "versionKey"
 *     parse a single offset ( -1 if null or unknown )
 */
void parseStructsObject(const rapidjson::Value& structsVal)
{
    for (auto itr = structsVal.MemberBegin(); itr != structsVal.MemberEnd(); ++itr) {
        std::string structName = itr->name.GetString();
        if (!itr->value.IsObject()) {
            continue;
        }
        const auto& fieldMapObj = itr->value.GetObject(); // e.g. "data", "Flags", etc.

        for (auto fieldIt = fieldMapObj.MemberBegin(); fieldIt != fieldMapObj.MemberEnd(); ++fieldIt) {
            std::string fieldName = fieldIt->name.GetString();
            if (!fieldIt->value.IsObject()) {
                continue;
            }
            const auto& versionsObj = fieldIt->value.GetObject();

            for (auto verIt = versionsObj.MemberBegin(); verIt != versionsObj.MemberEnd(); ++verIt) {
                std::string versionKey = verIt->name.GetString();
                int32_t off = parseOffsetOnly(verIt->value);
                g_structsOffsetMap[structName][fieldName][versionKey] = off;
            }
        }
    }
}

/**
 * parseFuncsObject:
 *   - The RapidJSON value is doc["funcs"], an object
 *   - For each "funcName" -> "argName" -> "versionKey"
 *     parse offset and location data
 */
void parseFuncsObject(const rapidjson::Value& funcsVal)
{
    for (auto itr = funcsVal.MemberBegin(); itr != funcsVal.MemberEnd(); ++itr) {
        std::string funcName = itr->name.GetString();
        if (!itr->value.IsObject()) {
            continue;
        }
        const auto& argMapObj = itr->value.GetObject(); // e.g. "data", "Flags", etc.

        for (auto argIt = argMapObj.MemberBegin(); argIt != argMapObj.MemberEnd(); ++argIt) {
            std::string argName = argIt->name.GetString();
            if (!argIt->value.IsObject()) {
                continue;
            }
            const auto& versionsObj = argIt->value.GetObject();

            for (auto verIt = versionsObj.MemberBegin(); verIt != versionsObj.MemberEnd(); ++verIt) {
                std::string versionKey = verIt->name.GetString();

                int32_t offsetVal = -1;
                std::unique_ptr<location_t> locPtr;
                parseOffsetAndLocation(verIt->value, offsetVal, locPtr);

                g_funcsLocationMap[funcName][argName][versionKey] = std::move(locPtr);
            }
        }
    }
}


/* int main(int argc, char** argv) { */
/*   px::EnvironmentGuard env_guard(&argc, argv); */

/*   if (FLAGS_binary.empty() || FLAGS_func_names.empty()) { */
/*     LOG(FATAL) << absl::Substitute("Expected --binary and --func_names arguments to be provided. Instead received $0", */
/*                                    *argv); */
/*   } */

/*   StatusOr<std::unique_ptr<ElfReader>> elf_reader_status = ElfReader::Create(FLAGS_binary); */
/*   if (!elf_reader_status.ok()) { */
/*     LOG(WARNING) << absl::Substitute( */
/*         "Failed to parse elf binary $0 with" */
/*         "Message = $1", */
/*         FLAGS_binary, elf_reader_status.msg()); */
/*   } */
/*   std::unique_ptr<ElfReader> elf_reader = elf_reader_status.ConsumeValueOrDie(); */

/*   StatusOr<std::unique_ptr<DwarfReader>> dwarf_reader_status = */
/*       DwarfReader::CreateIndexingAll(FLAGS_binary); */
/*   if (!dwarf_reader_status.ok()) { */
/*     VLOG(1) << absl::Substitute( */
/*         "Failed to get binary $0 debug symbols. " */
/*         "Message = $1", */
/*         FLAGS_binary, dwarf_reader_status.msg()); */
/*   } */
/*   std::unique_ptr<DwarfReader> dwarf_reader = dwarf_reader_status.ConsumeValueOrDie(); */

/*   /1* std::map<std::string, FuncArgsAndRets> args; *1/ */
/*   auto only_print = FLAGS_output_pb_file.empty(); */
/*   for (const auto& func_name : absl::StrSplit(FLAGS_func_names, ',')) { */
/*     if (func_name.empty()) { */
/*       LOG(FATAL) << absl::Substitute("Empty function name provided in --func_names: $0", FLAGS_func_names); */
/*     } */
/*     auto args_or_status = dwarf_reader->GetFunctionArgInfo(func_name); */

/*     if (!args_or_status.ok()) { */
/*       LOG(ERROR) << absl::Substitute("debug symbol parsing failed with: $0", args_or_status.msg()); */
/*     } */
/*     auto args = args_or_status.ConsumeValueOrDie(); */
/*     if (only_print) { */
/*       if (!FLAGS_json_output) { */
/*         for (const auto& [name, arg] : args) { */
/*           LOG(INFO) << absl::Substitute("Arg $0: $1", name, arg.ToString()); */
/*         } */
/*       } else { */
/*         PrintArgsAsJSON(args); */
/*       } */
/*     } */
/*   } */

/*   if (only_print) { */
/*     return 0; */
/*   } */

/*   std::fstream outfile(FLAGS_output_pb_file, std::ios::out | std::ios::trunc | std::ios::binary); */
/*   if (!outfile.is_open()) { */
/*     char const* const err_msg = "Failed to open output file: $0."; */
/*     LOG(ERROR) << absl::Substitute(err_msg, FLAGS_output_pb_file); */
/*   } */

/*   /1* if (!pprof_pb.SerializeToOstream(&outfile)) { *1/ */
/*   /1*   char const* const err_msg = "Failed to write pprof protobuf to file: $0."; *1/ */
/*   /1*   LOG(error) << absl::Substitute(err_msg, FLAGS_pprof_pb_file); *1/ */
/*   /1* } *1/ */
/* } */

int main(int argc, char** argv) 
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.json>\n";
        return 1;
    }

    // 1) Read the entire file
    std::ifstream ifs(argv[1]);
    if (!ifs) {
        std::cerr << "Could not open file: " << argv[1] << "\n";
        return 1;
    }
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string jsonContent = buffer.str();

    // 2) Parse with RapidJSON
    rapidjson::Document doc;
    rapidjson::ParseResult parseRes = doc.Parse(jsonContent.c_str());
    LOG(INFO) << "jsonContent: " << jsonContent.c_str();
    if (!parseRes) {
        std::cerr << "JSON parse error: "
                  << rapidjson::GetParseError_En(parseRes.Code())
                  << " at offset " << parseRes.Offset() << "\n";
        return 1;
    }

    // 3) We expect top-level "structs" and "funcs". Parse each if present:
    if (doc.HasMember("structs") && doc["structs"].IsObject()) {
        parseStructsObject(doc["structs"]);
    }
    if (doc.HasMember("funcs") && doc["funcs"].IsObject()) {
        parseFuncsObject(doc["funcs"]);
    }

    // 4) Now we have data in:
    //    g_structsOffsetMap, g_structsLocationMap,
    //    g_funcsLocationMap

    // Example usage: 
    int32_t offVal = g_structsOffsetMap["golang.org/x/net/http2.DataFrame"]["data"]["0.2.0"];
    std::cout << "DataFrame.data.0.2.0 => offset = " << offVal << "\n";

    {
        LOG(INFO) << "Assertion with null struct offset";
        int32_t offNullVal = g_structsOffsetMap["golang.org/x/net/http2.DataFrame"]["data"]["0.1.0"];
        assert(offNullVal == -1 && "Expected -1 for null offset");
    }

    {
        LOG(INFO) << "Assertion with existing struct offset";
        int32_t offNullVal = g_structsOffsetMap["golang.org/x/net/http2.FrameHeader"]["Flags"]["0.1.0"];
        assert(offNullVal == 2 && "Expected 2 for null offset");
    }

    {
        LOG(INFO) << "Assertion with null func arg";
        auto data_loc = std::move(g_funcsLocationMap["golang.org/x/net/http2.(*Framer).WriteDataPadded"]["data"]["0.1.0"]);
        assert(data_loc.get() == nullptr && "Expected null pointer");
        /* assert(data_loc->offset == -1 && "Expected -1 for null offset"); */
        /* assert(data_loc->type == nullptr && "Expected nullptr"); */
    }

    {
        LOG(INFO) << "Assertion with existing func arg";
        auto c_loc = std::move(g_funcsLocationMap["crypto/tls.(*Conn).Read"]["c"]["1.19.0"]);
        assert(c_loc.get() != nullptr && "Expected non-null pointer");
        assert(c_loc->offset == 0 && "Expected 0 for null offset");
        assert(c_loc->type == kLocationTypeRegisters && "Expected kLocationTypeRegisters");

        auto b_loc = std::move(g_funcsLocationMap["crypto/tls.(*Conn).Read"]["b"]["1.19.0"]);
        assert(b_loc.get() != nullptr && "Expected non-null pointer");
        assert(b_loc->offset == 8 && "Expected 8 for null offset");
        assert(b_loc->type == kLocationTypeRegisters && "Expected kLocationTypeRegisters");
    }

    return 0;
}
