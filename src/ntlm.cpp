#include "md4.h" // copy of OpenSSL MD4 from openrsync
#include <array>
#include <codecvt>
#include <cstddef>
#include <locale>
#include <string>

namespace hibp {

std::string utf8_to_utf16_le(const std::string& utf8Str) {
  std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
  const std::u16string utf16_str = converter.from_bytes(utf8Str);

  // Convert UTF-16 to a byte string in little endian, on any platform
  std::string utf16_le;
  for (const char16_t ch: utf16_str) {
    utf16_le.push_back(static_cast<char>(ch & 0xFF));        // NOLINT
    utf16_le.push_back(static_cast<char>((ch >> 8) & 0xFF)); // NOLINT
  }

  return utf16_le;
}

std::array<std::byte, 16> ntlm(const std::string& password) {

  std::string utf_16_le = utf8_to_utf16_le(password);

  std::array<std::byte, 16> hash{};

  MD4_CTX ctx;
  MD4_Init(&ctx);

  MD4_Update(&ctx, reinterpret_cast<const unsigned char*>(utf_16_le.data()), // NOLINT
             utf_16_le.size());

  MD4_Final(reinterpret_cast<unsigned char*>(hash.data()), &ctx); // NOLINT

  return hash;
}
} // namespace hibp
