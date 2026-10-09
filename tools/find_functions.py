#!/usr/bin/env python3
"""Find every function in Battlezone's boot segment and its three overlays by
recursive descent, and write N64Recomp's symbol file.

Why not splat's linear split (what BattleTanx started from): a gap-to-next-
symbol split merges any function only reached through a pointer into the one
before it, and those merged functions were most of BattleTanx's crashes
(its STATUS.md rounds 39-85). Here a function's extent is what control flow
actually reaches from its start, and starts come from:

  * the crt0 entry and each module's entry (its first word, called through
    the segment table by the main loop),
  * n64sym's library matches (boot only; names kept as hints),
  * every jal in reached code,
  * every lui/addiu (or lui/ori) pair in reached code that forms an address
    in the section's code, and every aligned data word that does, when the
    target decodes as a plausible function start,
  * j tail calls to an address outside the current function.

Anything left unreached is reported (tools output), so code only reached in a
way this misses shows up as a gap rather than silently being merged.

Usage: find_functions.py <rom.z64> <n64sym boot output> <out syms toml> [report]
"""
import sys
import struct
import rabbitizer
from bzlayout import SECTIONS, NOT_CODE, KNOWN_NAMES, load_rom, word

RA = 31
BOOT = None  # the boot Section, set by Finder: code anything may call

# Starts with only circumstantial evidence; dropped if their code runs into data.
WEAK = ("ptr", "dataptr", "tail")

# Opcodes (bits 31..26)
OP_SPECIAL, OP_REGIMM, OP_J, OP_JAL = 0, 1, 2, 3
OP_BEQ, OP_BNE, OP_BLEZ, OP_BGTZ = 4, 5, 6, 7
OP_COP1 = 0x11
OP_BEQL, OP_BNEL, OP_BLEZL, OP_BGTZL = 0x14, 0x15, 0x16, 0x17
OP_LUI, OP_ADDIU, OP_ORI, OP_LW = 0x0F, 0x09, 0x0D, 0x23


def sext16(x):
    return x - 0x10000 if x & 0x8000 else x


class Section:
    def __init__(self, rom, name, rom_start, vram, rom_text_end, rom_end, bss_end):
        self.rom = rom
        self.name = name
        self.rom_start = rom_start
        self.vram = vram
        self.rom_end = rom_end
        self.vend = vram + (rom_end - rom_start)
        self.bss_end = bss_end

    def has(self, v):
        if not (self.vram <= v < self.vend):
            return False
        for name, lo, hi in NOT_CODE:
            if name == self.name and lo <= v < hi:
                return False
        return True

    def w(self, v):
        return word(self.rom, self.rom_start + (v - self.vram))


def in_ram(v):
    """Code addresses the game can reach: 8 MB of RDRAM (Expansion Pak)."""
    return 0x80000000 <= v < 0x80800000


def in_module_slot(v):
    """Where the boot segment's calls into the loaded module land."""
    return SECTIONS[1][2] <= v < max(x[5] for x in SECTIONS[1:])


def data_like(raw, ins):
    """Words compilers never emit as code but data tables are full of: a load or
    store based on $zero (a pointer 0x80xxxxxx decodes as `lb rX, imm($zero)`),
    and writes to $zero other than nop."""
    op = raw >> 26
    if raw == 0:
        return False
    if (ins.doesLoad() or ins.doesStore()) and ((raw >> 21) & 31) == 0:
        return True
    if ins.outputsToGprZero():
        return True
    if op in (OP_J, OP_JAL) and not in_ram(((raw & 0x3FFFFFF) << 2) | 0x80000000):
        return True
    if ins.isTrap() and (ins.getOpcodeName() not in ("teq", "tne") or ((raw >> 21) & 31) == 0):
        return True  # compilers only emit `teq divisor, $zero` (divide checks)
    return False


def valid(sec, v):
    """Instruction at v decodes as something a compiler or libultra would emit."""
    if not sec.has(v):
        return False
    raw = sec.w(v)
    ins = rabbitizer.Instruction(raw, v)
    if not ins.isValid():
        return False
    return True


def plausible_start(sec, v, max_insns=64):
    """A pointer/jal target looks like a real function: aligned, in range, and
    the first instructions decode and don't write $zero (data tends to)."""
    if v & 3 or not sec.has(v):
        return False
    if sec.w(v) == 0:
        # A function can start with a nop (rare); require something real after.
        if not sec.has(v + 4) or sec.w(v + 4) == 0:
            return False
    for k in range(max_insns):
        a = v + 4 * k
        if not sec.has(a):
            return False
        raw = sec.w(a)
        ins = rabbitizer.Instruction(raw, a)
        if not ins.isValid() or data_like(raw, ins):
            return False
        if raw >> 26 in (OP_J, OP_JAL):
            t = ((raw & 0x3FFFFFF) << 2) | 0x80000000
            if not in_ram(t) or not (sec.has(t) or (BOOT is not None and BOOT.has(t))
                                     or (sec is BOOT and raw >> 26 == OP_JAL and in_module_slot(t))):
                return False
        if ins.isBranch():
            tgt = a + 4 + (sext16(raw & 0xFFFF) << 2)
            if not sec.has(tgt) or abs(tgt - a) > (0x1000 if k == 0 else 0x10000):
                return False
        if raw == 0x03E00008:  # jr $ra
            return True
        if (raw >> 26) == OP_J or ins.isJrNotRa():
            return True
    return True


def resolve_const(sec, start, before, reg, depth=0):
    """Constant part of `reg` just before address `before`, walking back over
    lui / addiu / ori / addu (an addu with one constant side yields that side:
    the table base, with the other side being the scaled index)."""
    if depth > 4:
        return None
    a = before - 4
    while a >= start and a >= before - 4 * 32:
        raw = sec.w(a)
        op = raw >> 26
        rs, rt, rd = (raw >> 21) & 31, (raw >> 16) & 31, (raw >> 11) & 31
        if op == OP_LUI and rt == reg:
            return (raw & 0xFFFF) << 16
        if op in (OP_ADDIU, OP_ORI) and rt == reg:
            base = resolve_const(sec, start, a, rs, depth + 1)
            if base is None:
                return None
            imm = raw & 0xFFFF
            return (base + (sext16(imm) if op == OP_ADDIU else imm)) & 0xFFFFFFFF
        if op == OP_SPECIAL and (raw & 0x3F) in (0x21, 0x20) and rd == reg:
            for src in (rs, rt):
                v = resolve_const(sec, start, a, src, depth + 1)
                if v is not None and v >= 0x80000000:
                    return v
            return None
        if op == OP_LW and rt == reg:
            return None
        a -= 4
    return None


def crosses_boundary(sec, pc, tgt):
    """A forward `j` from pc to tgt leaves the function if a function boundary
    lies between them: `jr $ra`, its delay slot, then a stack-frame prologue
    (`addiu $sp, $sp, -N`, possibly after nop padding). Compilers do emit `j`
    for jumps inside a function, so the target alone doesn't say."""
    if tgt <= pc + 8:
        return False
    a = pc + 8
    while a + 8 < tgt:
        if sec.w(a) == 0x03E00008:
            b = a + 8
            while b < tgt and sec.w(b) == 0:
                b += 4
            if b <= tgt:
                raw = sec.w(b)
                if (raw >> 16) == 0x27BD and (raw & 0x8000):
                    return True
        a += 4
    return False


def find_jump_table(sec, start, jr_addr, reg):
    """Locate the table behind `jr reg`: the lw that loads reg, with its base
    resolved back to a constant. Returns (table_vram, count or None)."""
    a = jr_addr - 4
    while a >= start and a >= jr_addr - 4 * 24:
        raw = sec.w(a)
        if raw >> 26 == OP_LW and ((raw >> 16) & 31) == reg:
            base = resolve_const(sec, start, a, (raw >> 21) & 31)
            if base is None:
                return None, None
            table = (base + sext16(raw & 0xFFFF)) & 0xFFFFFFFF
            count = None
            b = a - 4
            while b >= start and b >= a - 4 * 32:
                r2 = sec.w(b)
                if r2 >> 26 == 0x0B:  # sltiu bound check
                    count = r2 & 0xFFFF
                    break
                b -= 4
            return table, count
        a -= 4
    return None, None


class Finder:
    def __init__(self, rom, sec_args):
        global BOOT
        self.secs = [Section(rom, *s) for s in sec_args]
        BOOT = self.secs[0]
        self.starts = {s.name: {} for s in self.secs}   # name -> {vram: reason}
        self.funcs = {s.name: {} for s in self.secs}    # name -> {vram: end}
        self.jtables = {s.name: {} for s in self.secs}  # name -> {table: (jr, targets)}
        self.names = {s.name: {} for s in self.secs}
        self.notes = []
        self.origin = {}

    def add_start(self, sec, v, why):
        if not sec.has(v) or v & 3:
            return False
        if v in self.starts[sec.name]:
            return False
        self.starts[sec.name][v] = why
        return True

    def descend(self, sec, start):
        """Walk control flow from start; return (end, calls, ptrs)."""
        seen = set()
        work = [start]
        calls, ptrs, tails = set(), set(), set()
        hi_regs = {}
        max_addr = start
        self.last_bad = False
        while work:
            pc = work.pop()
            while True:
                if pc in seen or not sec.has(pc):
                    break
                seen.add(pc)
                max_addr = max(max_addr, pc)
                raw = sec.w(pc)
                op = raw >> 26
                ins = rabbitizer.Instruction(raw, pc)
                if not ins.isValid() or data_like(raw, ins):
                    self.notes.append(f"{sec.name} func {start:#x} ({self.starts[sec.name].get(start)}): data-like word at {pc:#x} ({raw:08x})")
                    self.last_bad = True
                    break
                if op == OP_SPECIAL and (raw & 0x3F) == 0x0D:  # break: crt0 ends here
                    break
                # address-forming pairs (lui then addiu/ori on same reg)
                if op == OP_LUI:
                    hi_regs[(raw >> 16) & 31] = (raw & 0xFFFF) << 16
                elif op in (OP_ADDIU, OP_ORI) and ((raw >> 21) & 31) in hi_regs:
                    hi = hi_regs[(raw >> 21) & 31]
                    imm = raw & 0xFFFF
                    val = (hi + (sext16(imm) if op == OP_ADDIU else imm)) & 0xFFFFFFFF
                    ptrs.add(val)
                if op in (OP_J, OP_JAL):
                    t = ((raw & 0x3FFFFFF) << 2) | (pc & 0xF0000000)
                    if not (sec.has(t) or self.secs[0].has(t) or (sec is self.secs[0] and op == OP_JAL and in_module_slot(t))):
                        self.notes.append(f"{sec.name} func {start:#x}: jump to {t:#x} outside reachable code at {pc:#x}")
                        self.last_bad = True
                        break
                if op == OP_JAL:
                    calls.add(((raw & 0x3FFFFFF) << 2) | (pc & 0xF0000000))
                    self.origin[(((raw & 0x3FFFFFF) << 2) | (pc & 0xF0000000))] = (start, pc, "jal")
                    seen.add(pc + 4)
                    max_addr = max(max_addr, pc + 4)
                    pc += 8
                    continue
                if op == OP_J:
                    tgt = ((raw & 0x3FFFFFF) << 2) | (pc & 0xF0000000)
                    seen.add(pc + 4)
                    max_addr = max(max_addr, pc + 4)
                    # Tail call: a jump back before this function, or to a start
                    # with independent evidence (seed, jal/bal target, pointer).
                    # A start that only a `j` produced doesn't count, or loops
                    # jumping back to their head would split functions.
                    why = self.starts[sec.name].get(tgt)
                    if tgt < start or (tgt != start and why is not None and why != "tail") or crosses_boundary(sec, pc, tgt):
                        if tgt < start:
                            tails.add(tgt)
                            self.origin[tgt] = (start, pc, "j")
                    else:
                        work.append(tgt)
                    break
                if ins.isJrRa():
                    seen.add(pc + 4)
                    max_addr = max(max_addr, pc + 4)
                    break
                if ins.isJrNotRa():
                    seen.add(pc + 4)
                    max_addr = max(max_addr, pc + 4)
                    reg = (raw >> 21) & 31
                    table, count = find_jump_table(sec, start, pc, reg)
                    if table is not None:
                        tsec = sec if sec.has(table) else self.secs[0] if self.secs[0].has(table) else None
                        targets = []
                        if tsec is not None:
                            n = 0
                            while True:
                                if count is not None and n >= count:
                                    break
                                if not tsec.has(table + 4 * n):
                                    break
                                t = tsec.w(table + 4 * n)
                                if count is None and not (start <= t < start + 0x10000 and sec.has(t)):
                                    break
                                targets.append(t)
                                n += 1
                                if n > 512:
                                    break
                        self.jtables[sec.name][table] = (pc, targets)
                        for t in targets:
                            if sec.has(t):
                                work.append(t)
                    break
                if ins.isBranch():
                    tgt = pc + 4 + (sext16(raw & 0xFFFF) << 2)
                    if not sec.has(tgt):
                        self.notes.append(f"{sec.name} func {start:#x}: branch out of section at {pc:#x}")
                        self.last_bad = True
                        break
                    uncond = ins.isUnconditionalBranch() or (op == OP_REGIMM and ((raw >> 16) & 31) == 1 and ((raw >> 21) & 31) == 0)
                    likely = ins.isBranchLikely()
                    # delay slot
                    seen.add(pc + 4)
                    max_addr = max(max_addr, pc + 4)
                    if ins.doesLink():  # bal / bgezal: a call
                        calls.add(tgt)
                        self.origin[tgt] = (start, pc, "bal")
                        pc += 8
                        continue
                    work.append(tgt)
                    if uncond:
                        break
                    if likely:
                        work.append(pc + 8)
                        break
                    pc += 8
                    continue
                pc += 4
        return max_addr + 4, calls, ptrs, tails, seen

    def run(self, seeds):
        """Fixed-point iteration over the start set. Each round descends every
        current start, then rebuilds the set from scratch: the fixed seeds plus
        whatever that round's code derives (calls, tail jumps, address pairs,
        data words), minus jump-table case labels and n64sym hits that another
        function's control flow reaches. Rebuilding (rather than accumulating)
        means a start derived from a bad start disappears with it."""
        seed_set = {}
        for secname, v, why in seeds:
            seed_set.setdefault(secname, {})[v] = why
        self.starts = {s.name: dict(seed_set.get(s.name, {})) for s in self.secs}
        history = []
        for rounds in range(1, 60):
            self.funcs = {s.name: {} for s in self.secs}
            self.jtables = {s.name: {} for s in self.secs}
            self.notes = []
            found = []
            bad = {s.name: set() for s in self.secs}
            reached = {s.name: {} for s in self.secs}
            for sec in self.secs:
                for v in sorted(self.starts[sec.name]):
                    end, calls, ptrs, tails, seen = self.descend(sec, v)
                    self.funcs[sec.name][v] = end
                    if self.last_bad and self.starts[sec.name][v] in WEAK:
                        bad[sec.name].add(v)
                        continue  # don't derive anything from data
                    found.append((sec, v, calls | tails, ptrs))
                    for pc in seen:
                        if pc != v:
                            reached[sec.name].setdefault(pc, v)
            labels = {s.name: set() for s in self.secs}
            table_words = {s.name: set() for s in self.secs}
            for sec in self.secs:
                for table, (jr, targets) in self.jtables[sec.name].items():
                    labels[sec.name].update(targets)
                    tsec = sec if sec.has(table) else self.secs[0]
                    table_words[tsec.name].update(range(table, table + 4 * len(targets), 4))

            nxt = {s.name: {} for s in self.secs}
            for sec in self.secs:
                for v, why in seed_set.get(sec.name, {}).items():
                    if why == "n64sym" and v in reached[sec.name]:
                        continue
                    if v in bad[sec.name]:
                        continue
                    nxt[sec.name][v] = why

            def add(tsec, v, why):
                if v & 3 or not tsec.has(v) or v in nxt[tsec.name]:
                    return
                if why in WEAK and v in bad[tsec.name]:
                    return
                nxt[tsec.name][v] = why

            boot = self.secs[0]
            for sec, owner, calls, ptrs in found:
                for t in calls:
                    kind = "tail" if self.origin.get(t, (0, 0, ""))[2] == "j" else "call"
                    tsec = self.target_section(sec, t)
                    if tsec is not None and t not in labels[tsec.name] and plausible_start(tsec, t):
                        add(tsec, t, kind)
                    elif tsec is None and sec is boot:
                        # boot calling into the module slot: whichever module is
                        # loaded at the time; add it wherever it starts a function
                        for osec in self.secs[1:]:
                            if (osec.has(t) and t not in labels[osec.name] and plausible_start(osec, t)
                                    and self.looks_like_entry(osec, t)):
                                add(osec, t, "call")
                for p in ptrs:
                    tsec = self.target_section(sec, p)
                    if (tsec is not None and p not in labels[tsec.name]
                            and plausible_start(tsec, p) and self.looks_like_entry(tsec, p)):
                        add(tsec, p, "ptr")
            for sec in self.secs:
                covered = self.coverage(sec)
                for off in range(sec.vram, sec.vend, 4):
                    if off in covered or off in table_words[sec.name]:
                        continue
                    x = sec.w(off)
                    for tsec in self.data_targets(sec, x):
                        if (x not in labels[tsec.name] and plausible_start(tsec, x)
                                and self.looks_like_entry(tsec, x)):
                            add(tsec, x, "dataptr")
            key = {n: frozenset(d) for n, d in nxt.items()}
            cur = {n: frozenset(d) for n, d in self.starts.items()}
            if key == cur:
                return rounds  # funcs/jtables match self.starts
            history.append((cur, dict(self.starts)))
            hit = next((i for i, (h, _) in enumerate(history) if h == key), None)
            if hit is not None:
                # A short cycle (hand-written asm such as __osException, where
                # pointer words appear and vanish as coverage changes): settle on
                # the union of the cycle's states, minus case labels, and stop.
                merged = {n: {} for n in key}
                for _, st in history[hit:] + [(key, nxt)]:
                    for n, d in st.items():
                        for v, why in d.items():
                            if v not in labels[n]:
                                merged[n].setdefault(v, why)
                self.starts = merged
                self.notes.append(f"start set cycled (period {len(history) - hit}); used the union")
                self.funcs = {s.name: {} for s in self.secs}
                self.jtables = {s.name: {} for s in self.secs}
                for sec in self.secs:
                    for v in sorted(self.starts[sec.name]):
                        self.funcs[sec.name][v] = self.descend(sec, v)[0]
                return rounds
            self.starts = nxt
        self.notes.append("start set did not converge in 60 rounds")
        return rounds

    def target_section(self, sec, v):
        """Which section a code address from `sec` refers to: its own, else boot."""
        if sec.has(v):
            return sec
        boot = self.secs[0]
        if boot.has(v):
            return boot
        return None

    def data_targets(self, sec, v):
        out = []
        if sec.has(v):
            out.append(sec)
        boot = self.secs[0]
        if sec is boot:
            # boot data may point into any overlay's code (e.g. the segment table);
            # those are resolved per overlay by seeds instead.
            pass
        elif boot.has(v):
            out.append(boot)
        return out

    def looks_like_entry(self, sec, v):
        """Pointer targets: accept only addresses that begin a function, i.e. the
        preceding word ends one (jr $ra delay slot, nop padding, or an
        unconditional jump's delay slot) or v is already inside nothing."""
        if v == sec.vram:
            return True
        prev = sec.w(v - 4) if sec.has(v - 4) else 0
        prev2 = sec.w(v - 8) if sec.has(v - 8) else 0
        if prev == 0:
            return True
        if prev2 == 0x03E00008 or (prev2 >> 26) == OP_J:
            return True
        raw = sec.w(v)
        return (raw >> 16) == 0x27BD and (raw & 0x8000) != 0  # addiu sp,sp,-N

    def coverage(self, sec):
        cov = set()
        for fs, fe in self.funcs[sec.name].items():
            cov.update(range(fs, fe, 4))
        return cov


def load_n64sym(path):
    out = []
    for line in open(path, errors="replace"):
        line = line.strip().split("\r")[-1]
        parts = line.split()
        if len(parts) != 2:
            continue
        try:
            v = int(parts[0], 16)
        except ValueError:
            continue
        out.append((v, parts[1]))
    return out


def main():
    rom = load_rom(sys.argv[1])
    syms = load_n64sym(sys.argv[2])
    out_path = sys.argv[3]
    report_path = sys.argv[4] if len(sys.argv) > 4 else None

    f = Finder(rom, SECTIONS)
    boot = f.secs[0]
    seeds = [("boot", 0x80028400, "crt0")]
    for s in f.secs[1:]:
        seeds.append((s.name, s.vram, "module entry"))
    lib_names = {}
    for v, n in syms:
        if boot.has(v) and not any(t in n for t in ("_rodata_", "_data_", "_bss_")) and plausible_start(boot, v):
            lib_names[v] = n
            seeds.append(("boot", v, "n64sym"))
    for v, n in KNOWN_NAMES.items():
        if n is None:
            lib_names.pop(v, None)
        else:
            lib_names[v] = n
            seeds.append(("boot", v, "n64sym"))
    rounds = f.run(seeds)

    # Gap pass: code nothing reachable references (dead functions, or callers
    # that are themselves dead) still gets recompiled, so a pointer this
    # analysis can't see (computed at run time) still finds a function. A gap
    # start must follow a terminator and decode like a function; it is a weak
    # start (a `j` to it is not taken as a tail call).
    for _ in range(20):
        extra = []
        for sec in f.secs:
            cov = f.coverage(sec)
            fl = sorted(f.funcs[sec.name].items())
            if not fl:
                continue
            last = max(fe for _, fe in fl)
            v = fl[0][0]
            while v < last:
                if v in cov or sec.w(v) == 0:
                    v += 4
                    continue
                # Sweep the gap: each candidate is descended locally and the
                # sweep resumes after it, so a run of dead functions is found in
                # one pass; words that don't start a function (a string's tail
                # before the next prologue) are stepped over one at a time.
                if plausible_start(sec, v) and f.looks_like_entry(sec, v):
                    end = f.descend(sec, v)[0]
                    if not f.last_bad and end > v:
                        extra.append((sec.name, v, "tail"))
                        cov.update(range(v, end, 4))
                        v = end
                        continue
                v += 4
        if not extra:
            break
        seeds += extra
        rounds += f.run(seeds)

    # Overlap resolution: a start inside another function's reached extent.
    report = []
    for sec in f.secs:
        fl = sorted(f.funcs[sec.name].items())
        for i, (fs, fe) in enumerate(fl):
            if i + 1 < len(fl) and fl[i + 1][0] < fe:
                report.append(f"OVERLAP {sec.name} {fs:#x}-{fe:#x} contains start {fl[i+1][0]:#x} ({f.starts[sec.name][fl[i+1][0]]}) from {' '.join(hex(x) if isinstance(x,int) else x for x in f.origin.get(fl[i+1][0], ()))}")

    # Emit
    o = open(out_path, "w")
    o.write("# Battlezone: Rise of the Black Dogs (USA) -- N64Recomp symbol file.\n")
    o.write("# Generated by tools/find_functions.py (recursive descent; see its header).\n")
    o.write("# Hand corrections belong in tools/function_overrides.toml, not here.\n\n")
    used_names, dup_names = set(), []
    for sec in f.secs:
        fl = sorted(f.funcs[sec.name].items())
        o.write("[[section]]\n")
        o.write(f'name = ".{sec.name}"\n')
        o.write(f"rom = {sec.rom_start:#08x}\n")
        o.write(f"vram = {sec.vram:#010x}\n")
        o.write(f"size = {sec.rom_end - sec.rom_start:#x}\n\n")
        o.write("functions = [\n")
        for fs, fe in fl:
            # Overlapping entries (a start inside another function's reached
            # code: shared tails, mid-function entry points) are all emitted;
            # each compiles to its own copy of the shared code, so neither has
            # to fall through into the other.
            name = lib_names.get(fs) if sec is boot else None
            if name is not None and name in used_names:
                dup_names.append((name, fs))
                name = f"{name}_{fs:08X}"  # n64sym matched the same signature twice
            if name is not None:
                used_names.add(name)
            if name is None:
                name = f"func_{fs:08X}" if sec is boot else f"{sec.name}_func_{fs:08X}"
            o.write(f'    {{ name = "{name}", vram = {fs:#010x}, size = {fe - fs:#x} }},\n')
        o.write("]\n\n")
    o.close()

    if report_path:
        r = open(report_path, "w")
        r.write(f"descent rounds: {rounds}\n")
        for n, v in dup_names:
            r.write(f"duplicate n64sym name {n} at {v:#x} (suffixed)\n")
        for sec in f.secs:
            fl = sorted(f.funcs[sec.name].items())
            cov = f.coverage(sec)
            code_bytes = len(cov) * 4
            r.write(f"{sec.name}: {len(fl)} functions, {code_bytes:#x} code bytes reached\n")
            # unreached gaps that decode like code (candidates for missed functions)
            gaps = []
            v = sec.vram
            first = fl[0][0] if fl else sec.vram
            last = max(fe for _, fe in fl) if fl else sec.vram
            while v < last:
                if v not in cov:
                    g = v
                    while v < last and v not in cov:
                        v += 4
                    n_nonzero = sum(1 for a in range(g, v, 4) if sec.w(a) != 0)
                    if n_nonzero:
                        gaps.append((g, v, n_nonzero))
                else:
                    v += 4
            r.write(f"  unreached non-zero gaps inside code span: {len(gaps)}\n")
            for g, e, n in gaps:
                kind = "codeish" if plausible_start(sec, g + 4 * next((k for k in range((e - g) // 4) if sec.w(g + 4 * k) != 0), 0)) else "data"
                r.write(f"    {g:#x}-{e:#x} ({n} words) {kind}\n")
        for line in report:
            r.write(line + "\n")
        for line in f.notes[:500]:
            r.write(line + "\n")
        r.close()


if __name__ == "__main__":
    main()
