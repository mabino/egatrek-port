// Checks every instruction in the Unicorn-generated corpus through both the
// interpreter and the recompiled code (tools/trekre/src/trekre/corpus.py).
#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "dos/machine.h"

namespace trek::corpus {
extern void (*const kCorpusFns[])(dos::Machine&);
}

namespace {

struct OpCase {
    std::uint8_t code[8];
    int len;
    std::uint16_t regs[8];
    std::uint16_t flags;
    std::uint8_t mem[32], stack[32];
    std::uint16_t out_regs[8], out_sregs[4];
    std::uint16_t out_flags, mask;
    std::uint8_t out_mem[32], out_stack[32];
    std::uint16_t out_ip;
    const char* text;
};

const OpCase kCases[] = {
#include "ops_corpus.inc"
};

constexpr std::uint16_t kCS = 0x1000, kDS = 0x2000, kSS = 0x3000, kIP = 0x0100, kData = 0x0200;

const trek::dos::Program& empty_program() {
    static const std::uint8_t image[16] = {};
    static const trek::dos::Program p{image, 0x0800, 0x0800, 0, 0x0800, 0x100, 0, {}, {}, {}, {}};
    return p;
}

void setup(trek::dos::Machine& m, const OpCase& c) {
    auto& cpu = m.cpu;
    std::memcpy(cpu.r, c.regs, sizeof cpu.r);
    cpu.s[trek::dos::ES] = kDS, cpu.s[trek::dos::CS] = kCS, cpu.s[trek::dos::SS] = kSS, cpu.s[trek::dos::DS] = kDS;
    cpu.unpack(c.flags);
    std::memset(m.mem() + kCS * 16, 0, 0x10000);
    std::memset(m.mem() + kDS * 16, 0, 0x10000);
    std::memset(m.mem() + kSS * 16, 0, 0x10000);
    std::memcpy(m.mem() + kCS * 16 + kIP, c.code, static_cast<std::size_t>(c.len));
    std::memcpy(m.mem() + kDS * 16 + kData, c.mem, 32);
    std::memcpy(m.mem() + kSS * 16 + 0x100, c.stack, 32);
    cpu.pc = kCS * 16u + kIP;
}

std::string check(trek::dos::Machine& m, const OpCase& c) {
    std::string err;
    const auto& cpu = m.cpu;
    static const char* names[] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
    for (int i = 0; i < 8; ++i)
        if (cpu.r[i] != c.out_regs[i]) err += std::string(" ") + names[i];
    for (int i = 0; i < 4; ++i)
        if (cpu.s[i] != c.out_sregs[i]) err += " sreg" + std::to_string(i);
    if ((cpu.pack() ^ c.out_flags) & c.mask) err += " flags";
    if (std::memcmp(m.mem() + kDS * 16 + kData, c.out_mem, 32)) err += " mem";
    if (std::memcmp(m.mem() + kSS * 16 + 0x100, c.out_stack, 32)) err += " stack";
    if (cpu.pc != kCS * 16u + c.out_ip) err += " ip";
    return err;
}

}  // namespace

TEST_CASE("instruction corpus: interpreter and recompiled code match Unicorn") {
    trek::dos::Machine m(empty_program(), {});
    int interp_fail = 0, recomp_fail = 0;
    for (std::size_t i = 0; i < std::size(kCases); ++i) {
        const OpCase& c = kCases[i];
        setup(m, c);
        REQUIRE(m.step());
        if (auto e = check(m, c); !e.empty() && ++interp_fail <= 20)
            MESSAGE("interpreter: case " << i << " `" << std::string(c.text) << "`:" << e);
        setup(m, c);
        m.budget = 1;
        trek::corpus::kCorpusFns[i](m);
        if (auto e = check(m, c); !e.empty() && ++recomp_fail <= 20)
            MESSAGE("recompiled: case " << i << " `" << std::string(c.text) << "`:" << e);
    }
    CHECK(interp_fail == 0);
    CHECK(recomp_fail == 0);
    CHECK(std::size(kCases) > 1000);
}
