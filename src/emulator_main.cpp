#include "common.hpp"
#include "emulator.hpp"

#include <iostream>
  // proverava da li postoji tacno 1 .hex fajl i poziva run
int main(int argc, char **argv) {
  try {
    if (argc != 2) throw ss::Error("upotreba: emulator <program.hex>");
    ss::Emulator emulator;
    emulator.run(argv[1]);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "emulator: greska: " << error.what() << '\n';
    return 1;
  }
}
