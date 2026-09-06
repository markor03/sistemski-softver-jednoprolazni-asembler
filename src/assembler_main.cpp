#include "assembler.hpp"
#include "common.hpp"

#include <iostream>
#include <string>

/* obrada komandne linije */
int main(int argc, char **argv) {
  try {
    std::string input, output;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-o") {
        if (++i >= argc) throw ss::Error("opcija -o zahteva naziv datoteke");
        output = argv[i];
      } else if (!arg.empty() && arg[0] == '-') {
        throw ss::Error("nepoznata opcija: " + arg);
      } else if (input.empty()) {
        input = arg;
      } else {
        throw ss::Error("asembler prihvata tacno jednu ulaznu datoteku");
      }
    }
    if (input.empty() || output.empty())
      throw ss::Error("upotreba: asembler -o <izlaz.o> <ulaz.s>");
    ss::Assembler assembler;
    assembler.assemble(input, output); // pokretanje asemblera
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "asembler: greska: " << error.what() << '\n';
    return 1;
  }
}
