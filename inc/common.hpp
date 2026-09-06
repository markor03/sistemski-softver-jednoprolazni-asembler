#ifndef SS_COMMON_HPP
#define SS_COMMON_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
/* male operacije koje koristi vise delova i klasa */
namespace ss {

class Error : public std::runtime_error {
public:
  explicit Error(const std::string &message) : std::runtime_error(message) {}
};

std::string trim(const std::string &text);
std::string lower(std::string text);
uint32_t parseNumber(const std::string &text);
bool fitsSigned12(int64_t value);
void write32(std::vector<uint8_t> &data, std::size_t offset, uint32_t value);
uint32_t read32(const std::vector<uint8_t> &data, std::size_t offset);
std::string hexBytes(const std::vector<uint8_t> &data);
std::vector<uint8_t> parseHexBytes(const std::string &text);

}

#endif
