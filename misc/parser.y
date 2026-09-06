%code requires {
#include "assembly_syntax.hpp"

#include <cstdint>
#include <string>
#include <vector>
} /* ovo sve ide u parser.hpp */

%{
#include "assembly_syntax.hpp"
#include "common.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern int yylex(void); /* potreban sledeci token */
extern int yylineno;
void yyerror(const char *message);

struct yy_buffer_state;
using YY_BUFFER_STATE = yy_buffer_state *;
extern YY_BUFFER_STATE yy_scan_bytes(const char *bytes, int length);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);
extern int yylex_destroy(void);

namespace {

ss::Program *activeProgram = nullptr; // pravi lokalni program
std::string parserError;

ss::Statement *instruction(const char *name,
                           std::initializer_list<ss::Operand> operands = {}) {
  auto *result = new ss::Statement();
  result->kind = ss::StatementKind::Instruction;
  result->name = name;
  result->operands.assign(operands.begin(), operands.end());
  return result;
}

ss::Statement *directive(ss::StatementKind kind) {
  auto *result = new ss::Statement();
  result->kind = kind;
  return result;
}

std::vector<uint8_t> decodeString(const char *input, unsigned line) {
  std::string text(input);
  std::vector<uint8_t> result;
  for (std::size_t i = 1; i + 1 < text.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (c != '\\') {
      result.push_back(c);
      continue;
    }
    ++i;
    if (i >= text.size() - 1)
      throw ss::Error("linija " + std::to_string(line) + ": neispravan escape u stringu");
    char escaped = text[i];
    if (escaped == 'n') result.push_back('\n');
    else if (escaped == 'r') result.push_back('\r');
    else if (escaped == 't') result.push_back('\t');
    else if (escaped == '0') result.push_back(0);
    else if (escaped == '\\' || escaped == '"') result.push_back(static_cast<uint8_t>(escaped));
    else if (escaped == 'x') {
      if (i + 2 >= text.size() - 1)
        throw ss::Error("linija " + std::to_string(line) + ": neispravan \\x escape");
      std::string hex = "0x" + text.substr(i + 1, 2);
      result.push_back(static_cast<uint8_t>(ss::parseNumber(hex)));
      i += 2;
    } else {
      throw ss::Error("linija " + std::to_string(line) + ": nepoznat escape: " + escaped);
    }
  }
  return result;
}

void appendLine(char *label, ss::Statement *statement, unsigned line) {
  ss::ParsedLine parsed;
  parsed.line = line;
  if (label) parsed.label = label;
  if (statement) {
    parsed.hasStatement = true;
    parsed.statement = std::move(*statement);
  }
  activeProgram->push_back(std::move(parsed));
  std::free(label);
  delete statement;
}

ss::Operand takeOperand(ss::Operand *operand) {
  ss::Operand result = std::move(*operand);
  delete operand;
  return result;
}

ss::ExprPtr takeExpression(ss::ExprPtr *expression) {
  ss::ExprPtr result = std::move(*expression);
  delete expression;
  return result;
}

} // namespace
%}

%define parse.error verbose
%locations

%union {
  char *text;
  long long number;
  ss::ExprPtr *expression;
  ss::Operand *operand;
  ss::Statement *statement;
  std::vector<std::string> *names;
  std::vector<ss::ExprPtr> *expressions;
}

%token <text> IDENTIFIER STRING
%token <number> NUMBER REGISTER CSR
%token HALT INT IRET CALL RET JMP BEQ BNE BGT PUSH POP XCHG
%token ADD SUB MUL DIV NOT AND OR XOR SHL SHR LD ST CSRRD CSRWR
%token DOT_GLOBAL DOT_SECTION DOT_END DOT_EXTERN DOT_WORD DOT_SKIP DOT_ASCII DOT_EQU
%token COLON COMMA DOLLAR LPAREN RPAREN LBRACKET RBRACKET EOL
%token PLUS MINUS STAR SLASH PERCENT SHIFT_LEFT SHIFT_RIGHT AMPERSAND CARET PIPE TILDE

%type <statement> statement instruction directive
%type <operand> load_operand store_operand memory_register
%type <names> identifier_list
%type <expressions> expression_list
%type <expression> expression

%destructor { std::free($$); } <text>
%destructor { delete $$; } <expression> <operand> <statement> <names> <expressions>

%left PIPE
%left CARET
%left AMPERSAND
%left SHIFT_LEFT SHIFT_RIGHT
%left PLUS MINUS
%left STAR SLASH PERCENT
%precedence UPLUS UMINUS TILDE

%%

program:
    lines
;

lines:
    %empty
  | lines line
;

line:
    EOL
  | statement EOL {
      bool ended = $1->kind == ss::StatementKind::End;
      appendLine(nullptr, $1, @1.first_line);
      if (ended) YYACCEPT;
    }
  | IDENTIFIER COLON EOL {
      appendLine($1, nullptr, @1.first_line);
    }
  | IDENTIFIER COLON statement EOL {
      bool ended = $3->kind == ss::StatementKind::End;
      appendLine($1, $3, @1.first_line);
      if (ended) YYACCEPT;
    }
;

statement:
    instruction { $$ = $1; }
  | directive { $$ = $1; }
;

instruction:
    HALT { $$ = instruction("halt"); }
  | INT { $$ = instruction("int"); }
  | IRET { $$ = instruction("iret"); }
  | RET { $$ = instruction("ret"); }
  | CALL expression {
      $$ = instruction("call", {{ss::OperandKind::Expression, takeExpression($2), 0}});
    }
  | JMP expression {
      $$ = instruction("jmp", {{ss::OperandKind::Expression, takeExpression($2), 0}});
    }
  | BEQ REGISTER COMMA REGISTER COMMA expression {
      $$ = instruction("beq", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)},
                                {ss::OperandKind::Expression, takeExpression($6), 0}});
    }
  | BNE REGISTER COMMA REGISTER COMMA expression {
      $$ = instruction("bne", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)},
                                {ss::OperandKind::Expression, takeExpression($6), 0}});
    }
  | BGT REGISTER COMMA REGISTER COMMA expression {
      $$ = instruction("bgt", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)},
                                {ss::OperandKind::Expression, takeExpression($6), 0}});
    }
  | PUSH REGISTER {
      $$ = instruction("push", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)}});
    }
  | POP REGISTER {
      $$ = instruction("pop", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)}});
    }
  | XCHG REGISTER COMMA REGISTER {
      $$ = instruction("xchg", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                 {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | ADD REGISTER COMMA REGISTER {
      $$ = instruction("add", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | SUB REGISTER COMMA REGISTER {
      $$ = instruction("sub", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | MUL REGISTER COMMA REGISTER {
      $$ = instruction("mul", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | DIV REGISTER COMMA REGISTER {
      $$ = instruction("div", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | NOT REGISTER {
      $$ = instruction("not", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)}});
    }
  | AND REGISTER COMMA REGISTER {
      $$ = instruction("and", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | OR REGISTER COMMA REGISTER {
      $$ = instruction("or", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                               {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | XOR REGISTER COMMA REGISTER {
      $$ = instruction("xor", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | SHL REGISTER COMMA REGISTER {
      $$ = instruction("shl", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | SHR REGISTER COMMA REGISTER {
      $$ = instruction("shr", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | LD load_operand COMMA REGISTER {
      $$ = instruction("ld", {takeOperand($2),
                               {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | ST REGISTER COMMA store_operand {
      $$ = instruction("st", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                               takeOperand($4)});
    }
  | CSRRD CSR COMMA REGISTER {
      $$ = instruction("csrrd", {{ss::OperandKind::Csr, nullptr, static_cast<int>($2)},
                                  {ss::OperandKind::Register, nullptr, static_cast<int>($4)}});
    }
  | CSRWR REGISTER COMMA CSR {
      $$ = instruction("csrwr", {{ss::OperandKind::Register, nullptr, static_cast<int>($2)},
                                  {ss::OperandKind::Csr, nullptr, static_cast<int>($4)}});
    }
;

load_operand:
    DOLLAR expression {
      $$ = new ss::Operand{ss::OperandKind::Immediate, takeExpression($2), 0};
    }
  | REGISTER {
      $$ = new ss::Operand{ss::OperandKind::Register, nullptr, static_cast<int>($1)};
    }
  | expression {
      $$ = new ss::Operand{ss::OperandKind::Expression, takeExpression($1), 0};
    }
  | memory_register { $$ = $1; }
;

store_operand:
    expression {
      $$ = new ss::Operand{ss::OperandKind::Expression, takeExpression($1), 0};
    }
  | memory_register { $$ = $1; }
;

memory_register:
    LBRACKET REGISTER RBRACKET {
      $$ = new ss::Operand{ss::OperandKind::MemoryRegister, ss::numberExpr(0),
                           static_cast<int>($2)};
    }
  | LBRACKET REGISTER PLUS expression RBRACKET {
      $$ = new ss::Operand{ss::OperandKind::MemoryRegister, takeExpression($4),
                           static_cast<int>($2)};
    }
;

directive:
    DOT_GLOBAL identifier_list {
      $$ = directive(ss::StatementKind::Global);
      $$->names = std::move(*$2);
      delete $2;
    }
  | DOT_EXTERN identifier_list {
      $$ = directive(ss::StatementKind::Extern);
      $$->names = std::move(*$2);
      delete $2;
    }
  | DOT_SECTION IDENTIFIER {
      $$ = directive(ss::StatementKind::Section);
      $$->name = $2;
      std::free($2);
    }
  | DOT_WORD expression_list {
      $$ = directive(ss::StatementKind::Word);
      $$->expressions = std::move(*$2);
      delete $2;
    }
  | DOT_SKIP expression {
      $$ = directive(ss::StatementKind::Skip);
      $$->expressions.push_back(takeExpression($2));
    }
  | DOT_ASCII STRING {
      $$ = directive(ss::StatementKind::Ascii);
      $$->bytes = decodeString($2, @2.first_line);
      std::free($2);
    }
  | DOT_EQU IDENTIFIER COMMA expression {
      $$ = directive(ss::StatementKind::Equ);
      $$->name = $2;
      std::free($2);
      $$->expressions.push_back(takeExpression($4));
    }
  | DOT_END { $$ = directive(ss::StatementKind::End); }
;

identifier_list:
    IDENTIFIER {
      $$ = new std::vector<std::string>();
      $$->emplace_back($1);
      std::free($1);
    }
  | identifier_list COMMA IDENTIFIER {
      $1->emplace_back($3);
      std::free($3);
      $$ = $1;
    }
;

expression_list:
    expression {
      $$ = new std::vector<ss::ExprPtr>();
      $$->push_back(takeExpression($1));
    }
  | expression_list COMMA expression {
      $1->push_back(takeExpression($3));
      $$ = $1;
    }
;

expression:
    NUMBER { $$ = new ss::ExprPtr(ss::numberExpr($1)); }
  | IDENTIFIER {
      $$ = new ss::ExprPtr(ss::symbolExpr($1));
      std::free($1);
    }
  | LPAREN expression RPAREN { $$ = $2; }
  | PLUS expression %prec UPLUS {
      $$ = new ss::ExprPtr(ss::unaryExpr(ss::ExprOp::Plus, takeExpression($2)));
    }
  | MINUS expression %prec UMINUS {
      $$ = new ss::ExprPtr(ss::unaryExpr(ss::ExprOp::Minus, takeExpression($2)));
    }
  | TILDE expression {
      $$ = new ss::ExprPtr(ss::unaryExpr(ss::ExprOp::Not, takeExpression($2)));
    }
  | expression STAR expression {
      $$ = new ss::ExprPtr(ss::binaryExpr(ss::ExprOp::Mul, takeExpression($1), takeExpression($3)));
    }
  | expression SLASH expression {
      $$ = new ss::ExprPtr(ss::binaryExpr(ss::ExprOp::Div, takeExpression($1), takeExpression($3)));
    }
  | expression PERCENT expression {
      $$ = new ss::ExprPtr(ss::binaryExpr(ss::ExprOp::Mod, takeExpression($1), takeExpression($3)));
    }
  | expression PLUS expression {
      $$ = new ss::ExprPtr(ss::binaryExpr(ss::ExprOp::Add, takeExpression($1), takeExpression($3)));
    }
  | expression MINUS expression {
      $$ = new ss::ExprPtr(ss::binaryExpr(ss::ExprOp::Sub, takeExpression($1), takeExpression($3)));
    }
  | expression SHIFT_LEFT expression {
      $$ = new ss::ExprPtr(ss::binaryExpr(ss::ExprOp::Shl, takeExpression($1), takeExpression($3)));
    }
  | expression SHIFT_RIGHT expression {
      $$ = new ss::ExprPtr(ss::binaryExpr(ss::ExprOp::Shr, takeExpression($1), takeExpression($3)));
    }
  | expression AMPERSAND expression {
      $$ = new ss::ExprPtr(ss::binaryExpr(ss::ExprOp::And, takeExpression($1), takeExpression($3)));
    }
  | expression CARET expression {
      $$ = new ss::ExprPtr(ss::binaryExpr(ss::ExprOp::Xor, takeExpression($1), takeExpression($3)));
    }
  | expression PIPE expression {
      $$ = new ss::ExprPtr(ss::binaryExpr(ss::ExprOp::Or, takeExpression($1), takeExpression($3)));
    }
;

%%

void yyerror(const char *message) {
  if (parserError.empty())
    parserError = "linija " + std::to_string(yylloc.first_line) + ": sintaksna greska: " + message;
}

namespace ss {

Program parseAssemblyFile(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw Error("nije moguce otvoriti ulaznu datoteku: " + path);

  std::ostringstream contents;
  contents << input.rdbuf();
  std::string source = contents.str();
  if (!source.empty() && source.back() != '\n') source.push_back('\n');

  Program program;
  activeProgram = &program;
  parserError.clear();
  yylineno = 1;
  YY_BUFFER_STATE buffer = yy_scan_bytes(source.data(), static_cast<int>(source.size()));
  int status = 0;
  try {
    status = yyparse();
  } catch (...) {
    yy_delete_buffer(buffer);
    yylex_destroy();
    activeProgram = nullptr;
    throw;
  }
  yy_delete_buffer(buffer);
  yylex_destroy();
  activeProgram = nullptr;
  if (status != 0)
    throw Error(parserError.empty() ? "sintaksna greska" : parserError);
  return program;
}

} // namespace ss
