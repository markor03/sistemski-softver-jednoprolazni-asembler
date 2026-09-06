#include "common.hpp"
#include "linker.hpp"

#include <iostream>
#include <string>
  // komandna linija
int main(int argc, char **argv) {
  try {
    ss::LinkerOptions options;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "-hex") options.hex = true;
      else if (arg == "-relocatable") options.relocatable = true;
      else if (arg == "-o") {
        if (++i >= argc) throw ss::Error("opcija -o zahteva naziv datoteke");
        options.output = argv[i];
      } else if (arg.rfind("-place=", 0) == 0 || arg.rfind("--place=", 0) == 0) {
        std::size_t equal = arg.find('=');
        std::string value = arg.substr(equal + 1);
        std::size_t at = value.rfind('@');
        if (at == std::string::npos || at == 0 || at + 1 == value.size())
          throw ss::Error("-place sintaksa je -place=<sekcija>@<adresa>");
        std::string section = value.substr(0, at);
        if (options.places.count(section)) throw ss::Error("vise -place opcija za sekciju " + section);
        options.places[section] = ss::parseNumber(value.substr(at + 1));
      } else if (!arg.empty() && arg[0] == '-') {
        throw ss::Error("nepoznata opcija: " + arg);
      } else options.inputs.push_back(arg);
    }
    if (options.hex == options.relocatable)
      throw ss::Error("mora biti navedena tacno jedna opcija: -hex ili -relocatable");
    if (options.output.empty() || options.inputs.empty())
      throw ss::Error("upotreba: linker (-hex|-relocatable) [-place=s@a] -o <izlaz> <ulaz.o>...");
    ss::Linker linker;
    linker.link(options);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "linker: greska: " << error.what() << '\n';
    return 1;
  }
}
