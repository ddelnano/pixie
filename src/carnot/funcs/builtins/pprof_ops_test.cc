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

#include "src/carnot/funcs/builtins/pprof_ops.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <utility>
#include <vector>

#include <absl/strings/str_format.h>

#include "src/carnot/udf/test_utils.h"
#include "src/common/testing/testing.h"
#include "src/shared/pprof/pprof.h"

namespace px {
namespace carnot {
namespace builtins {

using ::testing::HasSubstr;
using px::shared::DeserializePProfProfile;
using px::shared::PProfProfile;

namespace {
constexpr int64_t profiler_period_ms = 11;
}

TEST(PProf, profiling_rows_to_pprof_test) {
  // Raw stack trace string and count input data.
  // Note, there are duplicated stack traces.
  const std::vector<std::pair<std::string, uint64_t>> input = {
      {"foo;bar;baz", 1},
      {"foo;bar;baz", 2},
      {"foo;bar;qux", 3},
      {"foo;bar", 4},
      {"foo;bar", 5},
      {"main;compute;map;reduce", 6},
      {"main;compute;map;reduce", 7},
      {"main;compute;map;reduce", 8},
  };

  // The expected output as a histogram (string=>count map).
  const absl::flat_hash_map<std::string, uint64_t> expected = {
      {"foo;bar;baz", 1 + 2},
      {"foo;bar;qux", 3},
      {"foo;bar", 4 + 5},
      {"main;compute;map;reduce", 6 + 7 + 8},
  };

  // Create our UDA tester.
  auto pprof_uda_tester = udf::UDATester<CreatePProfRowAggregate>();

  // Feed the input to the tester.
  for (const auto& [stack_trace, count] : input) {
    pprof_uda_tester.ForInput(stack_trace, count, profiler_period_ms);
  }

  // Get the result.
  const std::string result = pprof_uda_tester.Result();

  // Parse the result.
  PProfProfile pprof;
  EXPECT_TRUE(pprof.ParseFromString(result));

  // Deserialize the parsed pprof into a histo.
  const auto actual = DeserializePProfProfile(pprof);

  // Expect the deserialized result to be equal to our expected value.
  EXPECT_EQ(actual, expected);
}

TEST(PProf, pprof_merge_test) {
  // Raw stack trace string and count input data.
  // Note, there are duplicated stack traces.
  const std::vector<std::pair<std::string, uint64_t>> input_a = {
      {"foo;bar;baz", 1},
      {"foo;bar;qux", 2},
      {"main;compute;map;reduce", 3},
  };
  const std::vector<std::pair<std::string, uint64_t>> input_b = {
      {"foo;bar;baz", 5},
      {"foo;bar;baz", 5},
      {"foo;bar;qux", 10},
      {"foo;bar;qux", 10},
      {"main;compute;map;reduce", 15},
      {"main;compute;map;reduce", 15},
  };

  // The expected output as a histogram (string=>count map).
  const absl::flat_hash_map<std::string, uint64_t> expected = {
      {"foo;bar;baz", 11},
      {"foo;bar;qux", 22},
      {"main;compute;map;reduce", 33},
  };

  auto pprof_uda_tester_a = udf::UDATester<CreatePProfRowAggregate>();
  auto pprof_uda_tester_b = udf::UDATester<CreatePProfRowAggregate>();
  auto pprof_uda_tester_merge = udf::UDATester<CreatePProfRowAggregate>();

  // Feed the A inputs to the tester_a.
  for (const auto& [stack_trace, count] : input_a) {
    pprof_uda_tester_a.ForInput(stack_trace, count, profiler_period_ms);
  }

  // Feed the B inputs to the tester_b.
  for (const auto& [stack_trace, count] : input_b) {
    pprof_uda_tester_b.ForInput(stack_trace, count, profiler_period_ms);
  }

  // Merge A & B UDAs into the final "merge" UDA.
  EXPECT_OK(pprof_uda_tester_merge.Deserialize(pprof_uda_tester_a.Serialize()));
  EXPECT_OK(pprof_uda_tester_merge.Deserialize(pprof_uda_tester_b.Serialize()));

  // Get the result.
  const std::string result = pprof_uda_tester_merge.Result();

  // Parse the result.
  PProfProfile pprof;
  EXPECT_TRUE(pprof.ParseFromString(result));

  // Deserialize the parsed pprof into a histo.
  const auto actual = DeserializePProfProfile(pprof);

  // Expect the deserialized result to be equal to our expected value.
  EXPECT_EQ(actual, expected);
}

TEST(PProf, uda_fails_with_multiple_sample_periods) {
  // Create our UDA tester.
  auto pprof_uda_tester = udf::UDATester<CreatePProfRowAggregate>();

  // Input two stack traces, but with different profiling sample periods.
  pprof_uda_tester.ForInput("foo;bar;baz", 1, profiler_period_ms);
  pprof_uda_tester.ForInput("foo;bar;qux", 2, profiler_period_ms + 1);

  // Get the result.
  const std::string result = pprof_uda_tester.Result();

  // Parse the result: this should fail because we used multiple sample periods above.
  PProfProfile pprof;
  EXPECT_FALSE(pprof.ParseFromString(result));
}

// Tests for SymbolizePProf UDF
TEST(SymbolizePProf, ReturnsErrorForEmptyMaps) {
  auto udf_tester = udf::UDFTester<SymbolizePProf>();

  // Create a simple legacy pprof format
  const std::string pprof_data = "heap profile:    1: 100 [   1: 100] @ growth\n"
                                  "     1: 100 [     1: 100] @ 0x1000 0x2000\n";

  // Empty maps content should return an error
  udf_tester.ForInput(pprof_data, "").Expect("Error: Empty maps content");
}

TEST(SymbolizePProf, HandlesEmptyPProfGracefully) {
  auto udf_tester = udf::UDFTester<SymbolizePProf>();

  // Empty pprof data (empty protobuf serializes to empty string)
  const std::string empty_pprof = "";
  const std::string maps_content = "00400000-00405000 r-xp 00000000 00:00 12345 /bin/test\n";

  // Even empty pprof data should process without crashing
  // An empty protobuf serializes back to an empty string
  std::string result = udf_tester.ForInput(empty_pprof, maps_content).Result();

  // Empty input is valid (empty proto) and will return empty serialization
  // The key thing is that it doesn't crash or return an error
  EXPECT_FALSE(absl::StartsWith(result, "Error:"));
}

TEST(SymbolizePProf, SymbolizesWithTestBinary) {
  // Get path to prebuilt_test_exe
  const auto test_exe_path =
      testing::BazelRunfilePath("src/stirling/obj_tools/testdata/cc/prebuilt_test_exe");

  // Skip test if binary doesn't exist
  if (!std::filesystem::exists(test_exe_path)) {
    GTEST_SKIP() << "Test binary not found: " << test_exe_path;
  }

  // Known symbol addresses from prebuilt_test_exe (from nm output):
  // 0x4011d0 T CanYouFindThis
  // 0x401290 T main
  constexpr uint64_t kCanYouFindThisAddr = 0x4011d0;
  constexpr uint64_t kMainAddr = 0x401290;

  // Create synthetic legacy heap profile data with these addresses
  const std::string legacy_pprof = absl::StrFormat(
      "heap profile:    1: 500 [   1: 500] @ growth\n"
      "     1: 500 [     1: 500] @ 0x%x 0x%x\n",
      kCanYouFindThisAddr, kMainAddr);

  // Create synthetic maps content pointing to our test binary
  const std::string maps_content = absl::StrFormat(
      "00400000-00405000 r-xp 00000000 00:00 12345      %s\n",
      test_exe_path.string());

  auto udf_tester = udf::UDFTester<SymbolizePProf>();
  std::string result = udf_tester.ForInput(legacy_pprof, maps_content).Result();

  // Result should not be an error
  EXPECT_FALSE(absl::StartsWith(result, "Error:")) << "Got error: " << result;

  // Parse the result as a pprof protobuf
  PProfProfile pprof;
  ASSERT_TRUE(pprof.ParseFromString(result)) << "Failed to parse symbolized pprof";

  // Verify symbolization worked - we should have functions
  EXPECT_GT(pprof.function_size(), 0) << "Expected functions to be created during symbolization";

  // Build a set of all function names in the profile
  std::set<std::string> function_names;
  for (const auto& func : pprof.function()) {
    if (func.name() > 0 && func.name() < pprof.string_table_size()) {
      function_names.insert(pprof.string_table(func.name()));
    }
  }

  // Verify that our known functions were symbolized
  EXPECT_TRUE(function_names.count("CanYouFindThis") > 0)
      << "Expected 'CanYouFindThis' to be symbolized";
  EXPECT_TRUE(function_names.count("main") > 0)
      << "Expected 'main' to be symbolized";
}

TEST(SymbolizePProf, HandlesBinaryPProfInput) {
  // Get path to prebuilt_test_exe
  const auto test_exe_path =
      testing::BazelRunfilePath("src/stirling/obj_tools/testdata/cc/prebuilt_test_exe");

  // Skip test if binary doesn't exist
  if (!std::filesystem::exists(test_exe_path)) {
    GTEST_SKIP() << "Test binary not found: " << test_exe_path;
  }

  constexpr uint64_t kCanYouFindThisAddr = 0x4011d0;

  // Create a pprof protobuf directly
  PProfProfile input_pprof;
  input_pprof.add_string_table("");  // index 0 must be empty
  input_pprof.add_string_table("alloc_objects");
  input_pprof.add_string_table("count");
  input_pprof.add_string_table("alloc_space");
  input_pprof.add_string_table("bytes");

  auto* st1 = input_pprof.add_sample_type();
  st1->set_type(1);
  st1->set_unit(2);
  auto* st2 = input_pprof.add_sample_type();
  st2->set_type(3);
  st2->set_unit(4);

  auto* loc = input_pprof.add_location();
  loc->set_id(1);
  loc->set_address(kCanYouFindThisAddr);

  auto* sample = input_pprof.add_sample();
  sample->add_location_id(1);
  sample->add_value(1);
  sample->add_value(100);

  // Serialize to binary
  std::string binary_pprof;
  ASSERT_TRUE(input_pprof.SerializeToString(&binary_pprof));

  // Create maps content
  const std::string maps_content = absl::StrFormat(
      "00400000-00405000 r-xp 00000000 00:00 12345      %s\n",
      test_exe_path.string());

  auto udf_tester = udf::UDFTester<SymbolizePProf>();
  std::string result = udf_tester.ForInput(binary_pprof, maps_content).Result();

  // Result should not be an error
  EXPECT_FALSE(absl::StartsWith(result, "Error:")) << "Got error: " << result;

  // Parse and verify
  PProfProfile output_pprof;
  ASSERT_TRUE(output_pprof.ParseFromString(result));
  EXPECT_GT(output_pprof.function_size(), 0);
}

// Tests for PProfToFoldedStacksUDF
TEST(PProfToFoldedStacks, ConvertsSymbolizedProfile) {
  // Build a symbolized pprof profile
  PProfProfile profile;

  // String table: [0]=empty, [1]="main", [2]="foo", [3]="bar", etc.
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

  // Sample: main→foo→bar with 5 allocations, 1000 bytes
  // pprof stores leaf first: bar, foo, main
  auto* sample = profile.add_sample();
  sample->add_location_id(3);  // bar (leaf)
  sample->add_location_id(2);  // foo
  sample->add_location_id(1);  // main (root)
  sample->add_value(5);        // alloc_objects
  sample->add_value(1000);     // alloc_space

  // Serialize to binary
  std::string binary_pprof;
  ASSERT_TRUE(profile.SerializeToString(&binary_pprof));

  // Test with alloc_space (value_index=1)
  auto udf_tester = udf::UDFTester<PProfToFoldedStacksUDF>();
  std::string result = udf_tester.ForInput(binary_pprof, 1).Result();

  EXPECT_FALSE(absl::StartsWith(result, "Error:")) << "Got error: " << result;
  EXPECT_THAT(result, HasSubstr("main;foo;bar 1000"));

  // Test with alloc_objects (value_index=0)
  auto udf_tester2 = udf::UDFTester<PProfToFoldedStacksUDF>();
  std::string result2 = udf_tester2.ForInput(binary_pprof, 0).Result();

  EXPECT_FALSE(absl::StartsWith(result2, "Error:")) << "Got error: " << result2;
  EXPECT_THAT(result2, HasSubstr("main;foo;bar 5"));
}

TEST(PProfToFoldedStacks, ReturnsErrorForInvalidInput) {
  auto udf_tester = udf::UDFTester<PProfToFoldedStacksUDF>();

  // Invalid protobuf data
  std::string result = udf_tester.ForInput("not a valid protobuf", 1).Result();

  EXPECT_THAT(result, HasSubstr("Error:"));
}

TEST(PProfToFoldedStacks, HandlesEmptyProfile) {
  PProfProfile profile;
  // Add required sample types but no samples
  profile.add_string_table("");
  profile.add_string_table("alloc_objects");
  profile.add_string_table("count");

  auto* st = profile.add_sample_type();
  st->set_type(1);
  st->set_unit(2);

  std::string binary_pprof;
  ASSERT_TRUE(profile.SerializeToString(&binary_pprof));

  auto udf_tester = udf::UDFTester<PProfToFoldedStacksUDF>();
  std::string result = udf_tester.ForInput(binary_pprof, 0).Result();

  // Should not error, just return empty or minimal output
  EXPECT_FALSE(absl::StartsWith(result, "Error:")) << "Got error: " << result;
}

TEST(PProfToFoldedStacks, EndToEndWithSymbolization) {
  // Get path to prebuilt_test_exe
  const auto test_exe_path =
      testing::BazelRunfilePath("src/stirling/obj_tools/testdata/cc/prebuilt_test_exe");

  if (!std::filesystem::exists(test_exe_path)) {
    GTEST_SKIP() << "Test binary not found: " << test_exe_path;
  }

  constexpr uint64_t kCanYouFindThisAddr = 0x4011d0;
  constexpr uint64_t kMainAddr = 0x401290;

  // Create legacy heap profile
  const std::string legacy_pprof = absl::StrFormat(
      "heap profile:    1: 500 [   1: 500] @ growth\n"
      "     1: 500 [     1: 500] @ 0x%x 0x%x\n",
      kCanYouFindThisAddr, kMainAddr);

  const std::string maps_content = absl::StrFormat(
      "00400000-00405000 r-xp 00000000 00:00 12345      %s\n",
      test_exe_path.string());

  // First symbolize using SymbolizePProf
  auto symbolize_tester = udf::UDFTester<SymbolizePProf>();
  std::string symbolized = symbolize_tester.ForInput(legacy_pprof, maps_content).Result();
  ASSERT_FALSE(absl::StartsWith(symbolized, "Error:")) << "Symbolization failed: " << symbolized;

  // Then convert to folded stacks
  auto folded_tester = udf::UDFTester<PProfToFoldedStacksUDF>();
  std::string folded = folded_tester.ForInput(symbolized, 1).Result();

  EXPECT_FALSE(absl::StartsWith(folded, "Error:")) << "Folded conversion failed: " << folded;

  // Verify the folded format: "func1;func2 value\n"
  // The output should contain semicolon-separated function names followed by a space and value
  EXPECT_THAT(folded, HasSubstr(";")) << "Folded output should contain semicolons between frames";
  EXPECT_THAT(folded, HasSubstr(" 500")) << "Folded output should end with ' 500' (the value)";

  // Verify both function names appear and are separated by semicolon
  // Expected format: "main;CanYouFindThis 500\n" (root to leaf order)
  EXPECT_THAT(folded, HasSubstr("main;CanYouFindThis"))
      << "Expected 'main;CanYouFindThis' in folded output. Got: " << folded;
}

}  // namespace builtins
}  // namespace carnot
}  // namespace px
