#include "assembler.hpp"

#include "assembly_syntax.hpp"
#include "common.hpp"
#include "object.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>

namespace ss {
namespace {
// poznata; relativna; mora da se razresi
enum class ValueKind { Absolute, Section, External };
struct Value {
  ValueKind kind = ValueKind::Absolute;
  std::string base;
  int64_t addend = 0;
};

uint32_t value32(int64_t value) { return static_cast<uint32_t>(value); }
int64_t signed32(int64_t value) { return static_cast<int32_t>(static_cast<uint32_t>(value)); }

}

struct Assembler::Impl {
  struct SymbolState {
    Binding binding = Binding::Local;
    SymbolKind kind = SymbolKind::Undefined;
    std::string section;
    uint32_t value = 0;
    ExprPtr equ;
    bool resolving = false;
    bool resolvedEqu = false;
  };
  struct Fix { std::string section; uint32_t offset; ExprPtr expr; }; // .word i 4B unos literal poola
  struct PoolUse { std::string section; uint32_t instruction; ExprPtr expr; }; // instrukcije kojima treba napraviti literal pool
  struct DispFix { std::string section; uint32_t instruction; ExprPtr expr; }; // pamti 12b pomeraje, ne moze relokacija

  // predmetni program, sadrzi sekcije, simbole i relokacije
  ObjectFile object;
  // mapiranje za pronalazak sekcija 
  std::unordered_map<std::string, std::size_t> sectionIndex;
  // tabela simbola
  std::map<std::string, SymbolState> symbols;
  // mesta za popunjavanje
  std::vector<Fix> fixes;
  // bazen literala
  std::vector<PoolUse> pools;
  std::vector<DispFix> displacements;
  std::string currentSection;
  // current
  unsigned lineNumber = 0;
  // pronalazi sekciju ako ona postoji, ako ne pravi novu
  Section &section(const std::string &name) {
    auto found = sectionIndex.find(name);
    if (found == sectionIndex.end()) {
      sectionIndex[name] = object.sections.size();
      object.sections.push_back({name, {}});
      return object.sections.back();
    }
    return object.sections[found->second];
  }
  // vraca trenutno aktivnu sekciju
  Section &current() {
    if (currentSection.empty()) fail("sadrzaj mora biti unutar sekcije");
    return section(currentSection);
  }
  [[noreturn]] void fail(const std::string &message) const {
    throw Error("greska na liniji " + std::to_string(lineNumber) + ": " + message);
  }
  // generisanje instrukcije
  uint32_t emit(uint8_t opmod, int a = 0, int b = 0, int c = 0, int64_t displacement = 0) {
    if (!fitsSigned12(displacement)) fail("pomeraj instrukcije nije 12-bitna oznacena vrednost");
    auto &data = current().data;
    uint32_t offset = static_cast<uint32_t>(data.size());
    uint16_t d = static_cast<uint16_t>(displacement) & 0x0FFF;
    data.push_back(opmod);
    data.push_back(static_cast<uint8_t>((a << 4) | b));
    data.push_back(static_cast<uint8_t>((c << 4) | (d >> 8)));
    data.push_back(static_cast<uint8_t>(d));
    return offset;
  }
  // 
  void patchDisp(const std::string &sectionName, uint32_t instruction, int64_t displacement) {
    if (!fitsSigned12(displacement))
      throw Error("literal pool je van 12-bitnog dosega u sekciji " + sectionName);
    auto &data = section(sectionName).data;
    if (instruction + 4 > data.size()) throw Error("interna greska pri razresavanju pomeraja");
    uint16_t d = static_cast<uint16_t>(displacement) & 0x0FFF;
    data[instruction + 2] = static_cast<uint8_t>((data[instruction + 2] & 0xF0) | (d >> 8));
    data[instruction + 3] = static_cast<uint8_t>(d);
  }

  Value symbolValue(const std::string &name) {
    auto found = symbols.find(name);
    if (found == symbols.end()) return {ValueKind::External, name, 0};
    SymbolState &symbol = found->second;
    if (symbol.equ && !symbol.resolvedEqu) {
      if (symbol.resolving) fail("ciklicna .equ definicija simbola " + name);
      symbol.resolving = true;
      Value value;
      try {
        value = evaluate(symbol.equ);
      } catch (...) {
        symbol.resolving = false;
        throw;
      }
      symbol.resolving = false;
      if (value.kind == ValueKind::External)
        fail(".equ simbol " + name + " zavisi od nedefinisanog simbola " + value.base);
      symbol.kind = value.kind == ValueKind::Absolute ? SymbolKind::Absolute : SymbolKind::Section;
      symbol.section = value.kind == ValueKind::Section ? value.base : "";
      symbol.value = value32(value.addend);
      symbol.resolvedEqu = true;
    }
    if (symbol.kind == SymbolKind::Absolute) return {ValueKind::Absolute, "", symbol.value};
    if (symbol.kind == SymbolKind::Section) return {ValueKind::Section, symbol.section, symbol.value};
    return {ValueKind::External, name, 0};
  }

  static Value absoluteBinary(ExprOp op, int64_t l, int64_t r) {
    uint32_t left = value32(l), right = value32(r);
    switch (op) {
    case ExprOp::Mul: return {ValueKind::Absolute, "", value32(static_cast<uint64_t>(left) * right)};
    case ExprOp::Div:
      if (right == 0) throw Error("deljenje nulom u izrazu");
      return {ValueKind::Absolute, "", left / right};
    case ExprOp::Mod:
      if (right == 0) throw Error("deljenje nulom u izrazu");
      return {ValueKind::Absolute, "", left % right};
    case ExprOp::Shl: return {ValueKind::Absolute, "", value32(left << (right & 31))};
    case ExprOp::Shr: return {ValueKind::Absolute, "", left >> (right & 31)};
    case ExprOp::And: return {ValueKind::Absolute, "", left & right};
    case ExprOp::Xor: return {ValueKind::Absolute, "", left ^ right};
    case ExprOp::Or: return {ValueKind::Absolute, "", left | right};
    default: throw Error("interna greska operatora izraza");
    }
  }
  // izracuna izraz i odredi da li je apsolutan
  Value evaluate(const ExprPtr &expr) {
    if (!expr) throw Error("prazan izraz");
    if (expr->op == ExprOp::Number) return {ValueKind::Absolute, "", expr->number};
    if (expr->op == ExprOp::Symbol) return symbolValue(expr->symbol);
    if (expr->op == ExprOp::Plus || expr->op == ExprOp::Minus || expr->op == ExprOp::Not) {
      Value value = evaluate(expr->left);
      if (expr->op == ExprOp::Plus) return value;
      if (value.kind != ValueKind::Absolute) throw Error("unarni operator zahteva apsolutan izraz");
      if (expr->op == ExprOp::Minus) value.addend = value32(0U - value32(value.addend));
      else value.addend = static_cast<uint32_t>(~value32(value.addend));
      return value;
    }
    Value left = evaluate(expr->left), right = evaluate(expr->right);
    if (expr->op == ExprOp::Add) {
      if (left.kind == ValueKind::Absolute) { right.addend += signed32(left.addend); return right; }
      if (right.kind == ValueKind::Absolute) { left.addend += signed32(right.addend); return left; }
      throw Error("zbir dva relokativna simbola nije dozvoljen");
    }
    if (expr->op == ExprOp::Sub) {
      if (right.kind == ValueKind::Absolute) { left.addend -= signed32(right.addend); return left; }
      if (left.kind == right.kind && left.base == right.base)
        return {ValueKind::Absolute, "", left.addend - right.addend};
      throw Error("razlika nesaglasnih relokativnih simbola nije dozvoljena");
    }
    if (left.kind != ValueKind::Absolute || right.kind != ValueKind::Absolute)
      throw Error("operator zahteva apsolutne operande");
    return absoluteBinary(expr->op, left.addend, right.addend);
  }
  // pravi labelu u sekciji, u njoj to nije adresa nego pomeraj
  void defineLabel(const std::string &name) {
    if (currentSection.empty()) fail("labela " + name + " je definisana izvan sekcije");
    SymbolState &symbol = symbols[name];
    if (symbol.kind != SymbolKind::Undefined || symbol.equ) fail("visestruka definicija simbola " + name);
    symbol.kind = SymbolKind::Section;
    symbol.section = currentSection;
    symbol.value = static_cast<uint32_t>(current().data.size());
  }
  void addPool(uint32_t instruction, ExprPtr expr) { pools.push_back({currentSection, instruction, std::move(expr)}); }

  void emitLoadAddress(const ExprPtr &expr, int destination) {
    uint32_t ins = emit(0x92, destination, 15, 0, 0);
    addPool(ins, expr);
  }
  void emitJump(const Statement &statement) {
    const std::string &mnemonic = statement.name;
    unsigned directMod = 0, indirectMod = 8;
    int b = 0, c = 0;
    ExprPtr expr;
    if (mnemonic == "call") {
      directMod = 0; indirectMod = 1; expr = statement.operands[0].expr;
    } else if (mnemonic == "jmp") {
      expr = statement.operands[0].expr;
    } else {
      b = statement.operands[0].number;
      c = statement.operands[1].number;
      expr = statement.operands[2].expr;
      if (mnemonic == "beq") { directMod = 1; indirectMod = 9; }
      else if (mnemonic == "bne") { directMod = 2; indirectMod = 10; }
      else { directMod = 3; indirectMod = 11; }
    }
    Value constant;
    bool direct = false;
    try {
      constant = evaluate(expr);
      direct = constant.kind == ValueKind::Absolute && fitsSigned12(signed32(constant.addend));
    } catch (const Error &) {
      direct = false;
    }
    uint8_t opcode = mnemonic == "call" ? 0x20 : 0x30;
    if (direct) emit(static_cast<uint8_t>(opcode | directMod), 0, b, c, signed32(constant.addend));
    else {
      uint32_t ins = emit(static_cast<uint8_t>(opcode | indirectMod), 15, b, c, 0);
      addPool(ins, expr);
    }
  }

  void emitLd(const Statement &statement) {
    const Operand &source = statement.operands[0];
    int destination = statement.operands[1].number;
    if (source.kind == OperandKind::Register) {
      emit(0x91, destination, source.number, 0, 0);
    } else if (source.kind == OperandKind::MemoryRegister) {
      uint32_t ins = emit(0x92, destination, source.number, 0, 0);
      displacements.push_back({currentSection, ins, source.expr});
    } else if (source.kind == OperandKind::Immediate) {
      try {
        Value value = evaluate(source.expr);
        if (value.kind == ValueKind::Absolute && fitsSigned12(signed32(value.addend))) {
          emit(0x91, destination, 0, 0, signed32(value.addend));
          return;
        }
      } catch (const Error &) {}
      emitLoadAddress(source.expr, destination);
    } else {
      try {
        Value value = evaluate(source.expr);
        if (value.kind == ValueKind::Absolute && fitsSigned12(signed32(value.addend))) {
          emit(0x92, destination, 0, 0, signed32(value.addend));
          return;
        }
      } catch (const Error &) {}
      emitLoadAddress(source.expr, destination);
      emit(0x92, destination, destination, 0, 0);
    }
  }

  void emitSt(const Statement &statement) {
    int source = statement.operands[0].number;
    const Operand &destination = statement.operands[1];
    if (destination.kind == OperandKind::MemoryRegister) {
      uint32_t ins = emit(0x80, destination.number, 0, source, 0);
      displacements.push_back({currentSection, ins, destination.expr});
      return;
    }
    try {
      Value value = evaluate(destination.expr);
      if (value.kind == ValueKind::Absolute && fitsSigned12(signed32(value.addend))) {
        emit(0x80, 0, 0, source, signed32(value.addend));
        return;
      }
    } catch (const Error &) {}
    uint32_t ins = emit(0x82, 15, 0, source, 0);
    addPool(ins, destination.expr);
  }

  void emitInstruction(const Statement &statement) {
    const std::string &mnemonic = statement.name;
    const auto &operands = statement.operands;
    if (mnemonic == "halt") emit(0x00);
    else if (mnemonic == "int") emit(0x10);
    else if (mnemonic == "iret") {
      emit(0x96, 0, 14, 0, 4); // status <= mem32[sp + 4]
      emit(0x93, 15, 14, 0, 8); // pc <= mem32[sp]; sp += 8
    } else if (mnemonic == "ret") {
      emit(0x93, 15, 14, 0, 4);
    } else if (mnemonic == "call" || mnemonic == "jmp" || mnemonic == "beq" ||
               mnemonic == "bne" || mnemonic == "bgt") emitJump(statement);
    else if (mnemonic == "push") {
      emit(0x81, 14, 0, operands[0].number, -4);
    } else if (mnemonic == "pop") {
      emit(0x93, operands[0].number, 14, 0, 4);
    } else if (mnemonic == "xchg") {
      int source = operands[0].number, destination = operands[1].number;
      emit(0x40, 0, destination, source, 0);
    } else if (mnemonic == "add" || mnemonic == "sub" || mnemonic == "mul" || mnemonic == "div") {
      int source = operands[0].number, destination = operands[1].number;
      int mod = mnemonic == "add" ? 0 : mnemonic == "sub" ? 1 : mnemonic == "mul" ? 2 : 3;
      emit(static_cast<uint8_t>(0x50 | mod), destination, destination, source, 0);
    } else if (mnemonic == "not") {
      int reg = operands[0].number; emit(0x60, reg, reg, 0, 0);
    } else if (mnemonic == "and" || mnemonic == "or" || mnemonic == "xor") {
      int source = operands[0].number, destination = operands[1].number;
      int mod = mnemonic == "and" ? 1 : mnemonic == "or" ? 2 : 3;
      emit(static_cast<uint8_t>(0x60 | mod), destination, destination, source, 0);
    } else if (mnemonic == "shl" || mnemonic == "shr") {
      int source = operands[0].number, destination = operands[1].number;
      emit(static_cast<uint8_t>(mnemonic == "shl" ? 0x70 : 0x71), destination, destination, source, 0);
    } else if (mnemonic == "ld") emitLd(statement);
    else if (mnemonic == "st") emitSt(statement);
    else if (mnemonic == "csrrd") {
      emit(0x90, operands[1].number, operands[0].number, 0, 0);
    } else if (mnemonic == "csrwr") {
      emit(0x94, operands[1].number, operands[0].number, 0, 0);
    } else fail("nepoznata instrukcija " + mnemonic);
  }
  // obrada direktiva .
  void applyDirective(const Statement &statement) {
    if (statement.kind == StatementKind::Global || statement.kind == StatementKind::Extern) {
      for (const auto &symbolName : statement.names) {
        SymbolState &symbol = symbols[symbolName];
        symbol.binding = Binding::Global;
        if (statement.kind == StatementKind::Extern &&
            (symbol.kind != SymbolKind::Undefined || symbol.equ))
          fail("definisan simbol ne moze biti extern: " + symbolName);
      }
    } else if (statement.kind == StatementKind::Section) { // postavlja aktivnu sekciju
      currentSection = statement.name; section(statement.name);
    } else if (statement.kind == StatementKind::Word) { // rezervise 4 nula B, 
      // pamti u fixes pa odlucuje da li je apsolutna adresa ili relokacija
      auto &data = current().data;
      for (const auto &value : statement.expressions) {
        uint32_t offset = static_cast<uint32_t>(data.size());
        data.insert(data.end(), 4, 0); fixes.push_back({currentSection, offset, value});
      }
    } else if (statement.kind == StatementKind::Skip) { // dodaje 32 nula B u trenutnu sekciju
      Value amount = evaluate(statement.expressions[0]);
      if (amount.kind != ValueKind::Absolute) fail(".skip zahteva apsolutan literal");
      uint32_t count = value32(amount.addend);
      if (count > 64U * 1024U * 1024U) fail(".skip je nerazumno veliki");
      current().data.insert(current().data.end(), count, 0);
    } else if (statement.kind == StatementKind::Ascii) { // dodaje bajtove stringa u sekciju, ne dodaje zavrsnu nulu
      auto &data = current().data;
      data.insert(data.end(), statement.bytes.begin(), statement.bytes.end());
    } else if (statement.kind == StatementKind::Equ) { // cuva izraz uz simbol
      SymbolState &symbol = symbols[statement.name];
      if (symbol.kind != SymbolKind::Undefined || symbol.equ)
        fail("visestruka definicija simbola " + statement.name);
      symbol.equ = statement.expressions[0];
    }
  }

  void process(const ParsedLine &line) {
    lineNumber = line.line;
    if (!line.label.empty()) defineLabel(line.label);
    if (!line.hasStatement || line.statement.kind == StatementKind::End) return;
    if (line.statement.kind == StatementKind::Instruction) emitInstruction(line.statement);
    else applyDirective(line.statement);
  }
  // upis apsolutne vrednosti u 4B ili sekcija + addend/spoljasnji simbol + addend
  void makeRelocation(const Fix &fix, const Value &value) {
    auto &data = section(fix.section).data;
    if (value.kind == ValueKind::Absolute) write32(data, fix.offset, value32(value.addend));
    else {
      write32(data, fix.offset, 0);
      object.relocations.push_back({fix.section, fix.offset,
        value.kind == ValueKind::Section ? RelocTarget::Section : RelocTarget::Symbol,
        value.base, value.addend});
      if (value.kind == ValueKind::External) symbols[value.base].binding = Binding::Global;
    }
  }
  // razresava .equ simbole, dodaje literal, poravna i pravi relokaciju ako treba, formira konacnu tabelu simbola
  void finalize() {
    for (auto &[name, symbol] : symbols) if (symbol.equ) (void)symbolValue(name);
    for (const auto &fix : displacements) {
      Value value = evaluate(fix.expr);
      if (value.kind != ValueKind::Absolute || !fitsSigned12(signed32(value.addend)))
        throw Error("pomeraj uz registar mora biti poznata 12-bitna oznacena vrednost");
      patchDisp(fix.section, fix.instruction, signed32(value.addend));
    }
    for (const auto &pool : pools) {
      auto &data = section(pool.section).data;
      uint32_t poolOffset = static_cast<uint32_t>(data.size());
      data.insert(data.end(), 4, 0);
      patchDisp(pool.section, pool.instruction,
                static_cast<int64_t>(poolOffset) - static_cast<int64_t>(pool.instruction + 4));
      fixes.push_back({pool.section, poolOffset, pool.expr});
    }
    for (const auto &fix : fixes) makeRelocation(fix, evaluate(fix.expr));

    for (const auto &[name, state] : symbols) {
      Symbol symbol;
      symbol.name = name; symbol.binding = state.binding; symbol.kind = state.kind;
      symbol.section = state.section; symbol.value = state.value;
      if (symbol.kind == SymbolKind::Undefined) symbol.binding = Binding::Global;
      object.symbols.push_back(std::move(symbol));
    }
  }
};

Assembler::Assembler() : impl_(std::make_unique<Impl>()) {}
Assembler::~Assembler() = default;
Assembler::Assembler(Assembler &&) noexcept = default;
Assembler &Assembler::operator=(Assembler &&) noexcept = default;

void Assembler::assemble(const std::string &inputPath, const std::string &outputPath) {
  impl_ = std::make_unique<Impl>(); // pravi se novo stanje
  Program program = parseAssemblyFile(inputPath); // dobijen program kao niz obradjenih linija
  for (const ParsedLine &line : program) {
    try { impl_->process(line); } // pretvaranje linija u sekcije, simbole i instrukcije
    catch (const Error &error) {
      std::string message = error.what();
      if (message.rfind("linija ", 0) == 0) throw;
      impl_->lineNumber = line.line;
      impl_->fail(message);
    }
  }
  impl_->finalize(); // zavrsavaju se odlozene vrednosti
  saveObject(impl_->object, outputPath); // cuvanje rezultata u .o fajl
}

}
