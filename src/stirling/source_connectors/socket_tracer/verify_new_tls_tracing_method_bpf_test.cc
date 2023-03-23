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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "src/common/base/base.h"
#include "src/common/exec/exec.h"
#include "src/common/testing/test_environment.h"
#include "src/shared/types/column_wrapper.h"
#include "src/shared/types/types.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/http/parse.h"
#include "src/stirling/source_connectors/socket_tracer/socket_trace_connector.h"
#include "src/stirling/source_connectors/socket_tracer/testing/container_images.h"
#include "src/stirling/source_connectors/socket_tracer/testing/protocol_checkers.h"
#include "src/stirling/source_connectors/socket_tracer/testing/socket_trace_bpf_test_fixture.h"
#include "src/stirling/source_connectors/socket_tracer/uprobe_symaddrs.h"
#include "src/stirling/testing/common.h"

namespace px {
namespace stirling {

namespace http = protocols::http;

using ::px::stirling::testing::EqHTTPRecord;
using ::px::stirling::testing::FindRecordIdxMatchesPID;
using ::px::stirling::testing::GetTargetRecords;
using ::px::stirling::testing::SocketTraceBPFTestFixture;
using ::px::stirling::testing::ToRecordVector;

using ::testing::Contains;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;

class NginxOpenSSL_1_1_0_ContainerWrapper
    : public ::px::stirling::testing::NginxOpenSSL_1_1_0_Container {
 public:
  int32_t PID() const { return NginxWorkerPID(); }
};

class NginxOpenSSL_1_1_1_ContainerWrapper
    : public ::px::stirling::testing::NginxOpenSSL_1_1_1_Container {
 public:
  int32_t PID() const { return NginxWorkerPID(); }
};

class NginxOpenSSL_3_0_7_ContainerWrapper
    : public ::px::stirling::testing::NginxOpenSSL_3_0_7_Container {
 public:
  int32_t PID() const { return NginxWorkerPID(); }
};

class Node12_3_1ContainerWrapper : public ::px::stirling::testing::Node12_3_1Container {
 public:
  int32_t PID() const { return process_pid(); }
};

class Node14_18_1AlpineContainerWrapper
    : public ::px::stirling::testing::Node14_18_1AlpineContainer {
 public:
  int32_t PID() const { return process_pid(); }
};

class PythonAsyncioContainerWrapper : public ::px::stirling::testing::PythonAsyncioContainer {
 public:
  int32_t PID() const { return process_pid(); }
};

class PythonBlockingContainerWrapper : public ::px::stirling::testing::PythonBlockingContainer {
 public:
  int32_t PID() const { return process_pid(); }
};

// Includes all information we need to extract from the trace records, which are used to verify
// against the expected results.
struct TraceRecords {
  std::vector<http::Record> http_records;
  std::vector<std::string> remote_address;
};

template <typename TServerContainer, bool TForceFptrs>
class BaseOpenSSLTraceTest : public SocketTraceBPFTestFixture</* TClientSideTracing */ false> {
 protected:
  std::size_t response_length_ = 5 * 1024;

  BaseOpenSSLTraceTest() {
    // Run the nginx HTTPS server.
    // The container runner will make sure it is in the ready state before unblocking.
    // Stirling will run after this unblocks, as part of SocketTraceBPFTest SetUp().
    StatusOr<std::string> run_result = server_.Run(std::chrono::seconds{60});
    PX_CHECK_OK(run_result);

    // Sleep an additional second, just to be safe.
    sleep(1);
  }

  void SetUp() override {
    FLAGS_openssl_force_raw_fptrs = force_fptr_;
    FLAGS_max_body_bytes = response_length_;
    FLAGS_http_body_limit_bytes = response_length_;

    SocketTraceBPFTestFixture::SetUp();
  }

  // Returns the trace records of the process specified by the input pid.
  TraceRecords GetTraceRecords(int pid) {
    std::vector<TaggedRecordBatch> tablets =
        this->ConsumeRecords(SocketTraceConnector::kHTTPTableNum);
    if (tablets.empty()) {
      return {};
    }
    types::ColumnWrapperRecordBatch record_batch = tablets[0].records;
    std::vector<size_t> server_record_indices =
        FindRecordIdxMatchesPID(record_batch, kHTTPUPIDIdx, pid);
    std::vector<http::Record> http_records =
        ToRecordVector<http::Record>(record_batch, server_record_indices);
    std::vector<std::string> remote_addresses =
        testing::GetRemoteAddrs(record_batch, server_record_indices);
    return {std::move(http_records), std::move(remote_addresses)};
  }

  TServerContainer server_;
  bool force_fptr_ = TForceFptrs;
};

//-----------------------------------------------------------------------------
// Test Background and Motivation
//-----------------------------------------------------------------------------

// This test is designed to stress test the new implementation of our TLS tracing. Rather than
// walking user space data structures via offsets, it accesses the socket file descriptor via the
// underlying syscall's fd argument. The assumption is that this process will work reliably for
// applications that use their TLS library in a synchronous fashion. This is because we expect the
// uprobe'd functions (SSL_write and SSL_read) to remain on the stack while the syscall is entered
// and returned from. This provides an opportunity to pass the socket fd from the socket syscall
// (read, write, etc) to the uprobe function via a BPF map.
//
// When prototyping this method, we expected the Node14_18_1AlpineContainerWrapper and
// Node12_3_1ContainerWrapper test cases to fail consistently since they use OpenSSL asynchronously.
// However, we noticed that the alpine case passes erroneously. Therefore we wanted to stress test
// the cases that were expected to work more throughly to ensure that they aren't false positives as
// well.
//
// Since our protocol tracing uses the socket fd as the connection's identity, if this mechanism
// were to incorrectly the socket fd we would likely see traffic mixed up between connections.
// In order to increase the liklihood of detecting this, these test cases use HTTPS servers that
// return 5 KiB responses that are padded with the URL field. For example, if the client makes a
// request to /1 it will return a 5 KiB response of 1 characters. The test runs two locust load
// tests which request different URLs many times in order to trigger this connection confusion. The
// assertions ensure that each response payload is 5 KiB in length and only includes the expected
// character.

http::Record GetExpectedHTTPRecord(std::string req_path) {
  http::Record expected_record;
  expected_record.req.minor_version = 1;
  expected_record.req.req_method = "GET";
  expected_record.req.req_path = req_path;
  expected_record.req.body = "";
  expected_record.resp.resp_status = 200;
  expected_record.resp.resp_message = "OK";
  return expected_record;
}

typedef ::testing::Types<NginxOpenSSL_1_1_1_ContainerWrapper, NginxOpenSSL_3_0_7_ContainerWrapper,
                         PythonAsyncioContainerWrapper, PythonBlockingContainerWrapper>
    OpenSSLServerImplementations;

template <typename T>
using OpenSSLTraceDlsymTest = BaseOpenSSLTraceTest<T, true>;

#define OPENSSL_TYPED_TEST(TestCase, CodeBlock) \
  TYPED_TEST(OpenSSLTraceDlsymTest, TestCase)   \
  CodeBlock

TYPED_TEST_SUITE(OpenSSLTraceDlsymTest, OpenSSLServerImplementations);

inline auto PartialEqHTTPResp(const protocols::http::Message& x) {
  using ::testing::Field;

  return AllOf(Field(&protocols::http::Message::resp_status, ::testing::Eq(x.resp_status)),
               Field(&protocols::http::Message::resp_message, ::testing::StrEq(x.resp_message)));
}

inline auto PartialEqHTTPRecord(const protocols::http::Record& x) {
  using ::testing::Field;

  return AllOf(Field(&protocols::http::Record::req, testing::EqHTTPReq(x.req)),
               Field(&protocols::http::Record::resp, PartialEqHTTPResp(x.resp)));
}

OPENSSL_TYPED_TEST(ssl_capture_locust_load_test, {
  using ::testing::StrEq;
  /* FLAGS_stirling_conn_trace_pid = this->server_.PID(); */

  this->StartTransferDataThread();

  auto container_name = this->server_.container_name();
  auto target_http_records = 100;
  // Divide total http requests by number of locust containers
  auto iterations = target_http_records / 2;
  // This controls the concurrency locust will have. The total number of
  // http requests will be divided up between these 'workers'.
  auto users = 5;

  std::thread locust_client1([&container_name, &iterations, &users]() {
    ::px::stirling::testing::LocustContainer client1;
    ASSERT_OK(client1.Run(
        std::chrono::seconds{60}, {absl::Substitute("--network=container:$0", container_name)},
        {"--loglevel=debug", "--user-agent=1", absl::Substitute("--users=$0", users),
         absl::Substitute("-i=$0", iterations)}));
    client1.Wait();
  });
  std::thread locust_client2([&container_name, &iterations, &users]() {
    ::px::stirling::testing::LocustContainer client2;
    ASSERT_OK(client2.Run(
        std::chrono::seconds{60}, {absl::Substitute("--network=container:$0", container_name)},
        {"--loglevel=debug", "--user-agent=2", absl::Substitute("--users=$0", users),
         absl::Substitute("-i=$0", iterations)}));
    client2.Wait();
  });
  locust_client1.join();
  locust_client2.join();

  this->StopTransferDataThread();

  TraceRecords records = this->GetTraceRecords(this->server_.PID());

  http::Record v1 = GetExpectedHTTPRecord("/1");
  http::Record v2 = GetExpectedHTTPRecord("/2");

  EXPECT_THAT(records.http_records.size(), target_http_records);
  for (auto& http_record : records.http_records) {
    EXPECT_THAT((std::array{v1, v2}), Contains(PartialEqHTTPRecord(http_record)));

    std::string s = http_record.resp.body;
    size_t one_count = std::count_if(s.begin(), s.end(), [](char c) { return c == '1'; });
    size_t two_count = std::count_if(s.begin(), s.end(), [](char c) { return c == '2'; });
    LOG(WARNING) << absl::Substitute(
        "Found body with len $0 and chars $1. One and two count: $2 $3\n", s.length(),
        s.substr(0, 10), one_count, two_count);
    if (http_record.req.req_path == "/1") {
      EXPECT_THAT(one_count, this->response_length_);
      EXPECT_THAT(two_count, 0);
    } else {
      EXPECT_THAT(two_count, this->response_length_);
      EXPECT_THAT(one_count, 0);
    }
  }
})

}  // namespace stirling
}  // namespace px
