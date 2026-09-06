#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>

namespace ss {
  // uklanja belinu s kraja
std::string trim(const std::string &text) {
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
  return text.substr(begin, end - begin);
}
  // pretvara u mala slova, pomocna
std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}
  // cita decimalne i hexa literale, koriste svi osim asemblera
uint32_t parseNumber(const std::string &input) {
  std::string text = trim(input);
  if (text.empty()) throw Error("ocekuje se literal");
  bool negative = text[0] == '-';
  bool positive = text[0] == '+';
  std::size_t pos = (negative || positive) ? 1 : 0;
  if (pos == text.size()) throw Error("neispravan literal: " + input);
  int base = 10;
  if (text.size() >= pos + 2 && text[pos] == '0' && (text[pos + 1] == 'x' || text[pos + 1] == 'X')) {
    base = 16;
    pos += 2;
  }
  if (pos == text.size()) throw Error("neispravan literal: " + input);
  uint64_t magnitude = 0;
  for (; pos < text.size(); ++pos) {
    unsigned char c = static_cast<unsigned char>(text[pos]);
    int digit = -1;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (base == 16 && c >= 'a' && c <= 'f') digit = 10 + c - 'a';
    else if (base == 16 && c >= 'A' && c <= 'F') digit = 10 + c - 'A';
    if (digit < 0 || digit >= base) throw Error("neispravan literal: " + input);
    if (magnitude > (std::numeric_limits<uint64_t>::max() - digit) / base)
      throw Error("literal je van opsega: " + input);
    magnitude = magnitude * base + static_cast<unsigned>(digit);
  }
  if (negative) {
    if (magnitude > 0x80000000ULL) throw Error("literal je van 32-bitnog opsega: " + input);
    return static_cast<uint32_t>(0ULL - magnitude);
  }
  if (magnitude > 0xFFFFFFFFULL) throw Error("literal je van 32-bitnog opsega: " + input);
  return static_cast<uint32_t>(magnitude);
}
  // proverava opseg od 12b, koristi ga asembler
bool fitsSigned12(int64_t value) { return value >= -2048 && value <= 2047; }
  // upisuje 32b little endian, koriste ga asembler i linker
void write32(std::vector<uint8_t> &data, std::size_t offset, uint32_t value) {
  if (offset + 4 > data.size()) throw Error("upis van granica sekcije");
  for (unsigned i = 0; i < 4; ++i) data[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
  // cita 32b little endian, pomocna
uint32_t read32(const std::vector<uint8_t> &data, std::size_t offset) {
  if (offset + 4 > data.size()) throw Error("citanje van granica sekcije");
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= static_cast<uint32_t>(data[offset + i]) << (8 * i);
  return value;
}
  // pretvara bajtove u hexa
std::string hexBytes(const std::vector<uint8_t> &data) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::uppercase;
  for (uint8_t byte : data) out << std::setw(2) << static_cast<unsigned>(byte);
  return out.str();
}
  // obrnuta konverzija, hexa tekst u bajtove
std::vector<uint8_t> parseHexBytes(const std::string &input) {
  std::string text = trim(input);
  if (text.size() % 2 != 0) throw Error("neparan broj heksadecimalnih cifara");
  std::vector<uint8_t> result;
  result.reserve(text.size() / 2);
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
  };
  for (std::size_t i = 0; i < text.size(); i += 2) {
    int hi = digit(text[i]), lo = digit(text[i + 1]);
    if (hi < 0 || lo < 0) throw Error("neispravan heksadecimalni sadrzaj");
    result.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return result;
}

} // namespace ss
