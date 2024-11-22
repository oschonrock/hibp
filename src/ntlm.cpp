#include "hibp.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace hibp {

hibp::pawned_pw_ntlm ntlm(const std::string& pw) {
  constexpr std::uint32_t INIT_A = 0x67452301;
  constexpr std::uint32_t INIT_B = 0xefcdab89;
  constexpr std::uint32_t INIT_C = 0x98badcfe;
  constexpr std::uint32_t INIT_D = 0x10325476;

  constexpr std::uint32_t SQRT_2 = 0x5a827999;
  constexpr std::uint32_t SQRT_3 = 0x6ed9eba1;

  std::array<std::uint32_t, 16> nt_buffer{};
  std::array<std::uint32_t, 4>  output{};

  // The length of pw need to be <= 27
  auto length = static_cast<unsigned>(pw.length());

  // Prepare the string for hash calculation
  unsigned i = 0;
  for (; i < length / 2; i++) {
    nt_buffer[i] =
        static_cast<unsigned>(pw[2UL * i]) | (static_cast<unsigned>(pw[2 * i + 1]) << 16U);
  }

  // padding
  if (length % 2 == 1) {
    nt_buffer[i] = static_cast<unsigned>(pw[length - 1]) | 0x800000U;
  } else {
    nt_buffer[i] = 0x80;
  }

  // put the length
  nt_buffer[14] = length << 4U;

  // NTLM hash calculation
  std::uint32_t a = INIT_A;
  std::uint32_t b = INIT_B;
  std::uint32_t c = INIT_C;
  std::uint32_t d = INIT_D;

  // clang-format off
  // Round 1
  a += (d ^ (b & (c ^ d)))  +  nt_buffer[0] ; a = (a << 3U ) | (a >> 29U);
  d += (c ^ (a & (b ^ c)))  +  nt_buffer[1] ; d = (d << 7U ) | (d >> 25U);
  c += (b ^ (d & (a ^ b)))  +  nt_buffer[2] ; c = (c << 11U) | (c >> 21U);
  b += (a ^ (c & (d ^ a)))  +  nt_buffer[3] ; b = (b << 19U) | (b >> 13U);

  a += (d ^ (b & (c ^ d)))  +  nt_buffer[4] ; a = (a << 3U ) | (a >> 29U);
  d += (c ^ (a & (b ^ c)))  +  nt_buffer[5] ; d = (d << 7U ) | (d >> 25U);
  c += (b ^ (d & (a ^ b)))  +  nt_buffer[6] ; c = (c << 11U) | (c >> 21U);
  b += (a ^ (c & (d ^ a)))  +  nt_buffer[7] ; b = (b << 19U) | (b >> 13U);

  a += (d ^ (b & (c ^ d)))  +  nt_buffer[8] ; a = (a << 3U ) | (a >> 29U);
  d += (c ^ (a & (b ^ c)))  +  nt_buffer[9] ; d = (d << 7U ) | (d >> 25U);
  c += (b ^ (d & (a ^ b)))  +  nt_buffer[10]; c = (c << 11U) | (c >> 21U);
  b += (a ^ (c & (d ^ a)))  +  nt_buffer[11]; b = (b << 19U) | (b >> 13U);

  a += (d ^ (b & (c ^ d)))  +  nt_buffer[12]; a = (a << 3U ) | (a >> 29U);
  d += (c ^ (a & (b ^ c)))  +  nt_buffer[13]; d = (d << 7U ) | (d >> 25U);
  c += (b ^ (d & (a ^ b)))  +  nt_buffer[14]; c = (c << 11U) | (c >> 21U);
  b += (a ^ (c & (d ^ a)))  +  nt_buffer[15]; b = (b << 19U) | (b >> 13U);

  // Round 2
  a += ((b & (c | d)) | (c & d)) + nt_buffer[0]  + SQRT_2; a = (a << 3U ) | (a >> 29U);
  d += ((a & (b | c)) | (b & c)) + nt_buffer[4]  + SQRT_2; d = (d << 5U ) | (d >> 27U);
  c += ((d & (a | b)) | (a & b)) + nt_buffer[8]  + SQRT_2; c = (c << 9U ) | (c >> 23U);
  b += ((c & (d | a)) | (d & a)) + nt_buffer[12] + SQRT_2; b = (b << 13U) | (b >> 19U);

  a += ((b & (c | d)) | (c & d)) + nt_buffer[1]  + SQRT_2; a = (a << 3U ) | (a >> 29U);
  d += ((a & (b | c)) | (b & c)) + nt_buffer[5]  + SQRT_2; d = (d << 5U ) | (d >> 27U);
  c += ((d & (a | b)) | (a & b)) + nt_buffer[9]  + SQRT_2; c = (c << 9U ) | (c >> 23U);
  b += ((c & (d | a)) | (d & a)) + nt_buffer[13] + SQRT_2; b = (b << 13U) | (b >> 19U);

  a += ((b & (c | d)) | (c & d)) + nt_buffer[2]  + SQRT_2; a = (a << 3U ) | (a >> 29U);
  d += ((a & (b | c)) | (b & c)) + nt_buffer[6]  + SQRT_2; d = (d << 5U ) | (d >> 27U);
  c += ((d & (a | b)) | (a & b)) + nt_buffer[10] + SQRT_2; c = (c << 9U ) | (c >> 23U);
  b += ((c & (d | a)) | (d & a)) + nt_buffer[14] + SQRT_2; b = (b << 13U) | (b >> 19U);

  a += ((b & (c | d)) | (c & d)) + nt_buffer[3]  + SQRT_2; a = (a << 3U ) | (a >> 29U);
  d += ((a & (b | c)) | (b & c)) + nt_buffer[7]  + SQRT_2; d = (d << 5U ) | (d >> 27U);
  c += ((d & (a | b)) | (a & b)) + nt_buffer[11] + SQRT_2; c = (c << 9U ) | (c >> 23U);
  b += ((c & (d | a)) | (d & a)) + nt_buffer[15] + SQRT_2; b = (b << 13U) | (b >> 19U);

  // Round 3 
  a += (d ^ c ^ b) + nt_buffer[0]  +  SQRT_3; a = (a << 3U ) | (a >> 29U);
  d += (c ^ b ^ a) + nt_buffer[8]  +  SQRT_3; d = (d << 9U ) | (d >> 23U);
  c += (b ^ a ^ d) + nt_buffer[4]  +  SQRT_3; c = (c << 11U) | (c >> 21U);
  b += (a ^ d ^ c) + nt_buffer[12] +  SQRT_3; b = (b << 15U) | (b >> 17U);

  a += (d ^ c ^ b) + nt_buffer[2]  +  SQRT_3; a = (a << 3U ) | (a >> 29U);
  d += (c ^ b ^ a) + nt_buffer[10] +  SQRT_3; d = (d << 9U ) | (d >> 23U);
  c += (b ^ a ^ d) + nt_buffer[6]  +  SQRT_3; c = (c << 11U) | (c >> 21U);
  b += (a ^ d ^ c) + nt_buffer[14] +  SQRT_3; b = (b << 15U) | (b >> 17U);

  a += (d ^ c ^ b) + nt_buffer[1]  +  SQRT_3; a = (a << 3U ) | (a >> 29U);
  d += (c ^ b ^ a) + nt_buffer[9]  +  SQRT_3; d = (d << 9U ) | (d >> 23U);
  c += (b ^ a ^ d) + nt_buffer[5]  +  SQRT_3; c = (c << 11U) | (c >> 21U);
  b += (a ^ d ^ c) + nt_buffer[13] +  SQRT_3; b = (b << 15U) | (b >> 17U);

  a += (d ^ c ^ b) + nt_buffer[3]  +  SQRT_3; a = (a << 3U ) | (a >> 29U);
  d += (c ^ b ^ a) + nt_buffer[11] +  SQRT_3; d = (d << 9U ) | (d >> 23U);
  c += (b ^ a ^ d) + nt_buffer[7]  +  SQRT_3; c = (c << 11U) | (c >> 21U);
  b += (a ^ d ^ c) + nt_buffer[15] +  SQRT_3; b = (b << 15U) | (b >> 17U);

  // clang-format on
  output[0] = a + INIT_A;
  output[1] = b + INIT_B;
  output[2] = c + INIT_C;
  output[3] = d + INIT_D;

  hibp::pawned_pw_ntlm hash;
  // the 4 unsigned ints in series contain exactly what we need, despite little endian byte order
  std::memcpy(&hash.hash, &output, 16);
  return hash;
}
} // namespace hibp
