CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinc -Ibuild
FLEX ?= flex
BISON ?= bison
COMMON := src/common.cpp src/object.cpp
PARSER := build/parser.cpp
LEXER := build/lexer.cpp

.PHONY: all clean test
.DELETE_ON_ERROR:

all: asembler linker emulator

build:
	mkdir -p build

$(PARSER): misc/parser.y | build
	$(BISON) -Wall -d --defines=build/parser.hpp -o $@ $<

$(LEXER): misc/lexer.l $(PARSER) | build
	$(FLEX) -o $@ $<

asembler: $(COMMON) $(PARSER) $(LEXER) src/assembler.cpp src/assembler_main.cpp \
		inc/common.hpp inc/object.hpp inc/assembler.hpp inc/assembly_syntax.hpp
	$(CXX) $(CXXFLAGS) $(COMMON) $(PARSER) $(LEXER) src/assembler.cpp src/assembler_main.cpp -o $@

linker: $(COMMON) src/linker.cpp src/linker_main.cpp inc/common.hpp inc/object.hpp inc/linker.hpp
	$(CXX) $(CXXFLAGS) $(COMMON) src/linker.cpp src/linker_main.cpp -o $@

emulator: src/common.cpp src/emulator.cpp src/emulator_main.cpp inc/common.hpp inc/emulator.hpp
	$(CXX) $(CXXFLAGS) src/common.cpp src/emulator.cpp src/emulator_main.cpp -o $@

test: all
	./asembler -o tests/basic.o tests/basic.s
	./linker -hex -place=text@0x40000000 -place=data@0x40001000 -o tests/basic.hex tests/basic.o
	./emulator tests/basic.hex
	./asembler -o tests/instructions.o tests/instructions.s
	./linker -hex -place=text@0x40000000 -place=data@0x50000000 -o tests/instructions.hex tests/instructions.o
	./emulator tests/instructions.hex

clean:
	rm -f asembler linker emulator tests/*.o tests/*.hex
	rm -rf build
