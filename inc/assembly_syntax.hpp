#ifndef SS_ASSEMBLY_SYNTAX_HPP
#define SS_ASSEMBLY_SYNTAX_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/* unutrasnji oblik instrukcija, direktiva, operanada i izraza */
namespace ss {

enum class ExprOp {
  Number, Symbol, Add, Sub, Mul, Div, Mod, Shl, Shr, And, Xor, Or, Plus, Minus, Not
};
  // stablo
struct Expr {
  ExprOp op = ExprOp::Number;
  int64_t number = 0;
  std::string symbol;
  std::shared_ptr<Expr> left;
  std::shared_ptr<Expr> right;
};
using ExprPtr = std::shared_ptr<Expr>;

enum class OperandKind { Expression, Immediate, Register, Csr, MemoryRegister };

struct Operand {
  OperandKind kind = OperandKind::Expression;
  ExprPtr expr; // literal, simbol, izraz
  int number = 0; // broj registra
};

enum class StatementKind {
  Instruction, Global, Extern, Section, Word, Skip, Ascii, Equ, End
};

struct Statement {
  StatementKind kind = StatementKind::Instruction;
  std::string name;
  std::vector<Operand> operands;
  std::vector<std::string> names;
  std::vector<ExprPtr> expressions;
  std::vector<uint8_t> bytes;
};

struct ParsedLine {
  unsigned line = 0;
  std::string label;
  bool hasStatement = false;
  Statement statement;
};

using Program = std::vector<ParsedLine>;

inline ExprPtr numberExpr(int64_t value) {
  auto result = std::make_shared<Expr>();
  result->op = ExprOp::Number;
  result->number = value;
  return result;
}

inline ExprPtr symbolExpr(std::string name) {
  auto result = std::make_shared<Expr>();
  result->op = ExprOp::Symbol;
  result->symbol = std::move(name);
  return result;
}

inline ExprPtr unaryExpr(ExprOp op, ExprPtr value) {
  auto result = std::make_shared<Expr>();
  result->op = op;
  result->left = std::move(value);
  return result;
}

inline ExprPtr binaryExpr(ExprOp op, ExprPtr left, ExprPtr right) {
  auto result = std::make_shared<Expr>();
  result->op = op;
  result->left = std::move(left);
  result->right = std::move(right);
  return result;
}

Program parseAssemblyFile(const std::string &path);

} // namespace ss

#endif
