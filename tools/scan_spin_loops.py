#!/usr/bin/env python3
"""List busy-wait loops: short backward branches whose body loads memory and
makes no call. Under the runtime's cooperative scheduler such a loop never
lets the thread it waits on run, so each real one needs a yield hook
(bz_yield_via_priority_drop) inside it. Review the list: loops over arrays
also match; a spin re-reads the same address every iteration.

Usage: scan_spin_loops.py <rom.z64> <syms.toml> [--toml]
  --toml  print the [[patches.hook]] entries for battlezone.us.rev0.toml
          (one per loop head), leaving out NOT_SPINS
"""
import re
import sys
import rabbitizer
from bzlayout import load_rom, word


# Matches that aren't waits on another thread, or that never run here.
NOT_SPINS = {
    "ovl1_func_801712E0",  # loops over a stack variable
}
NOT_SPIN_PREFIXES = ("os", "__os", "__rmon")  # libultra; the runtime replaces it
# the dev-cartridge debug library (0x8003C700-0x8003EB3C): PI register polls
NOT_SPIN_RANGE = (0x8003C700, 0x8003EB3C)


def main():
    as_toml = "--toml" in sys.argv
    heads = {}
    rom = load_rom(sys.argv[1])
    txt = open(sys.argv[2]).read()
    for st in re.split(r"\[\[section\]\]", txt)[1:]:
        rs = int(re.search(r"rom = (0x[0-9a-f]+)", st).group(1), 16)
        vr = int(re.search(r"vram = (0x[0-9a-f]+)", st).group(1), 16)
        for m in re.finditer(r'name = "(\w+)", vram = (0x[0-9a-f]+), size = (0x[0-9a-f]+)', st):
            name, start, size = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
            for pc in range(start, start + size, 4):
                raw = word(rom, rs + pc - vr)
                ins = rabbitizer.Instruction(raw, pc)
                if not ins.isBranch() or ins.doesLink():
                    continue
                tgt = pc + 4 + (((raw & 0xFFFF) ^ 0x8000) - 0x8000) * 4
                if not (start <= tgt < pc and pc - tgt <= 4 * 10):
                    continue
                body = [rabbitizer.Instruction(word(rom, rs + a - vr), a) for a in range(tgt, pc + 8, 4)]
                if any(b.isFunctionCall() or b.isJump() for b in body):
                    continue
                loads = [b for b in body if b.doesLoad()]
                stores = [b for b in body if b.doesStore()]
                # a spin polls memory and doesn't write any (array loops usually do,
                # or advance a pointer register used by the load)
                if not loads or stores:
                    continue
                bases = {b.rs.value for b in loads}
                # Re-materializing a constant address each time round (lui, or an
                # addiu/ori onto a register that lui set) still polls one place.
                # Two passes: the loop wraps, so a lui in the closing branch's delay
                # slot feeds the next iteration's addiu. Judge on the second pass.
                const_regs = set()
                advanced = False
                for b in body + body:
                    if b is body[0]:
                        advanced = False
                    opname = b.getOpcodeName()
                    if opname == "lui":
                        const_regs.add(b.rt.value)
                        continue
                    if opname in ("addiu", "ori") and b.rs.value in const_regs:
                        const_regs.add(b.rt.value)
                        continue
                    if b.modifiesRt() and b.rt.value in bases:
                        advanced = True
                    if b.modifiesRd() and b.rd.value in bases:
                        advanced = True
                if advanced:
                    continue
                if not as_toml:
                    print(f"{name} loop {tgt:#010x}-{pc:#010x}: " + "; ".join(b.disassemble() for b in loads))
                    continue
                if (name in NOT_SPINS or name.startswith(NOT_SPIN_PREFIXES)
                        or NOT_SPIN_RANGE[0] <= start < NOT_SPIN_RANGE[1]):
                    continue
                heads[(name, tgt)] = "; ".join(b.disassemble() for b in loads)
    for (name, head), what in sorted(heads.items()):
        print(f'\n[[patches.hook]]  # {" ".join(what.split())}')
        print(f'func = "{name}"')
        print(f"before_vram = {head:#010x}")
        print('text = "    bz_yield_via_priority_drop(rdram);"')


if __name__ == "__main__":
    main()
