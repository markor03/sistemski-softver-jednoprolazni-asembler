#ifndef SS_OBJECT_HPP
#define SS_OBJECT_HPP

#include "common.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ss {

enum class Binding { Local, Global };
enum class SymbolKind { Undefined, Absolute, Section };
enum class RelocTarget { Symbol, Section };

struct Section {
  std::string name;
  std::vector<uint8_t> data;
};

struct Symbol {
  std::string name;
  Binding binding = Binding::Local;
  SymbolKind kind = SymbolKind::Undefined;
  std::string section;
  uint32_t value = 0; // pomeraj od pocetka sekcije
};

// gde linker upisuje konacnu adresu, adresa simbola (sekcije) + addend
struct Relocation {
  std::string section;
  uint32_t offset = 0;
  RelocTarget targetKind = RelocTarget::Symbol;
  std::string target;
  int64_t addend = 0;
};

// objedinjuje sekcije, simbole i relokacione zapise
struct ObjectFile {
  std::vector<Section> sections;
  std::vector<Symbol> symbols;
  std::vector<Relocation> relocations;
};

void saveObject(const ObjectFile &object, const std::string &path);
ObjectFile loadObject(const std::string &path);

}

#endif
