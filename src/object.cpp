#include "object.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

// provera formata, velicine sekcija, vrste simbola i relokacija, sadrzaj
namespace ss {

void saveObject(const ObjectFile &object, const std::string &path) {
  std::ofstream out(path);
  if (!out) throw Error("nije moguce otvoriti izlaznu datoteku: " + path);
  out << "SSOBJ 1\n";
  for (const auto &section : object.sections) {
    out << "SECTION " << std::quoted(section.name) << ' ' << section.data.size() << "\n";
    out << "DATA " << hexBytes(section.data) << "\nENDSECTION\n";
  }
  for (const auto &symbol : object.symbols) {
    const char *binding = symbol.binding == Binding::Global ? "GLOBAL" : "LOCAL";
    const char *kind = symbol.kind == SymbolKind::Undefined ? "UND" :
                       symbol.kind == SymbolKind::Absolute ? "ABS" : "SEC";
    out << "SYMBOL " << std::quoted(symbol.name) << ' ' << binding << ' ' << kind << ' '
        << std::quoted(symbol.section) << ' ' << symbol.value << "\n";
  }
  for (const auto &relocation : object.relocations) {
    out << "RELOC " << std::quoted(relocation.section) << ' ' << relocation.offset << " ABS32 "
        << (relocation.targetKind == RelocTarget::Section ? "SECTION " : "SYMBOL ")
        << std::quoted(relocation.target) << ' ' << relocation.addend << "\n";
  }
  out << "END\n";
  if (!out) throw Error("greska pri pisanju izlazne datoteke: " + path);
}

ObjectFile loadObject(const std::string &path) {
  std::ifstream in(path);
  if (!in) throw Error("nije moguce otvoriti predmetni program: " + path);
  std::string magic;
  unsigned version = 0;
  if (!(in >> magic >> version) || magic != "SSOBJ" || version != 1)
    throw Error("nepoznat format predmetnog programa: " + path);

  ObjectFile object;
  std::string keyword;
  while (in >> keyword) {
    if (keyword == "END") break;
    if (keyword == "SECTION") {
      Section section;
      uint64_t expectedSize = 0;
      if (!(in >> std::quoted(section.name) >> expectedSize) || expectedSize > std::numeric_limits<uint32_t>::max())
        throw Error("neispravno zaglavlje sekcije u: " + path);
      std::string dataKeyword, bytes, endKeyword;
      if (!(in >> dataKeyword) || dataKeyword != "DATA") throw Error("nedostaje DATA zapis u: " + path);
      std::getline(in, bytes);
      section.data = parseHexBytes(bytes);
      if (section.data.size() != expectedSize) throw Error("neispravna velicina sekcije " + section.name);
      if (!(in >> endKeyword) || endKeyword != "ENDSECTION") throw Error("nedostaje ENDSECTION u: " + path);
      object.sections.push_back(std::move(section));
    } else if (keyword == "SYMBOL") {
      Symbol symbol;
      std::string binding, kind;
      uint64_t value = 0;
      if (!(in >> std::quoted(symbol.name) >> binding >> kind >> std::quoted(symbol.section) >> value) ||
          value > std::numeric_limits<uint32_t>::max())
        throw Error("neispravan SYMBOL zapis u: " + path);
      if (binding == "GLOBAL") symbol.binding = Binding::Global;
      else if (binding == "LOCAL") symbol.binding = Binding::Local;
      else throw Error("nepoznato vezivanje simbola " + symbol.name);
      if (kind == "UND") symbol.kind = SymbolKind::Undefined;
      else if (kind == "ABS") symbol.kind = SymbolKind::Absolute;
      else if (kind == "SEC") symbol.kind = SymbolKind::Section;
      else throw Error("nepoznat tip simbola " + symbol.name);
      symbol.value = static_cast<uint32_t>(value);
      object.symbols.push_back(std::move(symbol));
    } else if (keyword == "RELOC") {
      Relocation relocation;
      std::string type, targetKind;
      if (!(in >> std::quoted(relocation.section) >> relocation.offset >> type >> targetKind >>
            std::quoted(relocation.target) >> relocation.addend) || type != "ABS32")
        throw Error("neispravan RELOC zapis u: " + path);
      if (targetKind == "SECTION") relocation.targetKind = RelocTarget::Section;
      else if (targetKind == "SYMBOL") relocation.targetKind = RelocTarget::Symbol;
      else throw Error("nepoznata meta relokacije u: " + path);
      object.relocations.push_back(std::move(relocation));
    } else {
      throw Error("nepoznat zapis '" + keyword + "' u: " + path);
    }
  }
  if (!in.eof() && in.fail()) throw Error("neispravan predmetni program: " + path);
  return object;
}

} // namespace ss
