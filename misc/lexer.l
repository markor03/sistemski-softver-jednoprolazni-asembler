/* koji su sledeci elementi u tekstu */
%{
#include "parser.hpp"

#include "common.hpp"
#include <cstdlib>
#include <cstring>

#define YY_USER_ACTION \
  yylloc.first_line = yylloc.last_line = yylineno;
%}
/* makro pre svakog tokena, njegovoj lokaciji se 
dodaje trenutni broj reda*/

/* gasenje podrazumevanog yywrap da lexer ne trazi 
jos fajlova posle EOF; brojac redova; velika i mala
slova su jednaka; nema pomocnih fja input i unput */

%option noyywrap yylineno caseless nounput noinput

/* svaka mnemonicka rec se mapira u bison token, 
redosled je bitan da bi se uhvatile kljucne reci pre SYMBOL pravila */

%%
"halt"                       { return HALT; }
"int"                        { return INT; }
"iret"                       { return IRET; }
"call"                       { return CALL; }
"ret"                        { return RET; }
"jmp"                        { return JMP; }
"beq"                        { return BEQ; }
"bne"                        { return BNE; }
"bgt"                        { return BGT; }
"push"                       { return PUSH; }
"pop"                        { return POP; }
"xchg"                       { return XCHG; }
"add"                        { return ADD; }
"sub"                        { return SUB; }
"mul"                        { return MUL; }
"div"                        { return DIV; }
"not"                        { return NOT; }
"and"                        { return AND; }
"or"                         { return OR; }
"xor"                        { return XOR; }
"shl"                        { return SHL; }
"shr"                        { return SHR; }
"ld"                         { return LD; }
"st"                         { return ST; }
"csrrd"                      { return CSRRD; }
"csrwr"                      { return CSRWR; }

".global"                    { return DOT_GLOBAL; }
".section"                   { return DOT_SECTION; }
".end"                       { return DOT_END; }
".extern"                    { return DOT_EXTERN; }
".word"                      { return DOT_WORD; }
".skip"                      { return DOT_SKIP; }
".ascii"                     { return DOT_ASCII; }
".equ"                       { return DOT_EQU; }

"%status"                    { yylval.number = 0; return CSR; }
"%handler"                   { yylval.number = 1; return CSR; }
"%cause"                     { yylval.number = 2; return CSR; }
"%sp"                        { yylval.number = 14; return REGISTER; }
"%pc"                        { yylval.number = 15; return REGISTER; }
"%r"(1[0-5]|[0-9])          { yylval.number = std::strtol(yytext + 2, nullptr, 10); return REGISTER; }

0[xX][0-9a-fA-F]+            { yylval.number = ss::parseNumber(yytext); return NUMBER; }
[0-9]+                       { yylval.number = ss::parseNumber(yytext); return NUMBER; }
\"([^\\\"\n]|\\.)*\"          { yylval.text = ::strdup(yytext); return STRING; } /* ne sme backslash i navodnici, sme backslash pre nekog drugog karaktera; kopira se literal, oslobadjanje strdup je sa free, a ne sa delete */
[A-Za-z_.][A-Za-z0-9_.$]*    { yylval.text = ::strdup(yytext); return IDENTIFIER; } 

":"                          { return COLON; }
","                          { return COMMA; }
"$"                          { return DOLLAR; }
"("                          { return LPAREN; }
")"                          { return RPAREN; }
"["                          { return LBRACKET; }
"]"                          { return RBRACKET; }
"<<"                         { return SHIFT_LEFT; }
">>"                         { return SHIFT_RIGHT; }
"+"                          { return PLUS; }
"-"                          { return MINUS; }
"*"                          { return STAR; }
"/"                          { return SLASH; }
"%"                          { return PERCENT; }
"&"                          { return AMPERSAND; }
"^"                          { return CARET; }
"|"                          { return PIPE; }
"~"                          { return TILDE; }

#[^\n]*                      { /* komentar */ }
[ \t\r]+                    { /* razmaci */ }
\n                           {
                                yylloc.first_line = yylloc.last_line = yylineno - 1;
                                return EOL;
                              }
.                            { return static_cast<unsigned char>(yytext[0]); } /* karakter koji nije prepoznat */
%%
