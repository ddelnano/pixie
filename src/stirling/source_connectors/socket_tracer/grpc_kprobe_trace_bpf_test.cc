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
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "src/common/exec/subprocess.h"
#include "src/common/system/kernel_version.h"
#include "src/common/system/proc_pid_path.h"
#include "src/common/testing/testing.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/http2/grpc.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/http2/testing/greeter_server.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/http2/testing/proto/greet.grpc.pb.h"
#include "src/stirling/source_connectors/socket_tracer/testing/container_images/py_grpc_hello_world_container.h"
#include "src/stirling/source_connectors/socket_tracer/testing/socket_trace_bpf_test_fixture.h"
#include "src/stirling/testing/common.h"
#include "src/stirling/utils/linux_headers.h"

namespace px {
namespace stirling {

using ::px::stirling::testing::FindRecordIdxMatchesPID;
using ::px::system::ProcPidPath;
using ::px::types::ColumnWrapperRecordBatch;
using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::HasSubstr;
using ::testing::StrEq;

/* StatusOr<md::UPID> ToUPID(uint32_t pid) { */
/*   PX_ASSIGN_OR_RETURN(int64_t pid_start_time, system::GetPIDStartTimeTicks(ProcPidPath(pid))); */
/*   return md::UPID{/1* asid *1/ 0, pid, pid_start_time}; */
/* } */

/* class GRPCServer { */
/*  public: */
/*   static constexpr std::string_view kServerPath = */
/*       "src/stirling/source_connectors/socket_tracer/protocols/http2/testing/go_grpc_server/" */
/*       "golang_$0_grpc_server"; */

/*   GRPCServer() = default; */

/*   void LaunchServer(std::string go_version, bool use_https) { */
/*     std::string server_path = absl::Substitute(kServerPath, go_version); */
/*     server_path = px::testing::BazelRunfilePath(server_path).string(); */
/*     CHECK(fs::Exists(server_path)); */

/*     const std::string https_flag = use_https ? "--https=true" : "--https=false"; */
/*     // Let server pick random port in order avoid conflicting. */
/*     const std::string port_flag = "--port=0"; */

/*     PX_CHECK_OK(s_.Start({server_path, https_flag, port_flag})); */
/*     LOG(INFO) << "Server PID: " << s_.child_pid(); */

/*     // Give some time for the server to start up. */
/*     sleep(2); */

/*     std::string port_str; */
/*     PX_CHECK_OK(s_.Stdout(&port_str)); */
/*     CHECK(absl::SimpleAtoi(port_str, &port_)); */
/*     CHECK_NE(0, port_); */
/*   } */

/*   int port() { return port_; } */
/*   int pid() { return s_.child_pid(); } */

/*   SubProcess s_; */
/*   int port_ = -1; */
/* }; */

/* class GRPCClient { */
/*  public: */
/*   static constexpr std::string_view kClientPath = */
/*       "src/stirling/source_connectors/socket_tracer/protocols/http2/testing/go_grpc_client/" */
/*       "golang_$0_grpc_client"; */

/*   void LaunchClient(std::string_view go_version, bool use_compression, bool use_https, int port) { */
/*     std::string client_path = absl::Substitute(kClientPath, go_version); */
/*     client_path = px::testing::BazelRunfilePath(client_path).string(); */

/*     CHECK(fs::Exists(client_path)); */

/*     const std::string https_flag = use_https ? "--https=true" : "--https=false"; */
/*     const std::string compression_flag = */
/*         use_compression ? "--compression=true" : "--compression=false"; */
/*     PX_CHECK_OK(c_.Start({client_path, https_flag, compression_flag, "-once", "-name=PixieLabs", */
/*                           absl::StrCat("-address=localhost:", port)})); */
/*     LOG(INFO) << "Client PID: " << c_.child_pid(); */
/*     CHECK_EQ(0, c_.Wait()); */
/*   } */

/*   SubProcess c_; */
/* }; */

/* struct TestParams { */
/*   std::string go_version; */
/*   bool use_compression; */
/*   bool use_https; */
/* }; */

/* using TestFixture = testing::SocketTraceBPFTestFixture</1* TClientSideTracing *1/ false>; */

/* class GRPCTraceTest : public TestFixture, public ::testing::WithParamInterface<TestParams> { */
/*  protected: */
/*   GRPCTraceTest() {} */

/*   void TearDown() override { */
/*     TestFixture::TearDown(); */
/*     server_.s_.Kill(); */
/*     CHECK_EQ(9, server_.s_.Wait()) << "Server should have been killed."; */
/*   } */

/*   GRPCServer server_; */
/*   GRPCClient client_; */
/* }; */

/* TEST_P(GRPCTraceTest, CaptureRPCTraceRecord) { */
/*   auto params = GetParam(); */

/*   PX_SET_FOR_SCOPE(FLAGS_socket_tracer_enable_http2_gzip, params.use_compression); */
/*   server_.LaunchServer(params.go_version, params.use_https); */

/*   // Deploy uprobes on the newly launched server. */
/*   RefreshContext(/1* blocking_deploy_uprobes *1/ true); */

/*   StartTransferDataThread(); */

/*   client_.LaunchClient(params.go_version, params.use_compression, params.use_https, server_.port()); */

/*   StopTransferDataThread(); */

/*   std::vector<TaggedRecordBatch> tablets = ConsumeRecords(SocketTraceConnector::kHTTPTableNum); */
/*   ASSERT_NOT_EMPTY_AND_GET_RECORDS(const types::ColumnWrapperRecordBatch& rb, tablets); */
/*   const std::vector<size_t> target_record_indices = */
/*       FindRecordIdxMatchesPID(rb, kHTTPUPIDIdx, server_.pid()); */
/*   ASSERT_GE(target_record_indices.size(), 1); */

/*   // We should get exactly one record. */
/*   const size_t idx = target_record_indices.front(); */
/*   const std::string scheme_text = params.use_https ? R"(":scheme":"https")" : R"(":scheme":"http")"; */

/*   md::UPID upid(rb[kHTTPUPIDIdx]->Get<types::UInt128Value>(idx).val); */
/*   ASSERT_OK_AND_ASSIGN(md::UPID expected_upid, ToUPID(server_.pid())); */
/*   EXPECT_EQ(upid, expected_upid); */

/*   EXPECT_THAT( */
/*       std::string(rb[kHTTPReqHeadersIdx]->Get<types::StringValue>(idx)), */
/*       AllOf(HasSubstr(absl::Substitute(R"(":authority":"localhost:$0")", server_.port())), */
/*             HasSubstr(R"(":method":"POST")"), HasSubstr(scheme_text), */
/*             HasSubstr(absl::StrCat(R"(":scheme":)", params.use_https ? R"("https")" : R"("http")")), */
/*             HasSubstr(R"("content-type":"application/grpc")"), HasSubstr(R"("grpc-timeout")"), */
/*             HasSubstr(R"("te":"trailers","user-agent")"))); */
/*   EXPECT_THAT( */
/*       std::string(rb[kHTTPRespHeadersIdx]->Get<types::StringValue>(idx)), */
/*       AllOf(HasSubstr(R"(":status":"200")"), HasSubstr(R"("content-type":"application/grpc")"), */
/*             HasSubstr(R"("grpc-message":"")"), HasSubstr(R"("grpc-status":"0"})"))); */
/*   EXPECT_THAT(std::string(rb[kHTTPRemoteAddrIdx]->Get<types::StringValue>(idx)), */
/*               AnyOf(HasSubstr("127.0.0.1"), HasSubstr("::1"))); */
/*   EXPECT_EQ(2, rb[kHTTPMajorVersionIdx]->Get<types::Int64Value>(idx).val); */
/*   EXPECT_EQ(0, rb[kHTTPMinorVersionIdx]->Get<types::Int64Value>(idx).val); */
/*   EXPECT_EQ(static_cast<uint64_t>(HTTPContentType::kGRPC), */
/*             rb[kHTTPContentTypeIdx]->Get<types::Int64Value>(idx).val); */

/*   EXPECT_EQ(rb[kHTTPRespBodyIdx]->Get<types::StringValue>(idx).string(), R"(1: "Hello PixieLabs")"); */
/* } */

/* INSTANTIATE_TEST_SUITE_P(SecurityModeTest, GRPCTraceTest, */
/*                          ::testing::Values( */
/*                              // Did not enumerate all combinations, as they are independent based on */
/*                              // our knowledge, and to minimize test size to reduce flakiness. */
/*                              TestParams{"1_16", true, true}, TestParams{"1_16", true, false}, */
/*                              TestParams{"1_17", false, true}, TestParams{"1_17", false, false}, */
/*                              TestParams{"1_18", false, true}, TestParams{"1_18", true, false}, */
/*                              TestParams{"1_19", false, false}, TestParams{"1_19", true, true}, */
/*                              TestParams{"1_20", true, true}, TestParams{"1_20", true, false}, */
/*                              TestParams{"1_21", true, true}, TestParams{"1_21", true, false}, */
/*                              TestParams{"boringcrypto", true, true})); */

/* class PyGRPCTraceTest : public testing::SocketTraceBPFTestFixture</1* TClientSideTracing *1/ false> { */
/*  protected: */
/*   PyGRPCTraceTest() { */
/*     // This takes effect before initializing socket tracer, so Python gRPC is actually enabled. */
/*     FLAGS_stirling_enable_grpc_c_tracing = true; */

/*     // This enables uprobe attachment after stirling has started running. */
/*     FLAGS_stirling_rescan_for_dlopen = true; */
/*   } */
/* }; */

// Test that socket tracker can trace Python gRPC server's message.
/*TEST_F(PyGRPCTraceTest, VerifyTraceRecords) { */
/*  ASSERT_OK_AND_ASSIGN(const system::KernelVersion kernel_version, system::GetKernelVersion()); */
/*  const system::KernelVersion kKernelVersion5_3 = {5, 3, 0}; */
/*  if (system::CompareKernelVersions(kernel_version, kKernelVersion5_3) == */
/*      system::KernelVersionOrder::kOlder) { */
/*    LOG(WARNING) << absl::Substitute( */
/*        "Skipping because host kernel version $0 is older than $1, " */
/*        "old kernel versions do not support bounded loops, which is required by grpc_c_trace.c", */
/*        kernel_version.ToString(), kKernelVersion5_3.ToString()); */
/*    return; */
/*  } */

/*  StartTransferDataThread(); */

/*  testing::PyGRPCHelloWorld client; */
/*  testing::PyGRPCHelloWorld server; */

/*  // First start the server so the process can be detected by socket tracer. */
/*  PX_CHECK_OK(server.Run(std::chrono::seconds{60}, /1*options*/ {}, */
/*                         /1*args*/ {"python", "helloworld/greeter_server.py"})); */
/*  PX_CHECK_OK( */
/*      client.Run(std::chrono::seconds{60}, */
/*                 /1*options*/ {absl::Substitute("--network=container:$0", server.container_name())}, */
/*                 /1*args*/ {"python", "helloworld/greeter_client.py"})); */
/*  // The client sends only 1 request and then exits. So we can wait for it to finish. */
/*  client.Wait(); */

/*  StopTransferDataThread(); */

/*  std::vector<TaggedRecordBatch> tablets = ConsumeRecords(SocketTraceConnector::kHTTPTableNum); */
/*  ASSERT_NOT_EMPTY_AND_GET_RECORDS(const types::ColumnWrapperRecordBatch& rb, tablets); */

/*  const std::vector<size_t> target_record_indices = */
/*      FindRecordIdxMatchesPID(rb, kHTTPUPIDIdx, server.process_pid()); */
/*  ASSERT_EQ(target_record_indices.size(), 1); */
/*  const size_t idx = target_record_indices.front(); */

/*  md::UPID upid(rb[kHTTPUPIDIdx]->Get<types::UInt128Value>(idx).val); */
/*  ASSERT_OK_AND_ASSIGN(md::UPID expected_upid, ToUPID(server.process_pid())); */
/*  EXPECT_EQ(upid, expected_upid); */

/*  EXPECT_THAT( */
/*      rb[kHTTPReqHeadersIdx]->Get<types::StringValue>(idx).string(), */
/*      AllOf(HasSubstr(R"(":authority":"localhost:50051")"), HasSubstr(R"(":method":"POST")"), */
/*            HasSubstr(R"(":scheme":"http")"), HasSubstr(R"("content-type":"application/grpc")"), */
/*            HasSubstr(R"("te":"trailers")"))); */
/*  EXPECT_THAT(rb[kHTTPReqBodyIdx]->Get<types::StringValue>(idx).string(), HasSubstr(R"(1: "you")")); */
/*  EXPECT_THAT(rb[kHTTPRespBodyIdx]->Get<types::StringValue>(idx).string(), */
/*              HasSubstr(R"(1: "Hello, you!")")); */

/*  EXPECT_THAT( */
/*      std::string(rb[kHTTPRespHeadersIdx]->Get<types::StringValue>(idx)), */
/*      AllOf(HasSubstr(R"(":status":"200")"), HasSubstr(R"("content-type":"application/grpc")"), */
/*            HasSubstr(R"("grpc-message":"")"), HasSubstr(R"("grpc-status":"0"})"))); */
/*  EXPECT_THAT(std::string(rb[kHTTPRemoteAddrIdx]->Get<types::StringValue>(idx)), */
/*              AnyOf(HasSubstr("127.0.0.1"), HasSubstr("::1"))); */
/*  EXPECT_EQ(2, rb[kHTTPMajorVersionIdx]->Get<types::Int64Value>(idx).val); */
/*  EXPECT_EQ(0, rb[kHTTPMinorVersionIdx]->Get<types::Int64Value>(idx).val); */
/*  EXPECT_EQ(static_cast<uint64_t>(HTTPContentType::kGRPC), */
/*            rb[kHTTPContentTypeIdx]->Get<types::Int64Value>(idx).val); */
/*} */

TEST_F(GoGRPCKProbeTraceTest, TestGolangGrpcService) {
  // TODO(yzhao): Add a --count flag to greeter client so we can test the case of multiple RPC calls
  // (multiple HTTP2 streams).
  SubProcess c;
  ASSERT_OK(c.Start(
      {client_path_, "-name=PixieLabs", "-once", absl::StrCat("-address=localhost:", s_port_)}));

  EXPECT_EQ(0, c.Wait()) << "Client should exit normally.";

  connector_->TransferData(ctx_.get(), kHTTPTableNum, &data_table_);
  std::vector<TaggedRecordBatch> tablets = data_table_.ConsumeRecords();
  ASSERT_FALSE(tablets.empty());
  types::ColumnWrapperRecordBatch record_batch = tablets[0].records;

  for (const auto& col : record_batch) {
    // Sometimes connect() returns 0, so we might have data from requester and responder.
    ASSERT_GE(col->Size(), 1);
  }
  const std::vector<size_t> target_record_indices =
      FindRecordIdxMatchesPID(record_batch, kHTTPUPIDIdx, s_.child_pid());
  // We should get exactly one record.
  ASSERT_THAT(target_record_indices, SizeIs(1));
  const size_t target_record_idx = target_record_indices.front();

  EXPECT_THAT(
      std::string(record_batch[kHTTPReqHeadersIdx]->Get<types::StringValue>(target_record_idx)),
      AllOf(HasSubstr(absl::Substitute(R"(":authority":"localhost:$0")", s_port_)),
            HasSubstr(R"(":method":"POST")"),
            HasSubstr(R"(":path":"/pl.stirling.protocols.http2.testing.Greeter/SayHello")"),
            HasSubstr(R"(":scheme":"http")"), HasSubstr(R"("content-type":"application/grpc")"),
            HasSubstr(R"("grpc-timeout")"), HasSubstr(R"("te":"trailers","user-agent")")));
  EXPECT_THAT(
      std::string(record_batch[kHTTPRespHeadersIdx]->Get<types::StringValue>(target_record_idx)),
      StrEq(R"({":status":"200",)"
            R"("content-type":"application/grpc",)"
            R"("grpc-message":"",)"
            R"("grpc-status":"0"})"));
  EXPECT_THAT(
      std::string(record_batch[kHTTPRemoteAddrIdx]->Get<types::StringValue>(target_record_idx)),
      AnyOf(HasSubstr("127.0.0.1"), HasSubstr("::1")));
  // TODO(oazizi): This expectation broke after the switch to server-side tracing.
  // Need to replace s_port_ with client port.
  // EXPECT_EQ(s_port_,
  //           record_batch[kHTTPRemotePortIdx]->Get<types::Int64Value>(target_record_idx).val);
  EXPECT_EQ(2, record_batch[kHTTPMajorVersionIdx]->Get<types::Int64Value>(target_record_idx).val);
  EXPECT_EQ(static_cast<uint64_t>(HTTPContentType::kGRPC),
            record_batch[kHTTPContentTypeIdx]->Get<types::Int64Value>(target_record_idx).val);

  EXPECT_THAT(GetHelloReply(record_batch, target_record_idx),
              EqualsProto(R"proto(message: "Hello PixieLabs")proto"));
}


class GRPCCppTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetUpSocketTraceConnector();
    SetUpGRPCServices();
  }

  void SetUpSocketTraceConnector() {
    // Force disable protobuf parsing to output the binary protobuf in record batch.
    // Also ensure test remain passing when the default changes.
    FLAGS_stirling_enable_parsing_protobufs = false;
    FLAGS_stirling_enable_grpc_kprobe_tracing = true;
    // TODO(yzhao): Stirling DFATALs if kprobe and uprobe are working simultaneously, which would
    // clobber the events. The default is already false, and Google test claims to restore flag
    // values after each test. Not sure why this is needed.
    FLAGS_stirling_enable_grpc_uprobe_tracing = false;
    FLAGS_stirling_disable_self_tracing = false;

    source_ = SocketTraceConnector::Create("bcc_grpc_trace");
    ASSERT_OK(source_->Init());

    auto* socket_trace_connector = static_cast<SocketTraceConnector*>(source_.get());
    ASSERT_NE(nullptr, socket_trace_connector);

    data_table_ = std::make_unique<DataTable>(kHTTPTable);

    ctx_ = std::make_unique<StandaloneContext>();
  }

  void SetUpGRPCServices() {
    runner_.RegisterService(&greeter_service_);
    runner_.RegisterService(&greeter2_service_);
    runner_.RegisterService(&streaming_greeter_service_);

    server_ = runner_.Run();

    auto* server_ptr = server_.get();
    server_thread_ = std::thread([server_ptr]() { server_ptr->Wait(); });

    client_channel_ = CreateInsecureGRPCChannel(absl::StrCat("127.0.0.1:", runner_.port()));
    greeter_stub_ = std::make_unique<GRPCStub<Greeter>>(client_channel_);
    greeter2_stub_ = std::make_unique<GRPCStub<Greeter2>>(client_channel_);
    streaming_greeter_stub_ = std::make_unique<GRPCStub<StreamingGreeter>>(client_channel_);
  }

  void TearDown() override {
    ASSERT_OK(source_->Stop());
    server_->Shutdown();
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  template <typename StubType, typename RPCMethodType>
  std::vector<::grpc::Status> CallRPC(StubType* stub, RPCMethodType method,
                                      const std::vector<std::string>& names) {
    std::vector<::grpc::Status> res;
    HelloRequest req;
    HelloReply resp;
    for (const auto& n : names) {
      req.set_name(n);
      res.push_back(stub->CallRPC(method, req, &resp));
    }
    return res;
  }

  std::unique_ptr<SourceConnector> source_;
  std::unique_ptr<StandaloneContext> ctx_;
  std::unique_ptr<DataTable> data_table_;

  GreeterService greeter_service_;
  Greeter2Service greeter2_service_;
  StreamingGreeterService streaming_greeter_service_;

  ServiceRunner runner_;
  std::unique_ptr<::grpc::Server> server_;
  std::thread server_thread_;

  std::shared_ptr<Channel> client_channel_;

  std::unique_ptr<GRPCStub<Greeter>> greeter_stub_;
  std::unique_ptr<GRPCStub<Greeter2>> greeter2_stub_;
  std::unique_ptr<GRPCStub<StreamingGreeter>> streaming_greeter_stub_;
};

TEST_F(GRPCCppTest, ParseTextProtoSimpleUnaryRPCCall) {
  FLAGS_stirling_enable_parsing_protobufs = true;
  CallRPC(greeter_stub_.get(), &Greeter::Stub::SayHello, {"pixielabs"});
  source_->TransferData(ctx_.get(), kHTTPTableNum, data_table_.get());

  std::vector<TaggedRecordBatch> tablets = data_table_->ConsumeRecords();
  ASSERT_FALSE(tablets.empty());
  types::ColumnWrapperRecordBatch record_batch = tablets[0].records;
  std::vector<size_t> indices = FindRecordIdxMatchesPID(record_batch, kHTTPUPIDIdx, getpid());
  ASSERT_THAT(indices, SizeIs(1));
  // Was parsed as an Empty message, all fields shown as unknown fields.
  EXPECT_EQ(std::string("1: \"Hello pixielabs!\""),
            std::string(record_batch[kHTTPRespBodyIdx]->Get<types::StringValue>(indices[0])));
}

TEST_F(GRPCCppTest, MixedGRPCServicesOnSameGRPCChannel) {
  // TODO(yzhao): Put CallRPC() calls inside multiple threads. That would cause header parsing
  // failures, debug and fix the root cause.
  CallRPC(greeter_stub_.get(), &Greeter::Stub::SayHello, {"pixielabs", "pixielabs", "pixielabs"});
  CallRPC(greeter_stub_.get(), &Greeter::Stub::SayHelloAgain,
          {"pixielabs", "pixielabs", "pixielabs"});
  CallRPC(greeter2_stub_.get(), &Greeter2::Stub::SayHi, {"pixielabs", "pixielabs", "pixielabs"});
  CallRPC(greeter2_stub_.get(), &Greeter2::Stub::SayHiAgain,
          {"pixielabs", "pixielabs", "pixielabs"});
  source_->TransferData(ctx_.get(), kHTTPTableNum, data_table_.get());

  std::vector<TaggedRecordBatch> tablets = data_table_->ConsumeRecords();
  ASSERT_FALSE(tablets.empty());
  types::ColumnWrapperRecordBatch record_batch = tablets[0].records;
  std::vector<size_t> indices = FindRecordIdxMatchesPID(record_batch, kHTTPUPIDIdx, getpid());
  EXPECT_THAT(indices, SizeIs(12));

  for (size_t idx : indices) {
    EXPECT_THAT(std::string(record_batch[kHTTPRemoteAddrIdx]->Get<types::StringValue>(idx)),
                AnyOf(HasSubstr("127.0.0.1"), HasSubstr("::1")));
    // TODO(oazizi): After switch to server-side tracing, this expectation is wrong,
    // but no easy way to get the port from client_channel_, so disabled this for now.
    // EXPECT_EQ(runner_.port(), record_batch[kHTTPRemotePortIdx]->Get<types::Int64Value>(idx).val);
    EXPECT_EQ(2, record_batch[kHTTPMajorVersionIdx]->Get<types::Int64Value>(idx).val);
    EXPECT_EQ(static_cast<uint64_t>(HTTPContentType::kGRPC),
              record_batch[kHTTPContentTypeIdx]->Get<types::Int64Value>(idx).val);
    EXPECT_THAT(GetHelloReply(record_batch, idx),
                AnyOf(EqualsProto(R"proto(message: "Hello pixielabs!")proto"),
                      EqualsProto(R"proto(message: "Hi pixielabs!")proto")));
  }
}

template <typename ProtoType>
std::vector<ProtoType> ParseProtobufRecords(absl::string_view buf) {
  std::vector<ProtoType> res;
  while (!buf.empty()) {
    const uint32_t len = nghttp2_get_uint32(reinterpret_cast<const uint8_t*>(buf.data()) + 1);
    ProtoType reply;
    reply.ParseFromArray(buf.data() + kGRPCMessageHeaderSizeInBytes, len);
    res.push_back(std::move(reply));
    buf.remove_prefix(kGRPCMessageHeaderSizeInBytes + len);
  }
  return res;
}

// Tests that a streaming RPC call will keep a HTTP2 stream open for the entirety of the RPC call.
// Therefore if the server takes a long time to return the results, the trace record would not
// be exported until then.
// TODO(yzhao): We need some way to export streaming RPC trace record gradually.
TEST_F(GRPCCppTest, ServerStreamingRPC) {
  HelloRequest req;
  req.set_name("pixielabs");
  req.set_count(3);

  std::vector<HelloReply> replies;

  ::grpc::Status st = streaming_greeter_stub_->CallServerStreamingRPC(
      &StreamingGreeter::Stub::SayHelloServerStreaming, req, &replies);
  EXPECT_TRUE(st.ok());
  EXPECT_THAT(replies, SizeIs(3));

  source_->TransferData(ctx_.get(), kHTTPTableNum, data_table_.get());

  std::vector<TaggedRecordBatch> tablets = data_table_->ConsumeRecords();
  ASSERT_FALSE(tablets.empty());
  types::ColumnWrapperRecordBatch record_batch = tablets[0].records;
  std::vector<size_t> indices = FindRecordIdxMatchesPID(record_batch, kHTTPUPIDIdx, getpid());
  EXPECT_THAT(indices, SizeIs(1));

  for (size_t idx : indices) {
    EXPECT_THAT(GetHelloRequest(record_batch, idx),
                EqualsProto(R"proto(name: "pixielabs" count: 3)proto"));
    EXPECT_THAT(ParseProtobufRecords<HelloReply>(
                    record_batch[kHTTPRespBodyIdx]->Get<types::StringValue>(idx)),
                ElementsAre(EqualsProto("message: 'Hello pixielabs for no. 0!'"),
                            EqualsProto("message: 'Hello pixielabs for no. 1!'"),
                            EqualsProto("message: 'Hello pixielabs for no. 2!'")));
  }
}

TEST_F(GRPCCppTest, BidirStreamingRPC) {
  HelloRequest req1;
  req1.set_name("foo");
  req1.set_count(1);

  HelloRequest req2;
  req2.set_name("bar");
  req2.set_count(1);

  std::vector<HelloReply> replies;

  ::grpc::Status st = streaming_greeter_stub_->CallBidirStreamingRPC(
      &StreamingGreeter::Stub::SayHelloBidirStreaming, {req1, req2}, &replies);
  EXPECT_TRUE(st.ok());
  EXPECT_THAT(replies, SizeIs(2));

  source_->TransferData(ctx_.get(), kHTTPTableNum, data_table_.get());

  std::vector<TaggedRecordBatch> tablets = data_table_->ConsumeRecords();
  ASSERT_FALSE(tablets.empty());
  types::ColumnWrapperRecordBatch record_batch = tablets[0].records;
  std::vector<size_t> indices = FindRecordIdxMatchesPID(record_batch, kHTTPUPIDIdx, getpid());
  EXPECT_THAT(indices, SizeIs(1));

  for (size_t idx : indices) {
    EXPECT_THAT(ParseProtobufRecords<HelloRequest>(
                    record_batch[kHTTPReqBodyIdx]->Get<types::StringValue>(idx)),
                ElementsAre(EqualsProto(R"proto(name: "foo" count: 1)proto"),
                            EqualsProto(R"proto(name: "bar" count: 1)proto")));
    EXPECT_THAT(ParseProtobufRecords<HelloReply>(
                    record_batch[kHTTPRespBodyIdx]->Get<types::StringValue>(idx)),
                ElementsAre(EqualsProto("message: 'Hello foo for no. 0!'"),
                            EqualsProto("message: 'Hello bar for no. 0!'")));
  }
}

// Do not initialize socket tracer to simulate the socket tracer missing head of start of the HTTP2
// connection.
class GRPCCppMiddleInterceptTest : public GRPCCppTest {
 protected:
  void SetUp() { SetUpGRPCServices(); }
};

TEST_F(GRPCCppMiddleInterceptTest, InterceptMiddleOfTheConnection) {
  CallRPC(greeter_stub_.get(), &Greeter::Stub::SayHello, {"pixielabs", "pixielabs", "pixielabs"});

  // Attach the probes after connection started.
  SetUpSocketTraceConnector();
  CallRPC(greeter_stub_.get(), &Greeter::Stub::SayHello, {"pixielabs", "pixielabs", "pixielabs"});
  source_->TransferData(ctx_.get(), kHTTPTableNum, data_table_.get());

  std::vector<TaggedRecordBatch> tablets = data_table_->ConsumeRecords();
  ASSERT_FALSE(tablets.empty());
  types::ColumnWrapperRecordBatch record_batch = tablets[0].records;
  std::vector<size_t> indices = FindRecordIdxMatchesPID(record_batch, kHTTPUPIDIdx, getpid());
  EXPECT_THAT(indices, SizeIs(3));
  for (size_t idx : indices) {
    // Header parsing would fail, because missing the head of start.
    // TODO(yzhao): We should device some meaningful mechanism for capturing headers, if inflation
    // failed.
    EXPECT_THAT(GetHelloReply(record_batch, idx),
                AnyOf(EqualsProto(R"proto(message: "Hello pixielabs!")proto"),
                      EqualsProto(R"proto(message: "Hi pixielabs!")proto")));
  }
}

class GRPCCppCallingNonRegisteredServiceTest : public GRPCCppTest {
 protected:
  void SetUp() {
    SetUpSocketTraceConnector();

    runner_.RegisterService(&greeter2_service_);
    server_ = runner_.Run();

    auto* server_ptr = server_.get();
    server_thread_ = std::thread([server_ptr]() { server_ptr->Wait(); });

    client_channel_ = CreateInsecureGRPCChannel(absl::StrCat("127.0.0.1:", runner_.port()));
    greeter_stub_ = std::make_unique<GRPCStub<Greeter>>(client_channel_);
  }
};

// Tests to show what is captured when calling a remote endpoint that does not implement the
// requested method.
TEST_F(GRPCCppCallingNonRegisteredServiceTest, ResultsAreAsExpected) {
  CallRPC(greeter_stub_.get(), &Greeter::Stub::SayHello, {"pixielabs", "pixielabs", "pixielabs"});
  source_->TransferData(ctx_.get(), kHTTPTableNum, data_table_.get());
  std::vector<TaggedRecordBatch> tablets = data_table_->ConsumeRecords();
  ASSERT_FALSE(tablets.empty());
  types::ColumnWrapperRecordBatch record_batch = tablets[0].records;
  std::vector<size_t> indices = FindRecordIdxMatchesPID(record_batch, kHTTPUPIDIdx, getpid());
  EXPECT_THAT(indices, SizeIs(3));
  for (size_t idx : indices) {
    EXPECT_THAT(std::string(record_batch[kHTTPRespHeadersIdx]->Get<types::StringValue>(idx)),
                StrEq(R"({":status":"200",)"
                      R"("content-type":"application/grpc",)"
                      R"("grpc-status":"12"})"));
    EXPECT_THAT(GetHelloRequest(record_batch, idx), EqualsProto(R"proto(name: "pixielabs")proto"));
    EXPECT_THAT(record_batch[kHTTPRespBodyIdx]->Get<types::StringValue>(idx), IsEmpty());
  }
}

}  // namespace stirling
}  // namespace px
