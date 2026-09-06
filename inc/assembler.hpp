#ifndef SS_ASSEMBLER_HPP
#define SS_ASSEMBLER_HPP

#include <memory>
#include <string>

namespace ss {

class Assembler {
public:
  Assembler();
  ~Assembler();
  Assembler(Assembler &&) noexcept;
  Assembler &operator=(Assembler &&) noexcept;
  Assembler(const Assembler &) = delete; // asembler ima svoje stanje
  Assembler &operator=(const Assembler &) = delete;

  void assemble(const std::string &inputPath, const std::string &outputPath);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

#endif
