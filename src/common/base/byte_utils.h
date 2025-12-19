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

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if __cplusplus >= 202002L
#include <compare>
#endif

#include "src/common/base/logging.h"

// Provide char_traits specialization for uint8_t to enable std::basic_string_view<uint8_t>.
// This is required by BinaryDecoder and other code that uses basic_string_view<uint8_t>.
// Newer libc++ only provides char_traits for standard char types.
namespace std {
template <>
struct char_traits<uint8_t> {
  using char_type = uint8_t;
  using int_type = unsigned int;
  using off_type = streamoff;
  using pos_type = streampos;
  using state_type = mbstate_t;
#if __cplusplus >= 202002L
  using comparison_category = strong_ordering;
#endif

  static constexpr void assign(char_type& r, const char_type& a) noexcept { r = a; }
  static constexpr bool eq(char_type a, char_type b) noexcept { return a == b; }
  static constexpr bool lt(char_type a, char_type b) noexcept { return a < b; }

  static constexpr int compare(const char_type* s1, const char_type* s2, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      if (lt(s1[i], s2[i])) return -1;
      if (lt(s2[i], s1[i])) return 1;
    }
    return 0;
  }

  static constexpr size_t length(const char_type* s) {
    size_t len = 0;
    while (!eq(s[len], char_type())) ++len;
    return len;
  }

  static constexpr const char_type* find(const char_type* s, size_t n, const char_type& a) {
    for (size_t i = 0; i < n; ++i) {
      if (eq(s[i], a)) return s + i;
    }
    return nullptr;
  }

  static constexpr char_type* move(char_type* dest, const char_type* src, size_t n) {
    if (n == 0) return dest;
    // Use memmove for overlapping regions
    return static_cast<char_type*>(memmove(dest, src, n));
  }

  static constexpr char_type* copy(char_type* dest, const char_type* src, size_t n) {
    if (n == 0) return dest;
    return static_cast<char_type*>(memcpy(dest, src, n));
  }

  static constexpr char_type* assign(char_type* s, size_t n, char_type a) {
    for (size_t i = 0; i < n; ++i) s[i] = a;
    return s;
  }

  static constexpr int_type not_eof(int_type c) noexcept { return eq_int_type(c, eof()) ? 0 : c; }
  static constexpr char_type to_char_type(int_type c) noexcept { return static_cast<char_type>(c); }
  static constexpr int_type to_int_type(char_type c) noexcept { return static_cast<int_type>(c); }
  static constexpr bool eq_int_type(int_type c1, int_type c2) noexcept { return c1 == c2; }
  static constexpr int_type eof() noexcept { return static_cast<int_type>(-1); }
};
}  // namespace std

namespace px {
namespace utils {

// Use std::vector and std::span for byte operations instead of basic_string<uint8_t>.
// The char_traits specialization above is still needed for BinaryDecoder and other code
// that uses std::basic_string_view<uint8_t>.
using u8string = std::vector<uint8_t>;
using u8string_view = std::span<const uint8_t>;

template <size_t N>
void ReverseBytes(const uint8_t* x, uint8_t* y) {
  for (size_t k = 0; k < N; k++) {
    y[k] = x[N - k - 1];
  }
}

template <typename TCharType, size_t N>
void ReverseBytes(const TCharType (&x)[N], TCharType (&y)[N]) {
  const uint8_t* x_bytes = reinterpret_cast<const uint8_t*>(x);
  uint8_t* y_bytes = reinterpret_cast<uint8_t*>(y);
  ReverseBytes<N>(x_bytes, y_bytes);
}

template <typename T>
T ReverseBytes(const T* x) {
  T y;
  const uint8_t* x_bytes = reinterpret_cast<const uint8_t*>(x);
  uint8_t* y_bytes = reinterpret_cast<uint8_t*>(&y);
  ReverseBytes<sizeof(T)>(x_bytes, y_bytes);
  return y;
}

/**
 * Convert a little-endian string of bytes to an integer.
 *
 * @tparam T The receiver int type.
 * @tparam N Number of bytes to process from the source buffer. N must be <= sizeof(T).
 * If N < sizeof(T), the remaining bytes (MSBs) are assumed to be zero.
 * @param buf The sequence of bytes.
 * @return The decoded int value.
 */
template <typename T, typename TCharType = char, size_t N = sizeof(T)>
T LEndianBytesToIntInternal(std::basic_string_view<TCharType> buf) {
  // Doesn't make sense to process more bytes than the destination type.
  // Less bytes is okay, on the other hand, since the value will still fit.
  static_assert(N <= sizeof(T));

  // Source buffer must have enough bytes.
  DCHECK_GE(buf.size(), N);

  T result = 0;
  for (size_t i = 0; i < N; i++) {
    result = static_cast<uint8_t>(buf[N - 1 - i]) | (result << 8);
  }
  return result;
}

template <typename T, size_t N = sizeof(T)>
T LEndianBytesToInt(std::string_view buf) {
  return LEndianBytesToIntInternal<T, char, N>(buf);
}

template <typename T, size_t N = sizeof(T), typename TCharType = char>
T LEndianBytesToInt(std::basic_string_view<TCharType> buf) {
  return LEndianBytesToIntInternal<T, TCharType, N>(buf);
}

// Overload for std::span<const uint8_t>
template <typename T, size_t N = sizeof(T)>
T LEndianBytesToInt(std::span<const uint8_t> buf) {
  static_assert(N <= sizeof(T));
  DCHECK_GE(buf.size(), N);

  T result = 0;
  for (size_t i = 0; i < N; i++) {
    result = static_cast<uint8_t>(buf[N - 1 - i]) | (result << 8);
  }
  return result;
}

/**
 * Convert a little-endian string of bytes to a float/double.
 *
 * @tparam T The receiver float type.
 * @param buf The sequence of bytes.
 * @return The decoded float value.
 */
template <typename TFloatType>
TFloatType LEndianBytesToFloat(std::string_view buf) {
  // Source buffer must have enough bytes.
  DCHECK_GE(buf.size(), sizeof(TFloatType));

  TFloatType val;
  std::memcpy(&val, buf.data(), sizeof(TFloatType));
  return val;
}

/**
 * Convert an int to a little-endian string of bytes.
 *
 * @tparam TCharType The char type to use in the string (e.g. char vs uint8_t).
 * @param num The number to convert.
 * @param result the destination buffer.
 */
template <typename TCharType, size_t N>
void IntToLEndianBytes(int64_t num, TCharType (&result)[N]) {
  static_assert(N <= sizeof(int64_t));
  for (size_t i = 0; i < N; i++) {
    result[i] = (num >> (i * 8));
  }
}

/**
 * Convert an int to a big-endian string of bytes.
 *
 * @tparam TCharType The char type to use in the string (e.g. char vs uint8_t).
 * @param num The number to convert.
 * @param result the destination buffer.
 */
template <typename TCharType, size_t N>
void IntToBEndianBytes(int64_t num, TCharType (&result)[N]) {
  static_assert(N <= sizeof(int64_t));
  for (size_t i = 0; i < N; i++) {
    result[i] = (num >> ((N - i - 1) * 8));
  }
}

/**
 * Convert a big-endian string of bytes to an integer.
 *
 * @tparam T The receiver int type.
 * @tparam N Number of bytes to process from the source buffer. N must be <= sizeof(T).
 * If N < sizeof(T), the remaining bytes (MSBs) are assumed to be zero.
 * @param buf The sequence of bytes.
 * @return The decoded int value.
 */
template <typename T, typename TCharType, size_t N = sizeof(T)>
T BEndianBytesToIntInternal(std::basic_string_view<TCharType> buf) {
  // Doesn't make sense to process more bytes than the destination type.
  // Less bytes is okay, on the other hand, since the value will still fit.
  static_assert(N <= sizeof(T));

  // Source buffer must have enough bytes.
  DCHECK_GE(buf.size(), N);

  T result = 0;
  for (size_t i = 0; i < N; i++) {
    result = static_cast<uint8_t>(buf[i]) | (result << 8);
  }
  return result;
}

template <typename T, size_t N = sizeof(T)>
T BEndianBytesToInt(std::string_view buf) {
  return BEndianBytesToIntInternal<T, char, N>(buf);
}

template <typename T, size_t N = sizeof(T), typename TCharType = char>
T BEndianBytesToInt(std::basic_string_view<TCharType> buf) {
  return BEndianBytesToIntInternal<T, TCharType, N>(buf);
}

// Overload for std::span<const uint8_t>
template <typename T, size_t N = sizeof(T)>
T BEndianBytesToInt(std::span<const uint8_t> buf) {
  static_assert(N <= sizeof(T));
  DCHECK_GE(buf.size(), N);

  T result = 0;
  for (size_t i = 0; i < N; i++) {
    result = static_cast<uint8_t>(buf[i]) | (result << 8);
  }
  return result;
}

/**
 * Convert a big-endian string of bytes to a float/double.
 *
 * @tparam T The receiver float type.
 * @param buf The sequence of bytes.
 * @return The decoded float value.
 */
template <typename TFloatType>
TFloatType BEndianBytesToFloat(std::string_view buf) {
  // Source buffer must have enough bytes.
  DCHECK_GE(buf.size(), sizeof(TFloatType));

  // Note that unlike LEndianBytesToFloat, we don't use memcpy to align the data.
  // That is because ReverseBytes will naturally cause the alignment to occur when it copies bytes.
  const TFloatType* ptr = reinterpret_cast<const TFloatType*>(buf.data());
  return ReverseBytes<TFloatType>(ptr);
}

template <typename TValueType>
TValueType MemCpy(const void* buf) {
  TValueType tmp;
  memcpy(&tmp, buf, sizeof(tmp));
  return tmp;
}

template <typename TValueType, typename TByteType>
TValueType MemCpy(std::basic_string_view<TByteType> buf) {
  static_assert(sizeof(TByteType) == 1);
  return MemCpy<TValueType>(static_cast<const void*>(buf.data()));
}

template <typename TValueType, typename TByteType>
TValueType MemCpy(const TByteType* buf) {
  static_assert(sizeof(TByteType) == 1);
  return MemCpy<TValueType>(static_cast<const void*>(buf));
}

// Overload for std::span
template <typename TValueType, typename TByteType>
TValueType MemCpy(std::span<TByteType> buf) {
  static_assert(sizeof(TByteType) == 1);
  return MemCpy<TValueType>(static_cast<const void*>(buf.data()));
}

}  // namespace utils
}  // namespace px
