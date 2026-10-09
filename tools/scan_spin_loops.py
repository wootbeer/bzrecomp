#!/usr/bin/env python3
"""List busy-wait loops: short backward branches whose body loads memory and
makes no call. Under the runtime's cooperative scheduler such a loop never
lets the thread it waits on run, so each real one needs a yield hook
(bz_yield_via_priority_drop) inside it. Review the list: loops over arrays
also match; a spin re-reads the same address every iteration.

Usage: scan_spin_loops.py <rom.z64> <syms.toml>
"""
import re
import sys
import rabbitizer
from bzlayout import load_rom, word


def main():
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
                advanced = any(b.modifiesRt() and b.rt.value in bases for b in body) or \
                           any(b.modifiesRd() and b.rd.value in bases for b in body)
                if advanced:
                    continue
                print(f"{name} loop {tgt:#010x}-{pc:#010x}: " + "; ".join(b.disassemble() for b in loads))


if __name__ == "__main__":
    main()
