#include <codecvt>
#include <openssl/md4.h> // OpenSSL library for MD4
#include <string>
#include <locale>
#include <array>

namespace hibp {

std::string utf8_to_utf16_le(const std::string& utf8Str) {
  std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
  std::u16string utf16_str = converter.from_bytes(utf8Str);

  // Convert UTF-16 to a byte string in little endian, on any platform
  std::string utf16_le;
  for (char16_t ch: utf16_str) {
    utf16_le.push_back(static_cast<char>(ch & 0xFF));        // NOLINT
    utf16_le.push_back(static_cast<char>((ch >> 8) & 0xFF)); // NOLINT
  }

  return utf16_le;
}

std::array<std::byte, 16> ntlm(const std::string& password) {

  std::string utf16LE = utf8_to_utf16_le(password);

  std::array<std::byte, 16> hash{};

  MD4(reinterpret_cast<const unsigned char*>(utf16LE.data()), utf16LE.size(), // NOLINT
      reinterpret_cast<unsigned char*>(hash.data()));                         // NOLINT

  return hash;
}
} // namespace hibp
