#ifndef SS_LINKER_HPP
#define SS_LINKER_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ss {

struct LinkerOptions {
  bool hex = false; // pravi se .hex
  bool relocatable = false; // pravi se novi .o
  std::string output;
  std::map<std::string, uint32_t> places; // pocetne adrese sekcija
  std::vector<std::string> inputs;
};

class Linker {
public:
  Linker();
  ~Linker();
  Linker(Linker &&) noexcept;
  Linker &operator=(Linker &&) noexcept;
  Linker(const Linker &) = delete;
  Linker &operator=(const Linker &) = delete;

  void link(const LinkerOptions &options);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

#endif
