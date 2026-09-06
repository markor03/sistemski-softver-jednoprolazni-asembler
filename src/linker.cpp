#include "linker.hpp"

#include "common.hpp"
#include "object.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace ss {

struct Linker::Impl {
  struct Input {
    std::string path;
    ObjectFile object;
    std::unordered_map<std::string, uint32_t> contribution; // pocetak njegovog dela u spojenoj sekciji
  };
  struct Definition { std::size_t input = 0; Symbol symbol; }; // vrednost simbola zavisi od contribution tog fajla

  std::vector<Input> inputs;
  // objedinjene sekcije svih inputa
  ObjectFile combined; 
  // preslikaa naziv sekcije u njen indeks
  std::unordered_map<std::string, std::size_t> combinedIndex; 
  // mapa globalnih definisanih simbola
  std::map<std::string, Definition> definitions; 
  // globalni korisceni simboli koji nisu definisani u tom fajlu,
  // pa se posle vrsi provera da li ih je neki drugi fajl definisao
  std::set<std::string> undefined; 
  
  Section &combinedSection(const std::string &name) {
    auto found = combinedIndex.find(name);
    if (found == combinedIndex.end()) {
      combinedIndex[name] = combined.sections.size();
      combined.sections.push_back({name, {}});
      return combined.sections.back();
    }
    return combined.sections[found->second];
  }
  // ucitava predmetne programe, proverava imaju li 2 sekcije istog imena
  void load(const std::vector<std::string> &paths) {
    for (const auto &path : paths) {
      Input input{path, loadObject(path), {}};
      std::set<std::string> names;
      for (const auto &section : input.object.sections) {
        if (!names.insert(section.name).second)
          throw Error("predmetni program " + path + " sadrzi duplu sekciju " + section.name);
      }
      inputs.push_back(std::move(input));
    }
  }
  // spaja istoimene sekcije redosledom ulaznih fajlova
  void aggregate() {
    for (std::size_t inputIndex = 0; inputIndex < inputs.size(); ++inputIndex) {
      Input &input = inputs[inputIndex];
      for (const auto &section : input.object.sections) {
        Section &target = combinedSection(section.name);
        if (target.data.size() > std::numeric_limits<uint32_t>::max() - section.data.size())
          throw Error("agregirana sekcija je prevelika: " + section.name);
        input.contribution[section.name] = static_cast<uint32_t>(target.data.size());
        target.data.insert(target.data.end(), section.data.begin(), section.data.end());
      }
      for (const auto &symbol : input.object.symbols) {
        if (symbol.kind == SymbolKind::Section && !input.contribution.count(symbol.section))
          throw Error("simbol " + symbol.name + " upucuje na nepostojecu sekciju " + symbol.section);
        if (symbol.binding != Binding::Global) continue;
        if (symbol.kind == SymbolKind::Undefined) {
          undefined.insert(symbol.name);
        } else {
          auto [it, inserted] = definitions.emplace(symbol.name, Definition{inputIndex, symbol});
          if (!inserted) throw Error("visestruka definicija simbola " + symbol.name);
        }
      }
      for (const auto &relocation : input.object.relocations) {
        if (!input.contribution.count(relocation.section))
          throw Error("relokacija upucuje na nepostojecu izvornu sekciju " + relocation.section);
        if (relocation.targetKind == RelocTarget::Section && !input.contribution.count(relocation.target))
          throw Error("relokacija upucuje na nepostojecu ciljnu sekciju " + relocation.target);
      }
    }
  }
  // pravi se novi predmetni program
  ObjectFile relocatableObject() {
    ObjectFile result;
    result.sections = combined.sections;
    std::set<std::string> emitted;
    for (const auto &[name, definition] : definitions) {
      Symbol symbol = definition.symbol;
      if (symbol.kind == SymbolKind::Section)
        symbol.value += inputs[definition.input].contribution.at(symbol.section);
      result.symbols.push_back(std::move(symbol));
      emitted.insert(name);
    }
    for (const auto &name : undefined) if (!emitted.count(name))
      result.symbols.push_back({name, Binding::Global, SymbolKind::Undefined, "", 0});

    for (std::size_t inputIndex = 0; inputIndex < inputs.size(); ++inputIndex) {
      const Input &input = inputs[inputIndex];
      for (auto relocation : input.object.relocations) {
        uint64_t source = static_cast<uint64_t>(input.contribution.at(relocation.section)) + relocation.offset;
        if (source > std::numeric_limits<uint32_t>::max()) throw Error("preveliki ofset relokacije");
        relocation.offset = static_cast<uint32_t>(source);
        if (relocation.targetKind == RelocTarget::Section)
          relocation.addend += input.contribution.at(relocation.target);
        result.relocations.push_back(std::move(relocation));
      }
    }
    return result;
  }
  // odredjuje konacne adrese
  std::map<std::string, uint32_t> placeSections(const std::map<std::string, uint32_t> &requested) {
    for (const auto &[name, address] : requested) {
      (void)address;
      if (!combinedIndex.count(name)) throw Error("-place navodi nepostojecu sekciju " + name);
    }
    struct Range { uint64_t begin, end; std::string name; };
    std::vector<Range> ranges;
    std::map<std::string, uint32_t> places = requested;
    uint64_t high = 0;
    for (const auto &[name, address] : requested) {
      uint64_t size = combined.sections[combinedIndex.at(name)].data.size();
      uint64_t end = static_cast<uint64_t>(address) + size;
      if (end > (1ULL << 32)) throw Error("sekcija " + name + " izlazi iz adresnog prostora");
      ranges.push_back({address, end, name});
      high = std::max(high, end);
    }
    std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.begin < b.begin; });
    for (std::size_t i = 1; i < ranges.size(); ++i) {
      if (ranges[i].begin < ranges[i - 1].end && ranges[i].begin != ranges[i].end &&
          ranges[i - 1].begin != ranges[i - 1].end)
        throw Error("preklapanje sekcija " + ranges[i - 1].name + " i " + ranges[i].name);
    }
    for (const auto &section : combined.sections) {
      if (places.count(section.name)) continue;
      if (high + section.data.size() > (1ULL << 32))
        throw Error("sekcija " + section.name + " izlazi iz adresnog prostora");
      places[section.name] = static_cast<uint32_t>(high);
      high += section.data.size();
    }
    return places;
  }
  // mapa globalnih i skup koriscenih nedefinisanih simbola
  // prijavljuje visestruku definiciju, nerazresen simbol i
  // simbol koji pokazuje na nepostojecu sekciju
  std::map<std::string, uint32_t> globalValues(const std::map<std::string, uint32_t> &places) {
    std::map<std::string, uint32_t> values;
    for (const auto &[name, definition] : definitions) {
      const Symbol &symbol = definition.symbol;
      if (symbol.kind == SymbolKind::Absolute) values[name] = symbol.value;
      else {
        uint64_t value = static_cast<uint64_t>(places.at(symbol.section)) +
                         inputs[definition.input].contribution.at(symbol.section) + symbol.value;
        if (value > std::numeric_limits<uint32_t>::max())
          throw Error("vrednost simbola je van adresnog prostora: " + name);
        values[name] = static_cast<uint32_t>(value);
      }
    }
    for (const auto &name : undefined)
      if (!values.count(name)) throw Error("nerazresen simbol " + name);
    return values;
  }
  // izracunava konacnu vrednost baza cilja + addend
  void applyRelocations(const std::map<std::string, uint32_t> &places,
                        const std::map<std::string, uint32_t> &globals) {
    for (std::size_t inputIndex = 0; inputIndex < inputs.size(); ++inputIndex) {
      const Input &input = inputs[inputIndex];
      for (const auto &relocation : input.object.relocations) {
        uint64_t offset64 = static_cast<uint64_t>(input.contribution.at(relocation.section)) + relocation.offset;
        Section &source = combined.sections[combinedIndex.at(relocation.section)];
        if (offset64 + 4 > source.data.size())
          throw Error("relokacija je van sekcije " + relocation.section);
        uint32_t base = 0;
        if (relocation.targetKind == RelocTarget::Section) {
          uint64_t value = static_cast<uint64_t>(places.at(relocation.target)) +
                           input.contribution.at(relocation.target);
          base = static_cast<uint32_t>(value);
        } else {
          auto found = globals.find(relocation.target);
          if (found == globals.end()) throw Error("nerazresen simbol " + relocation.target);
          base = found->second;
        }
        uint32_t value = static_cast<uint32_t>(static_cast<uint64_t>(base) + relocation.addend);
        write32(source.data, static_cast<uint32_t>(offset64), value);
      }
    }
  }
  // pravi tekstualni memorijski zapis
  void writeHex(const std::string &path, const std::map<std::string, uint32_t> &places) {
    std::map<uint32_t, uint8_t> memory;
    for (const auto &section : combined.sections) {
      uint32_t address = places.at(section.name);
      for (std::size_t i = 0; i < section.data.size(); ++i) {
        uint32_t location = address + static_cast<uint32_t>(i);
        if (!memory.emplace(location, section.data[i]).second)
          throw Error("preklapanje sekcija na adresi " + std::to_string(location));
      }
    }
    std::ofstream out(path);
    if (!out) throw Error("nije moguce otvoriti izlaznu datoteku: " + path);
    auto it = memory.begin();
    while (it != memory.end()) {
      uint32_t start = it->first, expected = start;
      out << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << start << ':';
      unsigned count = 0;
      while (it != memory.end() && it->first == expected && count < 8) {
        out << ' ' << std::setw(2) << static_cast<unsigned>(it->second);
        ++it; ++expected; ++count;
      }
      out << '\n';
    }
    if (!out) throw Error("greska pri pisanju izlazne datoteke: " + path);
  }
};

Linker::Linker() : impl_(std::make_unique<Impl>()) {}
Linker::~Linker() = default;
Linker::Linker(Linker &&) noexcept = default;
Linker &Linker::operator=(Linker &&) noexcept = default;

void Linker::link(const LinkerOptions &options) {
  impl_ = std::make_unique<Impl>();
  impl_->load(options.inputs);
  impl_->aggregate();
  if (options.relocatable) {
    saveObject(impl_->relocatableObject(), options.output);
    return;
  }
  auto places = impl_->placeSections(options.places);
  auto globals = impl_->globalValues(places);
  impl_->applyRelocations(places, globals);
  impl_->writeHex(options.output, places);
}

} // namespace ss
