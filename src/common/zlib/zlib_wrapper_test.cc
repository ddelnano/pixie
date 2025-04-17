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

#include "src/common/zlib/zlib_wrapper.h"
#include <zlib.h>
#include <string>
#include <iostream>

#include "src/common/testing/testing.h"

namespace px {

class ZlibTest : public ::testing::Test {
 private:
  inline static const uint8_t compressed_str_bytes_[] = {
      0x1f, 0x8b, 0x08, 0x00, 0x37, 0xf0, 0xbf, 0x5c, 0x00, 0x03, 0x0b,
      0xc9, 0xc8, 0x2c, 0x56, 0x00, 0xa2, 0x44, 0x85, 0x92, 0xd4, 0xe2,
      0x12, 0x2e, 0x00, 0x8c, 0x2d, 0xc0, 0xfa, 0x0f, 0x00, 0x00, 0x00};
  inline static const std::string expected_result_ = "This is a test\n";
 public:
  std::string GetCompressedString() {
    return std::string(reinterpret_cast<const char*>(compressed_str_bytes_),
                       sizeof(compressed_str_bytes_));
  }
  std::string GetExpectedResult() { return expected_result_; }
};

// Test basic decompression of valid gzip data
TEST_F(ZlibTest, basic_decompression_test) {
  auto result = px::zlib::Inflate(GetCompressedString());
  EXPECT_OK_AND_EQ(result, GetExpectedResult());
  std::cout << "\nBasic decompression result: '" << result.ValueOrDie() << "'" << std::endl;
}

// Test handling of empty input
TEST_F(ZlibTest, empty_input_test) {
  auto result = px::zlib::Inflate("");
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.ValueOrDie(), "");
  std::cout << "\nEmpty input result: '" << result.ValueOrDie() << "'" << std::endl;
}

// Test handling of invalid gzip header
TEST_F(ZlibTest, invalid_header_test) {
  std::string invalid_header = "This is not a gzip header";
  auto result = px::zlib::Inflate(invalid_header);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), px::statuspb::INTERNAL);
  EXPECT_TRUE(result.status().msg().find("zlib inflate failed") != std::string::npos);
  std::cout << "\nInvalid header error: " << result.status().msg() << std::endl;
}

// Test handling of corrupted data in the middle of the stream
TEST_F(ZlibTest, corrupted_data_test) {
  std::string corrupted_data = GetCompressedString();
  corrupted_data[15] = 0xFF;
  corrupted_data[16] = 0xFF;
  
  auto result = px::zlib::Inflate(corrupted_data);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), px::statuspb::INTERNAL);
  EXPECT_TRUE(result.status().msg().find("zlib inflate failed") != std::string::npos);
  std::cout << "\nCorrupted data error: " << result.status().msg() << std::endl;
}

// Test handling of large compressed data
TEST_F(ZlibTest, large_data_test) {
  // Create a large input string
  std::string large_input(1024 * 1024, 'A'); // 1MB of 'A's
  
  // Compress it first
  z_stream zs = {};
  ASSERT_EQ(deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY), Z_OK);

  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(large_input.data()));
  zs.avail_in = large_input.size();

  std::string compressed;
  compressed.resize(large_input.size());

  zs.next_out = reinterpret_cast<Bytef*>(&compressed[0]);
  zs.avail_out = compressed.size();

  int ret = deflate(&zs, Z_FINISH);
  ASSERT_EQ(ret, Z_STREAM_END);

  compressed.resize(zs.total_out);
  deflateEnd(&zs);

  // Test partial decompression
  std::string truncated = compressed.substr(0, compressed.size() / 2);
  auto result = px::zlib::Inflate(truncated);
  EXPECT_TRUE(result.ok());
  
  std::string decompressed = result.ValueOrDie();
  EXPECT_TRUE(decompressed.find("[PARTIAL DECOMPRESSION]") != std::string::npos);
  EXPECT_EQ(decompressed.substr(decompressed.length() - std::string("[PARTIAL DECOMPRESSION] ").length()), 
            "[PARTIAL DECOMPRESSION] ");
  
  std::cout << "\nLarge data decompression result (first 50 chars): '";
  if (decompressed.size() > 50) {
    std::cout << decompressed.substr(0, 50) << "...'" << std::endl;
  } else {
    std::cout << decompressed << "'" << std::endl;
  }
  std::cout << "Total decompressed size: " << decompressed.size() << " bytes" << std::endl;
}

// Test handling of truncated gzip data
TEST_F(ZlibTest, truncated_data_test) {
  std::string truncated_data = GetCompressedString().substr(0, 20);
  auto result = px::zlib::Inflate(truncated_data);
  EXPECT_TRUE(result.ok());
  
  std::string decompressed = result.ValueOrDie();
  EXPECT_TRUE(decompressed.find("[PARTIAL DECOMPRESSION]") != std::string::npos);
  EXPECT_EQ(decompressed.substr(decompressed.length() - std::string("[PARTIAL DECOMPRESSION] ").length()), 
            "[PARTIAL DECOMPRESSION] ");
  
  std::cout << "\nTruncated data decompression result: '" << decompressed << "'" << std::endl;
}

}  // namespace px