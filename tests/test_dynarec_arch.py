"""Run host regressions against the production dynarec C functions."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CPU = ROOT / "mupen64plus-core/src/device/r4300"
DYNAREC = CPU / "new_dynarec"


def function(path, signature):
    source = path.read_text()
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor] + "\n"


class DynarecArchitectureTests(unittest.TestCase):
    def run_c(self, source):
        with tempfile.TemporaryDirectory(prefix="m64-dynarec-") as directory:
            path = Path(directory) / "test.c"
            executable = Path(directory) / "test"
            path.write_text("""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int u_int;
typedef unsigned char u_char;
""" + source)
            subprocess.run(
                shlex.split(os.environ.get("CC", "cc"))
                + ["-std=c99", "-O1", "-g", "-fsanitize=address,undefined",
                   "-fno-sanitize-recover=all", "-I" + str(DYNAREC),
                   str(path), "-o", str(executable)], check=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_instruction_cache_flushes_every_dirty_page(self):
        for arch in ("arm", "arm64"):
            with self.subTest(arch=arch):
                self.run_c("""
#define TARGET_SIZE_2 18
static u_int needs_clear_cache[2], flushed[2];
static void *base_addr = (void *)0x100000;
static void *base_addr_rx = (void *)0x100000;
static unsigned calls;
static void cache_flush(void *first, void *last) {
    uintptr_t begin = (uintptr_t)first - (uintptr_t)base_addr;
    uintptr_t end = (uintptr_t)last - (uintptr_t)base_addr;
    assert(begin < end && end <= (1U << TARGET_SIZE_2));
    assert(begin % 4096 == 0 && end % 4096 == 0);
    for (unsigned page = begin / 4096; page < end / 4096; ++page) {
        unsigned mask = 1U << (page % 32);
        assert(!(flushed[page / 32] & mask));
        flushed[page / 32] |= mask;
    }
    ++calls;
}
""" + function(DYNAREC / arch / ("assem_" + arch + ".c"),
               "static void do_clear_cache(void)") + """
static void check(uint32_t first, uint32_t second) {
    needs_clear_cache[0] = first;
    needs_clear_cache[1] = second;
    memset(flushed, 0, sizeof(flushed));
    calls = 0;
    do_clear_cache();
    assert(flushed[0] == first && flushed[1] == second);
    assert(needs_clear_cache[0] == 0 && needs_clear_cache[1] == 0);
    unsigned expected = 0;
    for (unsigned i = 0; i < 32; ++i) {
        expected += ((first >> i) & 1) && (i == 0 || !((first >> (i-1)) & 1));
        expected += ((second >> i) & 1) && (i == 0 || !((second >> (i-1)) & 1));
    }
    assert(calls == expected);
    do_clear_cache();
    assert(calls == expected);
}
int main(void) {
    check(0, 0);
    check(UINT32_MAX, UINT32_MAX);
    check(0x80000000U, 1);
    check(0xaaaaaaaaU, 0x55555555U);
    for (unsigned i = 0; i < 32; ++i) check(1U << i, 1U << i);
    uint32_t random = 42;
    for (unsigned i = 0; i < 10000; ++i) {
        random = random * 1664525U + 1013904223U;
        check(random, ~random);
    }
    return 0;
}
""")

    def test_code_cache_uses_host_pages_and_reports_failures(self):
        self.run_c("""
#include <errno.h>
#define TARGET_SIZE_2 25
#define _SC_PAGESIZE 1
#define M64MSG_ERROR 1
#define DebugMessage(...) ((void)0)
static long host_page_size;
static int protection_result, calls;
static uintptr_t protected_start;
static size_t protected_length;
static long sysconf(int name) { return host_page_size; }
static int mprotect(void *start, size_t length, int protection) {
    ++calls;
    protected_start = (uintptr_t)start;
    protected_length = length;
    assert(protected_start % host_page_size == 0);
    return protection_result;
}
""" + function(DYNAREC / "new_dynarec.c",
               "static int protect_code_cache(void *addr, int protection)") + """
int main(void) {
    const uintptr_t start = 0x105000;
    for (host_page_size = 4096; host_page_size <= 65536; host_page_size *= 4) {
        assert(protect_code_cache((void *)start, 7) == 0);
        assert(protected_start <= start);
        assert(protected_start + protected_length == start + (1U << TARGET_SIZE_2));
        assert(start - protected_start < (uintptr_t)host_page_size);
    }
    host_page_size = 16384;
    protection_result = -1;
    assert(protect_code_cache((void *)start, 7) == -1);
    int previous_calls = calls;
    host_page_size = -1;
    assert(protect_code_cache((void *)start, 7) == -1);
    assert(calls == previous_calls);
    return 0;
}
""")

    def test_arm64_jump_table_bounds(self):
        self.run_c("""
#include <setjmp.h>
#define TARGET_SIZE_2 25
static uintptr_t jump_table_symbols[] = {0x50000000, 0x60000000, 0x70000000};
#define JUMP_TABLE_SIZE (sizeof(jump_table_symbols) * 2)
static void *base_addr = (void *)0x10000000;
static void *base_addr_rx = (void *)0x20000000;
static u_char *out = (void *)0x10000000;
static jmp_buf rejected_jump;
#undef assert
#define assert(condition) do { if (!(condition)) longjmp(rejected_jump, 1); } while (0)
""" + function(DYNAREC / "arm64/assem_arm64.c",
               "static u_int genjmp(intptr_t addr)") + """
#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)
int main(void) {
    assert(genjmp((intptr_t)base_addr + 16) == 4);
    for (unsigned i = 0; i < 3; ++i) {
        unsigned expected = ((1U << TARGET_SIZE_2) - JUMP_TABLE_SIZE + i * 16) >> 2;
        assert(genjmp(jump_table_symbols[i]) == expected);
    }
    /* A missing far target must hit the range assertion without reading
     * beyond the symbol array (checked by AddressSanitizer). */
    if (!setjmp(rejected_jump)) {
        genjmp(0x80000000);
        abort();
    }
    return 0;
}
""")

    def test_arm64_allocator_preserves_platform_and_trampoline_registers(self):
        self.run_c("""
#include "arm64/assem_arm64.h"
#define MAXREG 45
#define CCREG 36
#define FTEMP 40
#define PTEMP 41
#define RJUMP 1
#define UJUMP 2
#define CJUMP 3
#define SJUMP 4
#define FJUMP 5
#define M64MSG_ERROR 1
#define DebugMessage(...) ((void)0)
struct regstat {
    signed char regmap[HOST_REGS];
    uint64_t dirty, isconst, u, uu;
};
static struct regstat regs[2];
static int rs1[2], rs2[2], rt1[2], rt2[2], bt[2], itype[2];
static uint64_t unneeded_reg[2], unneeded_reg_upper[2];
static int loop_reg(int i, int r, int preferred) { return preferred; }
static void lsn(u_char hsn[], int i, int *preferred) {}
""" + function(DYNAREC / "arm64/assem_arm64.c",
               "static void alloc_reg(struct regstat *cur,int i,signed char tr)")
               + function(DYNAREC / "arm64/assem_arm64.c",
                          "static void alloc_reg_temp(struct regstat *cur,int i,signed char tr)") + """
int main(void) {
    assert(EXCLUDE_REG == 18);
    assert(HOST_TRAMPREG >= HOST_REGS && HOST_TRAMPREG != 18 && HOST_TRAMPREG != LR);
    assert(!(CALLER_SAVED_REGS & (1U << 18)));
    struct regstat current = {0};
    memset(current.regmap, -1, sizeof(current.regmap));
    for (unsigned i = 1; i <= 35; ++i) {
        alloc_reg(&current, 0, i);
        assert(current.regmap[18] == -1);
    }
    for (unsigned i = 40; i <= MAXREG; ++i) {
        alloc_reg_temp(&current, 0, i);
        assert(current.regmap[18] == -1);
    }
    return 0;
}
""")

    def test_arm64_trampolines_use_reserved_register_and_keep_return_address(self):
        helpers = ("TLBR", "TLBP", "MULT", "MULTU", "DIV", "DIVU",
                   "DMULT", "DMULTU", "DDIV", "DDIVU")
        declarations = "\n".join(
            "static uintptr_t cached_interp_" + name + ";" for name in helpers
        )
        assignments = "\n".join(
            "cached_interp_" + name + " = (uintptr_t)base_addr_rx + 0x20000000U;"
            for name in helpers
        )
        self.run_c("""
#include "arm64/assem_arm64.h"
#undef TARGET_SIZE_2
#define TARGET_SIZE_2 12
#define fp_memory_map 0
static uint64_t cache[(1U << TARGET_SIZE_2) / sizeof(uint64_t)];
static void *base_addr = cache, *base_addr_rx = cache;
static uintptr_t jump_table_symbols[12];
static struct {
    struct { struct { unsigned rounding_modes[4]; intptr_t ram_offset; } new_dynarec_hot_state; } r4300;
    struct { void *dram; } rdram;
} g_dev;
static unsigned flush_calls;
#define __clear_cache mock_clear_cache
static void mock_clear_cache(char *start, char *end) {
    assert(start == (char *)cache + sizeof(cache) - JUMP_TABLE_SIZE);
    assert(end == (char *)cache + sizeof(cache));
    ++flush_calls;
}
""" + declarations + "\n"
               + function(DYNAREC / "arm64/assem_arm64.c", "static void arch_init(void)")
               + """
int main(void) {
""" + assignments + """
    uintptr_t table = (uintptr_t)base_addr_rx + sizeof(cache) - JUMP_TABLE_SIZE;
    jump_table_symbols[10] = table + 10 * 16 + 16;
    jump_table_symbols[11] = table + 11 * 16 - 16;
    arch_init();
    uint32_t *code = (uint32_t *)((char *)cache + sizeof(cache) - JUMP_TABLE_SIZE);
    for (unsigned i = 0; i < 12; ++i) {
        uint64_t target;
        memcpy(&target, code + i * 4 + 2, sizeof(target));
        assert(target == jump_table_symbols[i]);
        if (i < 10) {
            /* LDR x28, [PC, #8]; BR x28 leaves both x18 and x30 intact. */
            assert(code[i * 4] == 0x5800005cU);
            assert(code[i * 4 + 1] == 0xd61f0380U);
        } else {
            assert(code[i * 4] == (i == 10 ? 0x14000004U : 0x17fffffcU));
        }
    }
    assert(flush_calls == 1);
    return 0;
}
""")

    def test_branch_liveness_culls_overwritten_registers_within_bounds(self):
        source = (DYNAREC / "new_dynarec.c").read_text()
        start = source.index("// Don't need stuff which is overwritten")
        end = source.index("// Merge in delay slot", start)
        for registers in (8, 13, 28):
            with self.subTest(host_registers=registers):
                self.run_c("#define HOST_REGS " + str(registers) + "\n" + """
static struct { signed char regmap[HOST_REGS]; } regs[1];
static signed char regmap_pre[1][HOST_REGS];
static uint64_t cull(uint64_t nr, int hr) {
    int i = 0;
""" + source[start:end] + """
    return nr;
}
int main(void) {
    uint64_t all = (1ULL << HOST_REGS) - 1;
    for (unsigned changed = 0; changed < HOST_REGS; ++changed) {
        for (unsigned hr = 0; hr < HOST_REGS; ++hr)
            regs[0].regmap[hr] = regmap_pre[0][hr] = hr;
        regs[0].regmap[changed] = HOST_REGS;
        /* Branches can arrive with hr left at zero or past the final
         * register by an earlier loop. Both must cull every mapping. */
        assert(cull(all, 0) == (all & ~(1ULL << changed)));
        assert(cull(all, HOST_REGS) == (all & ~(1ULL << changed)));
        regs[0].regmap[changed] = regmap_pre[0][changed] = -1;
        assert(cull(all, HOST_REGS) == (all & ~(1ULL << changed)));
    }
    memset(regs, -1, sizeof(regs));
    assert(cull(all, HOST_REGS) == 0);
    return 0;
}
""")

    def test_init_failure_falls_back_without_start_or_cleanup(self):
        self.run_c("""
#define DYNAREC 1
#define NEW_DYNAREC 4
#define EMUMODE_PURE_INTERPRETER 0
#define EMUMODE_INTERPRETER 1
#define EMUMODE_DYNAREC 2
#define M64MSG_INFO 1
#define M64MSG_WARNING 2
#define DebugMessage(...) ((void)0)
typedef void (*handler)(void);
struct cached_interp {
    handler fin_block, not_compiled, not_compiled2, init_block, free_block, recompile_block;
    struct { void *block; } *actual;
};
struct r4300_core {
    unsigned emumode, start_address;
    struct cached_interp cached_interp;
    struct { unsigned last_addr; } cp0;
};
static int g_rom_pause, stop, success, pure_calls, start_calls, cleanup_calls, free_calls;
static unsigned pc;
static int *r4300_stop(struct r4300_core *cpu) { return &stop; }
static unsigned *r4300_pc(struct r4300_core *cpu) { return &pc; }
static void run_pure_interpreter(struct r4300_core *cpu) {
    assert(cpu->emumode == EMUMODE_PURE_INTERPRETER);
    ++pure_calls;
}
static void init_blocks(struct cached_interp *interp) {}
static void free_blocks(struct cached_interp *interp) { ++free_calls; }
static int new_dynarec_init(void) { return success; }
static void new_dyna_start(void) { assert(success); ++start_calls; }
static void new_dynarec_cleanup(void) { assert(success); ++cleanup_calls; }
static void cached_interpreter_jump_to(struct r4300_core *cpu, unsigned addr) {}
static void run_cached_interpreter(struct r4300_core *cpu) {}
static void cached_interp_FIN_BLOCK(void) {}
static void cached_interp_NOTCOMPILED(void) {}
static void cached_interp_NOTCOMPILED2(void) {}
static void cached_interp_init_block(void) {}
static void cached_interp_free_block(void) {}
static void cached_interp_recompile_block(void) {}
""" + function(CPU / "r4300_core.c", "void run_r4300(struct r4300_core* r4300)") + """
int main(void) {
    struct r4300_core cpu = {0};
    cpu.emumode = EMUMODE_DYNAREC;
    run_r4300(&cpu);
    assert(pure_calls == 1 && start_calls == 0 && cleanup_calls == 0 && free_calls == 1);
    success = 1;
    cpu.emumode = EMUMODE_DYNAREC;
    run_r4300(&cpu);
    assert(pure_calls == 1 && start_calls == 1 && cleanup_calls == 1 && free_calls == 2);
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
