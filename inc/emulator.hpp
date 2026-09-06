#ifndef SS_EMULATOR_HPP
#define SS_EMULATOR_HPP

#include <memory>
#include <string>

namespace ss {

class Emulator {
public:
  Emulator();
  ~Emulator();
  Emulator(Emulator &&) noexcept;
  Emulator &operator=(Emulator &&) noexcept;
  Emulator(const Emulator &) = delete;
  Emulator &operator=(const Emulator &) = delete;

  void run(const std::string &hexPath);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

#endif
