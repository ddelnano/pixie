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

#include "src/carnot/exec/explode_node.h"

#include <sole.hpp>

#include "src/carnot/exec/test_utils.h"
#include "src/carnot/planpb/test_proto.h"
#include "src/carnot/udf/registry.h"
#include "src/common/testing/testing.h"
#include "src/shared/types/types.h"

namespace px {
namespace carnot {
namespace exec {

using table_store::schema::RowBatch;
using table_store::schema::RowDescriptor;
using ::testing::_;
using types::Int64Value;
using types::StringValue;

class ExplodeNodeTest : public ::testing::Test {
 public:
  ExplodeNodeTest() {
    func_registry_ = std::make_unique<udf::Registry>("test_registry");
    auto table_store = std::make_shared<table_store::TableStore>();

    exec_state_ = std::make_unique<ExecState>(
        func_registry_.get(), table_store, MockResultSinkStubGenerator, MockMetricsStubGenerator,
        MockTraceStubGenerator, MockLogStubGenerator, sole::uuid4(), nullptr);
  }

 protected:
  std::unique_ptr<plan::Operator> plan_node_;
  std::unique_ptr<ExecState> exec_state_;
  std::unique_ptr<udf::Registry> func_registry_;
};

TEST_F(ExplodeNodeTest, BasicNewlineExplode) {
  auto op_proto = planpb::testutils::CreateTestExplode1PB();
  plan_node_ = plan::ExplodeOperator::FromProto(op_proto, /*id*/ 1);

  RowDescriptor input_rd({types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});
  RowDescriptor output_rd(
      {types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});

  auto tester = exec::ExecNodeTester<ExplodeNode, plan::ExplodeOperator>(
      *plan_node_, output_rd, {input_rd}, exec_state_.get());

  // Input: row with multi-line string "a\nb\nc" should explode into 3 rows.
  tester
      .ConsumeNext(RowBatchBuilder(input_rd, 2, /*eow*/ false, /*eos*/ false)
                       .AddColumn<types::Int64Value>({1, 2})
                       .AddColumn<types::StringValue>({"a\nb\nc", "x\ny"})
                       .AddColumn<types::Int64Value>({100, 200})
                       .get(),
                   0)
      .ExpectRowBatch(RowBatchBuilder(output_rd, 5, false, false)
                          .AddColumn<types::Int64Value>({1, 1, 1, 2, 2})
                          .AddColumn<types::StringValue>({"a", "b", "c", "x", "y"})
                          .AddColumn<types::Int64Value>({100, 100, 100, 200, 200})
                          .get())
      .Close();
}

TEST_F(ExplodeNodeTest, SemicolonDelimiter) {
  auto op_proto = planpb::testutils::CreateTestExplodeSemicolonPB();
  plan_node_ = plan::ExplodeOperator::FromProto(op_proto, /*id*/ 1);

  RowDescriptor input_rd({types::DataType::STRING, types::DataType::INT64});
  RowDescriptor output_rd({types::DataType::STRING, types::DataType::INT64});

  auto tester = exec::ExecNodeTester<ExplodeNode, plan::ExplodeOperator>(
      *plan_node_, output_rd, {input_rd}, exec_state_.get());

  // Input: row with semicolon-delimited string "foo;bar;baz" should explode into 3 rows.
  tester
      .ConsumeNext(RowBatchBuilder(input_rd, 1, /*eow*/ true, /*eos*/ true)
                       .AddColumn<types::StringValue>({"foo;bar;baz"})
                       .AddColumn<types::Int64Value>({42})
                       .get(),
                   0)
      .ExpectRowBatch(RowBatchBuilder(output_rd, 3, true, true)
                          .AddColumn<types::StringValue>({"foo", "bar", "baz"})
                          .AddColumn<types::Int64Value>({42, 42, 42})
                          .get())
      .Close();
}

TEST_F(ExplodeNodeTest, SingleValueNoDelimiter) {
  auto op_proto = planpb::testutils::CreateTestExplode1PB();
  plan_node_ = plan::ExplodeOperator::FromProto(op_proto, /*id*/ 1);

  RowDescriptor input_rd({types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});
  RowDescriptor output_rd(
      {types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});

  auto tester = exec::ExecNodeTester<ExplodeNode, plan::ExplodeOperator>(
      *plan_node_, output_rd, {input_rd}, exec_state_.get());

  // Input: row with single value (no delimiter) should produce 1 row.
  tester
      .ConsumeNext(RowBatchBuilder(input_rd, 1, /*eow*/ true, /*eos*/ true)
                       .AddColumn<types::Int64Value>({1})
                       .AddColumn<types::StringValue>({"single_value"})
                       .AddColumn<types::Int64Value>({100})
                       .get(),
                   0)
      .ExpectRowBatch(RowBatchBuilder(output_rd, 1, true, true)
                          .AddColumn<types::Int64Value>({1})
                          .AddColumn<types::StringValue>({"single_value"})
                          .AddColumn<types::Int64Value>({100})
                          .get())
      .Close();
}

TEST_F(ExplodeNodeTest, EmptyString) {
  auto op_proto = planpb::testutils::CreateTestExplode1PB();
  plan_node_ = plan::ExplodeOperator::FromProto(op_proto, /*id*/ 1);

  RowDescriptor input_rd({types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});
  RowDescriptor output_rd(
      {types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});

  auto tester = exec::ExecNodeTester<ExplodeNode, plan::ExplodeOperator>(
      *plan_node_, output_rd, {input_rd}, exec_state_.get());

  // Input: row with empty string should produce 1 row with empty string.
  tester
      .ConsumeNext(RowBatchBuilder(input_rd, 1, /*eow*/ true, /*eos*/ true)
                       .AddColumn<types::Int64Value>({1})
                       .AddColumn<types::StringValue>({""})
                       .AddColumn<types::Int64Value>({100})
                       .get(),
                   0)
      .ExpectRowBatch(RowBatchBuilder(output_rd, 1, true, true)
                          .AddColumn<types::Int64Value>({1})
                          .AddColumn<types::StringValue>({""})
                          .AddColumn<types::Int64Value>({100})
                          .get())
      .Close();
}

TEST_F(ExplodeNodeTest, ZeroRowInput) {
  auto op_proto = planpb::testutils::CreateTestExplode1PB();
  plan_node_ = plan::ExplodeOperator::FromProto(op_proto, /*id*/ 1);

  RowDescriptor input_rd({types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});
  RowDescriptor output_rd(
      {types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});

  auto tester = exec::ExecNodeTester<ExplodeNode, plan::ExplodeOperator>(
      *plan_node_, output_rd, {input_rd}, exec_state_.get());

  // Input: empty row batch should produce empty output.
  tester
      .ConsumeNext(RowBatchBuilder(input_rd, 0, /*eow*/ true, /*eos*/ true)
                       .AddColumn<types::Int64Value>({})
                       .AddColumn<types::StringValue>({})
                       .AddColumn<types::Int64Value>({})
                       .get(),
                   0)
      .ExpectRowBatch(RowBatchBuilder(output_rd, 0, true, true)
                          .AddColumn<types::Int64Value>({})
                          .AddColumn<types::StringValue>({})
                          .AddColumn<types::Int64Value>({})
                          .get())
      .Close();
}

TEST_F(ExplodeNodeTest, MultipleRowBatches) {
  auto op_proto = planpb::testutils::CreateTestExplode1PB();
  plan_node_ = plan::ExplodeOperator::FromProto(op_proto, /*id*/ 1);

  RowDescriptor input_rd({types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});
  RowDescriptor output_rd(
      {types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});

  auto tester = exec::ExecNodeTester<ExplodeNode, plan::ExplodeOperator>(
      *plan_node_, output_rd, {input_rd}, exec_state_.get());

  // First batch (not end of stream).
  tester
      .ConsumeNext(RowBatchBuilder(input_rd, 1, /*eow*/ false, /*eos*/ false)
                       .AddColumn<types::Int64Value>({1})
                       .AddColumn<types::StringValue>({"a\nb"})
                       .AddColumn<types::Int64Value>({10})
                       .get(),
                   0)
      .ExpectRowBatch(RowBatchBuilder(output_rd, 2, false, false)
                          .AddColumn<types::Int64Value>({1, 1})
                          .AddColumn<types::StringValue>({"a", "b"})
                          .AddColumn<types::Int64Value>({10, 10})
                          .get())
      // Second batch (end of stream).
      .ConsumeNext(RowBatchBuilder(input_rd, 1, /*eow*/ true, /*eos*/ true)
                       .AddColumn<types::Int64Value>({2})
                       .AddColumn<types::StringValue>({"x\ny\nz"})
                       .AddColumn<types::Int64Value>({20})
                       .get(),
                   0)
      .ExpectRowBatch(RowBatchBuilder(output_rd, 3, true, true)
                          .AddColumn<types::Int64Value>({2, 2, 2})
                          .AddColumn<types::StringValue>({"x", "y", "z"})
                          .AddColumn<types::Int64Value>({20, 20, 20})
                          .get())
      .Close();
}

TEST_F(ExplodeNodeTest, FoldedStacksFormat) {
  // Test with realistic folded stack format: "func1;func2;func3 123\nfunc4;func5 456"
  auto op_proto = planpb::testutils::CreateTestExplode1PB();
  plan_node_ = plan::ExplodeOperator::FromProto(op_proto, /*id*/ 1);

  RowDescriptor input_rd({types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});
  RowDescriptor output_rd(
      {types::DataType::INT64, types::DataType::STRING, types::DataType::INT64});

  auto tester = exec::ExecNodeTester<ExplodeNode, plan::ExplodeOperator>(
      *plan_node_, output_rd, {input_rd}, exec_state_.get());

  tester
      .ConsumeNext(RowBatchBuilder(input_rd, 1, /*eow*/ true, /*eos*/ true)
                       .AddColumn<types::Int64Value>({1})
                       .AddColumn<types::StringValue>({"main;foo;bar 100\nmain;baz 50"})
                       .AddColumn<types::Int64Value>({999})
                       .get(),
                   0)
      .ExpectRowBatch(RowBatchBuilder(output_rd, 2, true, true)
                          .AddColumn<types::Int64Value>({1, 1})
                          .AddColumn<types::StringValue>({"main;foo;bar 100", "main;baz 50"})
                          .AddColumn<types::Int64Value>({999, 999})
                          .get())
      .Close();
}

}  // namespace exec
}  // namespace carnot
}  // namespace px
