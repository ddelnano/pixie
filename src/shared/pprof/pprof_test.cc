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

#include "src/shared/pprof/pprof.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <absl/strings/str_format.h>
#include <absl/strings/str_join.h>
#include <google/protobuf/text_format.h>

#include "src/common/base/file.h"
#include "src/common/testing/testing.h"

namespace px {
namespace shared {

using ::testing::HasSubstr;

TEST(ParseLegacyHeapProfile, ParsesHeaderAndSamples) {
  const std::string legacy_text = R"(heap profile:    2: 100 [   2: 100] @ growth
     1: 50 [     1: 50] @ 0x1000 0x2000 0x3000
     1: 50 [     1: 50] @ 0x4000 0x5000
)";

  auto result = ParseLegacyHeapProfile(legacy_text);
  ASSERT_OK(result);

  auto profile = result.ConsumeValueOrDie();

  // Check sample types
  ASSERT_EQ(profile.sample_type_size(), 2);
  EXPECT_EQ(profile.string_table(profile.sample_type(0).type()), "alloc_objects");
  EXPECT_EQ(profile.string_table(profile.sample_type(0).unit()), "count");
  EXPECT_EQ(profile.string_table(profile.sample_type(1).type()), "alloc_space");
  EXPECT_EQ(profile.string_table(profile.sample_type(1).unit()), "bytes");

  // Check samples (should have 2 samples)
  ASSERT_EQ(profile.sample_size(), 2);

  // First sample: 1 allocation, 50 bytes, 3 locations
  EXPECT_EQ(profile.sample(0).value(0), 1);
  EXPECT_EQ(profile.sample(0).value(1), 50);
  EXPECT_EQ(profile.sample(0).location_id_size(), 3);

  // Second sample: 1 allocation, 50 bytes, 2 locations
  EXPECT_EQ(profile.sample(1).value(0), 1);
  EXPECT_EQ(profile.sample(1).value(1), 50);
  EXPECT_EQ(profile.sample(1).location_id_size(), 2);

  // Check locations
  ASSERT_GE(profile.location_size(), 5);  // At least 5 unique addresses
}

TEST(ParseProcMaps, ParsesMapsFormat) {
  const std::string maps_content = R"(57ab3a2e7000-57ab3c8ad000 r--p 00000000 00:00 653129 /path/to/binary
57ab3c8ad000-57ab41287000 r-xp 025c5000 00:00 653129 /path/to/binary
7b90c72c1000-7b90c72e7000 r--p 00000000 00:00 535351 /lib/x86_64-linux-gnu/libc.so.6
7ffe1d032000-7ffe1d0a8000 rw-p 00000000 00:00 0 [stack]
)";

  auto result = ParseProcMaps(maps_content);
  ASSERT_OK(result);

  auto mappings = result.ConsumeValueOrDie();
  ASSERT_EQ(mappings.size(), 4);

  // Check first mapping
  EXPECT_EQ(mappings[0].vmem_start, 0x57ab3a2e7000ULL);
  EXPECT_EQ(mappings[0].vmem_end, 0x57ab3c8ad000ULL);
  EXPECT_EQ(mappings[0].permissions, "r--p");
  EXPECT_EQ(mappings[0].file_offset, 0);
  EXPECT_EQ(mappings[0].pathname, "/path/to/binary");

  // Check second mapping (executable section with offset)
  EXPECT_EQ(mappings[1].vmem_start, 0x57ab3c8ad000ULL);
  EXPECT_EQ(mappings[1].vmem_end, 0x57ab41287000ULL);
  EXPECT_EQ(mappings[1].permissions, "r-xp");
  EXPECT_EQ(mappings[1].file_offset, 0x025c5000);
  EXPECT_EQ(mappings[1].pathname, "/path/to/binary");

  // Check stack mapping (no pathname, special name)
  EXPECT_EQ(mappings[3].pathname, "[stack]");
}

TEST(MemoryMapping, ContainsAndConversion) {
  MemoryMapping mapping;
  mapping.vmem_start = 0x1000;
  mapping.vmem_end = 0x2000;
  mapping.file_offset = 0x100;

  // Test Contains
  EXPECT_TRUE(mapping.Contains(0x1000));
  EXPECT_TRUE(mapping.Contains(0x1500));
  EXPECT_TRUE(mapping.Contains(0x1FFF));
  EXPECT_FALSE(mapping.Contains(0x0FFF));
  EXPECT_FALSE(mapping.Contains(0x2000));

  // Test VirtualToBinaryAddr
  // binary_addr = virtual_addr - vmem_start + file_offset
  EXPECT_EQ(mapping.VirtualToBinaryAddr(0x1000), 0x100);   // 0x1000 - 0x1000 + 0x100 = 0x100
  EXPECT_EQ(mapping.VirtualToBinaryAddr(0x1500), 0x600);   // 0x1500 - 0x1000 + 0x100 = 0x600
}

TEST(PProfToFoldedStacks, ConvertsSymbolizedProfile) {
  // Build a simple symbolized pprof profile manually
  PProfProfile profile;

  // String table: [0]=empty, [1]="main", [2]="foo", [3]="bar", [4]="alloc_objects", [5]="count",
  // [6]="alloc_space", [7]="bytes"
  profile.add_string_table("");              // 0
  profile.add_string_table("main");          // 1
  profile.add_string_table("foo");           // 2
  profile.add_string_table("bar");           // 3
  profile.add_string_table("alloc_objects"); // 4
  profile.add_string_table("count");         // 5
  profile.add_string_table("alloc_space");   // 6
  profile.add_string_table("bytes");         // 7

  // Sample types
  auto* st1 = profile.add_sample_type();
  st1->set_type(4);  // alloc_objects
  st1->set_unit(5);  // count
  auto* st2 = profile.add_sample_type();
  st2->set_type(6);  // alloc_space
  st2->set_unit(7);  // bytes

  // Functions: main(id=1), foo(id=2), bar(id=3)
  auto* f1 = profile.add_function();
  f1->set_id(1);
  f1->set_name(1);  // "main"
  auto* f2 = profile.add_function();
  f2->set_id(2);
  f2->set_name(2);  // "foo"
  auto* f3 = profile.add_function();
  f3->set_id(3);
  f3->set_name(3);  // "bar"

  // Locations with lines pointing to functions
  auto* loc1 = profile.add_location();
  loc1->set_id(1);
  loc1->add_line()->set_function_id(1);  // main
  auto* loc2 = profile.add_location();
  loc2->set_id(2);
  loc2->add_line()->set_function_id(2);  // foo
  auto* loc3 = profile.add_location();
  loc3->set_id(3);
  loc3->add_line()->set_function_id(3);  // bar

  // Sample 1: main→foo→bar with 5 allocations, 1000 bytes
  // pprof stores leaf first, so: bar, foo, main
  auto* s1 = profile.add_sample();
  s1->add_location_id(3);  // bar (leaf)
  s1->add_location_id(2);  // foo
  s1->add_location_id(1);  // main (root)
  s1->add_value(5);        // alloc_objects
  s1->add_value(1000);     // alloc_space

  // Sample 2: main→foo with 3 allocations, 500 bytes
  auto* s2 = profile.add_sample();
  s2->add_location_id(2);  // foo (leaf)
  s2->add_location_id(1);  // main (root)
  s2->add_value(3);        // alloc_objects
  s2->add_value(500);      // alloc_space

  // Test with alloc_space (value_index=1, default)
  auto result = PProfToFoldedStacks(profile, 1);
  ASSERT_OK(result);
  std::string folded = result.ConsumeValueOrDie();

  // Expected output (root→leaf order):
  // main;foo;bar 1000
  // main;foo 500
  EXPECT_THAT(folded, HasSubstr("main;foo;bar 1000"));
  EXPECT_THAT(folded, HasSubstr("main;foo 500"));

  // Test with alloc_objects (value_index=0)
  auto result_count = PProfToFoldedStacks(profile, 0);
  ASSERT_OK(result_count);
  std::string folded_count = result_count.ConsumeValueOrDie();

  EXPECT_THAT(folded_count, HasSubstr("main;foo;bar 5"));
  EXPECT_THAT(folded_count, HasSubstr("main;foo 3"));
}

TEST(PProfToFoldedStacks, HandlesUnsymbolizedProfile) {
  // Test with a profile that has locations but no functions (unsymbolized)
  PProfProfile profile;

  profile.add_string_table("");
  profile.add_string_table("alloc_objects");
  profile.add_string_table("count");
  profile.add_string_table("alloc_space");
  profile.add_string_table("bytes");

  auto* st1 = profile.add_sample_type();
  st1->set_type(1);
  st1->set_unit(2);
  auto* st2 = profile.add_sample_type();
  st2->set_type(3);
  st2->set_unit(4);

  // Location with address but no function
  auto* loc1 = profile.add_location();
  loc1->set_id(1);
  loc1->set_address(0x1234);

  auto* loc2 = profile.add_location();
  loc2->set_id(2);
  loc2->set_address(0x5678);

  auto* s1 = profile.add_sample();
  s1->add_location_id(2);  // leaf
  s1->add_location_id(1);  // root
  s1->add_value(1);
  s1->add_value(100);

  auto result = PProfToFoldedStacks(profile, 1);
  ASSERT_OK(result);
  std::string folded = result.ConsumeValueOrDie();

  // Should use hex addresses as fallback
  EXPECT_THAT(folded, HasSubstr("0x1234"));
  EXPECT_THAT(folded, HasSubstr("0x5678"));
  EXPECT_THAT(folded, HasSubstr(" 100"));
}

// This test requires the actual binaries to exist on disk.
// Run with: bazel build --spawn_strategy=local //src/shared/pprof:pprof_test
// and then run the binary directly.
// bazel test --strategy=local src/shared/pprof:pprof_test --nocache_test_results --test_output=all --verbose_failures
// Or add tags = ["no_sandbox"] to run without sandboxing.
TEST(SymbolizePProf, SymbolizesRealData) {
  // Read test data files
  const auto pprof_path = testing::BazelRunfilePath("src/shared/pprof/testdata/pprof.data");
  const auto maps_path = testing::BazelRunfilePath("src/shared/pprof/testdata/maps.data");

  auto pprof_data_or = ReadFileToString(pprof_path.string());
  auto maps_data_or = ReadFileToString(maps_path.string());

  // Skip test if files don't exist
  if (!pprof_data_or.ok() || !maps_data_or.ok()) {
    GTEST_SKIP() << "Test data files not found";
  }

  std::string pprof_data = pprof_data_or.ConsumeValueOrDie();
  std::string maps_data = maps_data_or.ConsumeValueOrDie();

  // Parse the legacy pprof format
  auto pprof_result = ParseLegacyHeapProfile(pprof_data);
  ASSERT_OK(pprof_result);
  auto profile = pprof_result.ConsumeValueOrDie();

  // Parse the maps
  auto maps_result = ParseProcMaps(maps_data);
  ASSERT_OK(maps_result);
  auto mappings = maps_result.ConsumeValueOrDie();

  // Check we have valid data
  EXPECT_GT(profile.sample_size(), 0);
  EXPECT_GT(profile.location_size(), 0);
  EXPECT_GT(mappings.size(), 0);

  // Try to symbolize - this will only work if the binaries exist
  LOG(INFO) << mappings.size() << " memory mappings parsed from maps data.";
  auto status = SymbolizePProfProfile(&profile, mappings);

  // If symbolization succeeded, check that we have functions
  if (status.ok()) {
    EXPECT_GT(profile.function_size(), 0) << "Expected some functions to be symbolized";

    // Write the pprof in text format to a temporary file
    std::string text_output;
    google::protobuf::TextFormat::PrintToString(profile, &text_output);

    const std::string text_output_path = "/tmp/pprof_test_output.txt";
    std::ofstream text_file(text_output_path, std::ios::trunc | std::ios::out);
    if (text_file.is_open()) {
      text_file << text_output;
      text_file.close();
      LOG(INFO) << "Wrote symbolized pprof profile text (" << text_output.size()
                << " bytes) to: " << text_output_path;
    } else {
      LOG(ERROR) << "Failed to open text output file: " << text_output_path;
    }

    // Write the pprof in binary protobuf format
    std::string binary_output;
    profile.SerializeToString(&binary_output);

    const std::string binary_output_path = "/tmp/pprof_test_output.pb";
    std::ofstream binary_file(binary_output_path, std::ios::binary | std::ios::trunc);
    if (binary_file.is_open()) {
      binary_file << binary_output;
      binary_file.close();
      LOG(INFO) << "Wrote symbolized pprof profile binary (" << binary_output.size()
                << " bytes) to: " << binary_output_path;
    } else {
      LOG(ERROR) << "Failed to open binary output file: " << binary_output_path;
    }

    // Write folded stacks format (alloc_space by default)
    auto folded_result = PProfToFoldedStacks(profile, 1);
    if (folded_result.ok()) {
      std::string folded_output = folded_result.ConsumeValueOrDie();
      const std::string folded_output_path = "/tmp/pprof_test_output.folded";
      std::ofstream folded_file(folded_output_path, std::ios::trunc | std::ios::out);
      if (folded_file.is_open()) {
        folded_file << folded_output;
        folded_file.close();
        LOG(INFO) << "Wrote folded stacks (" << folded_output.size()
                  << " bytes) to: " << folded_output_path;
      } else {
        LOG(ERROR) << "Failed to open folded output file: " << folded_output_path;
      }
    } else {
      LOG(WARNING) << "Failed to convert to folded stacks: " << folded_result.status().msg();
    }
  } else {
    LOG(WARNING) << "Symbolization failed (expected if binaries don't exist): " << status.msg();
  }
}

// Test symbolization with a known test binary that has symbols.
// This test uses prebuilt_test_exe which has known functions like CanYouFindThis at 0x4011d0.
TEST(SymbolizePProf, SymbolizesWithTestBinary) {
  // Get path to prebuilt_test_exe
  const auto test_exe_path =
      testing::BazelRunfilePath("src/stirling/obj_tools/testdata/cc/prebuilt_test_exe");

  // Verify the binary exists
  ASSERT_TRUE(std::filesystem::exists(test_exe_path))
      << "Test binary not found: " << test_exe_path;

  // Known symbol addresses from prebuilt_test_exe (from nm output):
  // 0x4011d0 T CanYouFindThis
  // 0x401290 T main
  constexpr uint64_t kCanYouFindThisAddr = 0x4011d0;
  constexpr uint64_t kMainAddr = 0x401290;

  // Create synthetic legacy heap profile data with these addresses.
  // Format: "count: bytes [count: bytes] @ 0xaddr1 0xaddr2 ..."
  // The addresses are in leaf-to-root order (call stack order).
  const std::string legacy_pprof = absl::StrFormat(
      "heap profile:    2: 1000 [   2: 1000] @ growth\n"
      "     1: 500 [     1: 500] @ 0x%x 0x%x\n",
      kCanYouFindThisAddr, kMainAddr);

  // Create synthetic maps content pointing to our test binary.
  // Format: start-end perms offset dev inode pathname
  // The prebuilt_test_exe is a non-PIE executable, so it loads at its compiled addresses.
  // We need a mapping that covers addresses 0x401000-0x402000 with offset 0.
  const std::string maps_content = absl::StrFormat(
      "00400000-00405000 r-xp 00000000 00:00 12345      %s\n",
      test_exe_path.string());

  // Parse the legacy pprof format
  auto pprof_result = ParseLegacyHeapProfile(legacy_pprof);
  ASSERT_OK(pprof_result);
  auto profile = pprof_result.ConsumeValueOrDie();

  // Verify we have the expected sample
  ASSERT_EQ(profile.sample_size(), 1);
  ASSERT_EQ(profile.location_size(), 2);

  // Parse the maps
  auto maps_result = ParseProcMaps(maps_content);
  ASSERT_OK(maps_result);
  auto mappings = maps_result.ConsumeValueOrDie();
  ASSERT_EQ(mappings.size(), 1);

  // Symbolize the profile
  auto status = SymbolizePProfProfile(&profile, mappings);
  ASSERT_OK(status);

  // Verify symbolization worked - we should have functions now
  EXPECT_GT(profile.function_size(), 0) << "Expected functions to be created during symbolization";

  // Build a set of all function names in the profile
  std::set<std::string> function_names;
  for (const auto& func : profile.function()) {
    if (func.name() > 0 && func.name() < profile.string_table_size()) {
      function_names.insert(profile.string_table(func.name()));
    }
  }

  // Verify that our known functions were symbolized
  EXPECT_TRUE(function_names.count("CanYouFindThis") > 0)
      << "Expected 'CanYouFindThis' to be symbolized. Found functions: "
      << absl::StrJoin(function_names, ", ");
  EXPECT_TRUE(function_names.count("main") > 0)
      << "Expected 'main' to be symbolized. Found functions: "
      << absl::StrJoin(function_names, ", ");

  // Verify folded stacks output contains the symbolized names
  auto folded_result = PProfToFoldedStacks(profile, 1);
  ASSERT_OK(folded_result);
  std::string folded = folded_result.ConsumeValueOrDie();

  EXPECT_THAT(folded, HasSubstr("CanYouFindThis"))
      << "Folded stacks should contain 'CanYouFindThis'. Got: " << folded;
  EXPECT_THAT(folded, HasSubstr("main"))
      << "Folded stacks should contain 'main'. Got: " << folded;
}

}  // namespace shared
}  // namespace px
