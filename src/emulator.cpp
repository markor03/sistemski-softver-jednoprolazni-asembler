#include "emulator.hpp"

#include "common.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <poll.h>
#include <regex>
#include <sstream>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <unordered_map>

// za njega ne postoje sekcije, simboli i relokacije, postoje samo adrese i vrednosti
namespace ss {
namespace {

constexpr uint32_t InitialPc = 0x40000000U;
constexpr uint32_t TermOut = 0xFFFFFF00U; // ispis na stdout
constexpr uint32_t TermIn = 0xFFFFFF04U; // cita karakter sa stdin
constexpr uint32_t TimerConfig = 0xFFFFFF10U;
  // posatvlja terminal u rezim bez automatskog prikaza
  // unetih karaktera, destruktor vraca terminal u prethodno stanje
class TerminalMode {
public:
  TerminalMode() {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &old_) != 0) return;
    termios raw = old_;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) active_ = true;
  }
  ~TerminalMode() { if (active_) tcsetattr(STDIN_FILENO, TCSANOW, &old_); }
  TerminalMode(const TerminalMode &) = delete;
  TerminalMode &operator=(const TerminalMode &) = delete;

private:
  termios old_{};
  bool active_ = false;
};
  // retka memorija, cuvaju se samo adrese u kojima ima sadrzaj
  // citanje bajta, little endian 32b i upis 32b
class Memory {
public:
  uint8_t read8(uint32_t address) const { // vraca B na toj adresi ako postoji
    auto found = bytes_.find(address);
    return found == bytes_.end() ? 0 : found->second;
  }
  uint32_t read32(uint32_t address) const { // vraca 4B little endian
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
      value |= static_cast<uint32_t>(read8(address + i)) << (8 * i);
    return value;
  }
  void write8Initial(uint32_t address, uint8_t value) { // samo pri ucitavanju .hex fajla
    if (!bytes_.emplace(address, value).second)
      throw Error("ulazna datoteka vise puta inicijalizuje adresu 0x" + hex(address));
  }
  void write32(uint32_t address, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes_[address + i] = static_cast<uint8_t>(value >> (8 * i));
  }

private:
  std::unordered_map<uint32_t, uint8_t> bytes_;
  static std::string hex(uint32_t value) {
    std::ostringstream out; out << std::hex << std::uppercase << value; return out.str();
  }
};
  // pomeraj ima 12B i postavlja se u odgovarajuci opseg
int32_t signExtend12(uint16_t value) {
  value &= 0x0FFF;
  return (value & 0x0800) ? static_cast<int16_t>(value | 0xF000) : static_cast<int32_t>(value);
}

std::chrono::milliseconds timerPeriod(uint32_t value) {
  static const std::array<int64_t, 8> periods{500, 1000, 1500, 2000, 5000, 10000, 30000, 60000};
  return std::chrono::milliseconds(periods[value < periods.size() ? value : 0]);
}

}

struct Emulator::Impl {
  Memory memory;
  std::array<uint32_t, 16> gpr{}; // registri opste namene
  std::array<uint32_t, 3> csr{}; // status, handler, cause
  bool halted = false;
  bool terminalPending = false;
  bool timerPending = false;
  std::chrono::steady_clock::time_point timerDeadline;
  // svaka linija se razdvaja na pocetnu adresu i niz bajtova
  void loadHex(const std::string &path) {
    std::ifstream input(path);
    if (!input) throw Error("nije moguce otvoriti ulaznu datoteku: " + path);
    static const std::regex linePattern(R"(^\s*([0-9A-Fa-f]{1,8})\s*:\s*(.*?)\s*$)");
    std::string line;
    unsigned lineNumber = 0;
    while (std::getline(input, line)) {
      ++lineNumber;
      if (trim(line).empty()) continue;
      std::smatch match;
      if (!std::regex_match(line, match, linePattern))
        throw Error("neispravan hex zapis u liniji " + std::to_string(lineNumber));
      uint32_t address = parseNumber("0x" + match[1].str());
      std::istringstream bytes(match[2].str());
      std::string token;
      unsigned count = 0;
      while (bytes >> token) {
        if (token.size() != 2) throw Error("neispravan bajt u liniji " + std::to_string(lineNumber));
        if (static_cast<uint64_t>(address) + count > std::numeric_limits<uint32_t>::max())
          throw Error("hex zapis izlazi iz adresnog prostora u liniji " + std::to_string(lineNumber));
        memory.write8Initial(address + count, static_cast<uint8_t>(parseNumber("0x" + token)));
        ++count;
      }
      if (count == 0) throw Error("prazan hex zapis u liniji " + std::to_string(lineNumber));
    }
  }

  void store32(uint32_t address, uint32_t value) {
    memory.write32(address, value);
    if (address == TermOut) {
      std::cout.put(static_cast<char>(value & 0xFF));
      std::cout.flush();
    } else if (address == TimerConfig) {
      timerDeadline = std::chrono::steady_clock::now() + timerPeriod(value); // odredjuje novi period i racuna rok
      timerPending = false;
    }
  }
  // sp
  void push(uint32_t value) {
    gpr[14] -= 4;
    store32(gpr[14], value);
  }
  // pc, status, cause
  void interrupt(uint32_t cause) {
    push(csr[0]);
    push(gpr[15]);
    csr[2] = cause;
    // cause: 1 - neispravna instrukcija, 2 - prekid od tajmera, 
    // 3 - prekid od terminala, 4 - softerski prekid int
    csr[0] |= 0x4U; // globalno maskiranje spoljasnjih prekida
    gpr[15] = csr[1]; // %handler u pc
    // maska: 0x1 - od tajmera, 0x2 - od terminala, 0x4 - globalno
  }
  // proverava da li je korisnik uneo karakter
  bool pollTerminal() {
    pollfd descriptor{STDIN_FILENO, POLLIN, 0};
    int result = ::poll(&descriptor, 1, 0);
    if (result <= 0 || !(descriptor.revents & POLLIN)) return false;
    unsigned char character = 0;
    ssize_t readCount = ::read(STDIN_FILENO, &character, 1);
    if (readCount != 1) return false;
    memory.write32(TermIn, character);
    terminalPending = true;
    return true;
  }
  // ako je rok dostignut, pomera sledeci rok za ceo broj perioda dok ne bude ponovo u buducnosti
  void pollTimer() {
    auto now = std::chrono::steady_clock::now();
    if (now < timerDeadline) return;
    timerPending = true;
    auto period = timerPeriod(memory.read32(TimerConfig));
    do { timerDeadline += period; } while (timerDeadline <= now);
  }
  // proverava po prioritetima
  void acceptExternalInterrupt() {
    if (csr[0] & 0x4U) return; // globalni
    if (terminalPending && !(csr[0] & 0x2U)) { // od terminala
      terminalPending = false;
      interrupt(3);
    } else if (timerPending && !(csr[0] & 0x1U)) { // od tajmera
      timerPending = false;
      interrupt(2);
    }
  }

  bool validCsr(int index) const { return index >= 0 && index < static_cast<int>(csr.size()); }

  void invalidInstruction() { interrupt(1); }
  // dekodovanje
  void executeOne() {
    uint32_t pc = gpr[15];
    uint8_t first = memory.read8(pc), second = memory.read8(pc + 1);
    uint8_t third = memory.read8(pc + 2), fourth = memory.read8(pc + 3);
    gpr[15] += 4;
    int oc = first >> 4, mod = first & 0xF;
    int a = second >> 4, b = second & 0xF, c = third >> 4;
    int32_t d = signExtend12(static_cast<uint16_t>((third & 0xF) << 8) | fourth);
    uint32_t address = 0;
    bool condition = false;

    switch (oc) {
    case 0:
      if (mod != 0 || second != 0 || third != 0 || fourth != 0) invalidInstruction();
      else halted = true; // halt
      break;
    case 1:
      if (mod != 0 || second != 0 || third != 0 || fourth != 0) invalidInstruction();
      else interrupt(4); // softverski prekid, pa prelazak na adresu u %handler
      break;
    case 2:
      if (c != 0) invalidInstruction();
      else if (mod == 0) { push(gpr[15]); gpr[15] = gpr[a] + gpr[b] + d; } // direktni call
      else if (mod == 1) { push(gpr[15]); gpr[15] = memory.read32(gpr[a] + gpr[b] + d); } // indirektni call
      else invalidInstruction();
      break;
    case 3:
      if (mod <= 3 || (mod >= 8 && mod <= 11)) {
        condition = mod == 0 /* jmp */ || mod == 8 /* indirektni jmp */ ||
                    (mod % 8 == 1 && gpr[b] == gpr[c]) /* direktni i indirektni beq */ ||
                    (mod % 8 == 2 && gpr[b] != gpr[c]) /* direktni i indirektni bne */ ||
                    (mod % 8 == 3 && static_cast<int32_t>(gpr[b]) > static_cast<int32_t>(gpr[c])) /* direktni i indirektni bgt */ ;
        if (condition) {
          address = gpr[a] + d;
          gpr[15] = mod < 8 ? address /* direktan skok */ : memory.read32(address) /* indirektan skok */;
        }
      } else invalidInstruction();
      break;
    case 4: // xchg (exchange)
      if (mod != 0 || a != 0 || d != 0) invalidInstruction();
      else { uint32_t temporary = gpr[b]; gpr[b] = gpr[c]; gpr[c] = temporary; }
      break;
    case 5: // aritmetika
      if (d != 0) invalidInstruction();
      else if (mod == 0) gpr[a] = gpr[b] + gpr[c];
      else if (mod == 1) gpr[a] = gpr[b] - gpr[c];
      else if (mod == 2) gpr[a] = gpr[b] * gpr[c];
      else if (mod == 3 && gpr[c] != 0) gpr[a] = gpr[b] / gpr[c];
      else invalidInstruction();
      break;
    case 6: // logika
      if (d != 0) invalidInstruction();
      else if (mod == 0) gpr[a] = ~gpr[b];
      else if (mod == 1) gpr[a] = gpr[b] & gpr[c];
      else if (mod == 2) gpr[a] = gpr[b] | gpr[c];
      else if (mod == 3) gpr[a] = gpr[b] ^ gpr[c];
      else invalidInstruction();
      break;
    case 7: // shift
      if (d != 0) invalidInstruction();
      else if (mod == 0) gpr[a] = gpr[b] << (gpr[c] & 31U);
      else if (mod == 1) gpr[a] = gpr[b] >> (gpr[c] & 31U);
      else invalidInstruction();
      break;
    case 8: // store
      address = gpr[a] + gpr[b] + d;
      if (mod == 0) store32(address, gpr[c]); // obican upis
      else if (mod == 1) { gpr[a] += d; store32(gpr[a], gpr[c]); } // preincr, za push
      else if (mod == 2) store32(memory.read32(address), gpr[c]); // memind, za store
      else invalidInstruction();
      break;
    case 9:
      if (mod == 0 && c == 0 && d == 0 && validCsr(b)) gpr[a] = csr[b]; // citanje privilegovanih registara
      else if (mod == 1 && c == 0) gpr[a] = gpr[b] + d; // regdirpom
      else if (mod == 2) gpr[a] = memory.read32(gpr[b] + gpr[c] + d); // memdir
      else if (mod == 3 && c == 0) { gpr[a] = memory.read32(gpr[b]); gpr[b] += d; } // postincr, za pop i ret
      else if (mod == 4 && c == 0 && d == 0 && validCsr(a)) csr[a] = gpr[b]; // upis u privilegovani registar
      else if (mod == 5 && c == 0 && validCsr(a) && validCsr(b)) csr[a] = csr[b] | static_cast<uint32_t>(d); // upis 
      // ...iz jednog privilegovanog u drugi privilegovani registar
      else if (mod == 6 && validCsr(a)) csr[a] = memory.read32(gpr[b] + gpr[c] + d); // memdir u privilegovani, za iret
      else if (mod == 7 && c == 0 && validCsr(a)) { csr[a] = memory.read32(gpr[b]); gpr[b] += d; } // postincr
      else invalidInstruction();
      break;
    default:
      invalidInstruction();
      break;
    }
    gpr[0] = 0;
  }

  void printState() const {
    std::cout << "\n-----------------------------------------------------------------\n";
    std::cout << "Emulated processor executed halt instruction\n"; // halt
    std::cout << "Emulated processor state:\n";
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        int reg = row * 4 + column;
        if (reg < 10) std::cout << ' ';
        std::cout << 'r' << std::dec << reg << "=0x" << std::uppercase << std::hex
                  << std::setfill('0') << std::setw(8) << gpr[reg];
        if (column != 3) std::cout << "    ";
      }
      std::cout << '\n';
    }
    std::cout << std::dec;
  }
  // inicijalno svi registri su 0, pa ako se desi push, call 
  // ili prekid pre inicijalizacije sp, stek se smanjuje ka adresi 0xFFFFFFFC
  void run() {
    gpr.fill(0); csr.fill(0); gpr[15] = InitialPc; // 0x40000000U
    memory.write32(TimerConfig, 0); // pocetni period je 0.5s
    timerDeadline = std::chrono::steady_clock::now() + timerPeriod(0);
    TerminalMode terminalMode;
    while (!halted) {
      executeOne(); // izvrsi instrukciju
      if (halted) break; // ako je halt, kraj
      pollTerminal(); // proveri prekid od terminala
      pollTimer(); // proveri prekid od tajmera
      acceptExternalInterrupt(); // proveri spoljasnji prekid
    }
    printState();
  }
};

Emulator::Emulator() : impl_(std::make_unique<Impl>()) {}
Emulator::~Emulator() = default;
Emulator::Emulator(Emulator &&) noexcept = default;
Emulator &Emulator::operator=(Emulator &&) noexcept = default;

void Emulator::run(const std::string &hexPath) {
  impl_ = std::make_unique<Impl>();
  impl_->loadHex(hexPath);
  impl_->run();
}

}
