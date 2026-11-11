#!/usr/bin/env python3
"""Mesure de la charge du RP2040 sans carte.

Exécute l'ELF de mesure (cible telestrat_bench, compilée comme le firmware)
dans un émulateur Cortex-M0+ (unicorn), en rejouant une trace de bus du 65C02
enregistrée par le banc PC (telestrat_headless -B), et compte les cycles.

Modèle de cycles Cortex-M0+ (ARM DDI 0484C, « Cortex-M0+ Technical Reference
Manual », chapitre 3 « Instruction timing ») : 1 cycle par instruction, sauf
LDR/STR 2 (1 sur le port d'E/S SIO du RP2040), LDM/STM/PUSH/POP 1+N, POP avec
PC 3+N, branchement conditionnel pris 2 (non pris 1), B 2, BL 3, BX/BLX 2,
MULS 1 (multiplieur rapide du RP2040). Hypothèses : pas d'attente mémoire
(code chaud et données en SRAM), aucune contention avec le cœur 1 (DVI) ni le
DMA, code en flash toujours présent dans le cache XIP.

Budget : 372 MHz (horloge du DVI 960x544, défaut du firmware) ou 295,2 MHz
(800x480, --video480) pour 19968 cycles 6502 par trame de 20 ms.

Usage : rp2040_load.py BENCH.elf FIRMWARE.elf PRÉFIXE_TRACE [--disk F] [--every N]
"""
import argparse
import struct
import sys
import time

import os
import capstone
import unicorn
from unicorn import arm_const as A

# Brique commune (socle) : lecture ELF, modèle de cycles Cortex-M0+, comptage, diviseur du SIO
sys.path.insert(0, os.path.join(os.environ.get("RELOAD_DIR") or os.path.expanduser("~/Projets/Neo6502Socle"), "tools"))
from m0sim import read_elf, md, insn_cost, Cycles, Sio  # noqa: E402

SYSCLK = 372_000_000  # 960x544 (défaut du firmware) ; --video480 : 295,2 MHz
TICKS_PER_FRAME = 19968
FRAME_BUDGET = SYSCLK // 50  # 20 ms
STACK_TOP = 0x20041000
RET_TRAP = 0x00000100
TRACE_ADDR, EVENTS_ADDR, DISK_ADDR = 0x60000000, 0x5F000000, 0x70000000


def straight_cost(path, sym, syms_segs):
    """Coût d'une fonction courte (pilote de bus) lue de son début à son premier
    retour, branchements conditionnels comptés pris (borne haute) ; les accès
    au SIO (0xD0000000) sont à 1 cycle."""
    segs, syms = syms_segs
    addr, size = syms[sym]
    blob = None
    for vaddr, data, _ in segs:
        if vaddr <= addr < vaddr + len(data):
            blob = data[addr - vaddr:addr - vaddr + size]
    if blob is None:
        raise SystemExit("%s (0x%x) hors des segments chargés de %s" % (sym, addr, path))
    cost = 0
    listing = []
    for i in md.disasm(blob, addr):
        c, _ = insn_cost(i)
        m = i.mnemonic.split(".")[0]
        # Toutes les E/S de ces fonctions passent par le SIO (base 0xD0000000)
        if m.startswith("ldr") or m.startswith("str"):
            if "[r3" in i.op_str or "[r2" in i.op_str or "[r1" in i.op_str:
                c = 1 if "sp" not in i.op_str else c
        cost += c
        listing.append((i.mnemonic, i.op_str, c))
        if (m == "pop" and "pc" in i.op_str) or (m == "bx" and "lr" in i.op_str):
            break
        if i.group(capstone.CS_GRP_JUMP) and m not in ("bl", "bx", "blx", "b"):
            cost += 1  # branchement conditionnel compté pris (borne haute)
    return cost


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bench_elf")
    ap.add_argument("firmware_elf")
    ap.add_argument("trace_prefix")
    ap.add_argument("--disk", default=None)
    ap.add_argument("--every", type=int, default=15, help="mesurer une trame sur N")
    ap.add_argument("--frames", type=int, default=0, help="limiter le nombre de trames")
    ap.add_argument("--profile", action="store_true", help="cycles par fonction")
    ap.add_argument("--config", default="oricutron", choices=("oricutron", "standard"))
    ap.add_argument("--video480", action="store_true", help="budget à 295,2 MHz (800x480)")
    args = ap.parse_args()
    global SYSCLK, FRAME_BUDGET
    if args.video480:
        SYSCLK = 295_200_000
    FRAME_BUDGET = SYSCLK // 50

    # Coût du pilote de bus réel, depuis le firmware : sondes du pilote intégré
    # (neo6502_bus.h : un cycle de lecture, un d'écriture, prologue compris),
    # sinon fonctions appelées de reload (chips/wdc65C02cpu.h)
    fw = read_elf(args.firmware_elf)
    probes = "telestrat_bus_probe_read" in fw[1]
    if probes:
        drv = {s: straight_cost(args.firmware_elf, s, fw) for s in
               ("telestrat_bus_probe_read", "telestrat_bus_probe_write")}
    else:
        drv = {s: straight_cost(args.firmware_elf, s, fw) for s in
               ("wdc65C02cpu_tick", "wdc65C02cpu_get_data", "wdc65C02cpu_set_data", "wdc65C02cpu_set_irq")}
        call = 3  # BL
        for k in drv:
            drv[k] += call

    segs, syms = read_elf(args.bench_elf)
    uc = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_THUMB | unicorn.UC_MODE_MCLASS)
    uc.mem_map(0x00000000, 0x1000)
    uc.mem_write(RET_TRAP, b"\x00\xbf" * 8)  # NOP
    uc.mem_map(0x10000000, 0x200000)
    uc.mem_map(0x20000000, 0x100000)
    for vaddr, data, memsz in segs:
        uc.mem_write(vaddr, data)
    sio = Sio()
    uc.mmio_map(0xD0000000, 0x1000, sio.read, None, sio.write, None)

    trace = open(args.trace_prefix + ".trace", "rb").read()
    n_cycles = len(trace) // 4
    uc.mem_map(TRACE_ADDR, (len(trace) + 0xFFF) & ~0xFFF)
    uc.mem_write(TRACE_ADDR, trace)
    ev = open(args.trace_prefix + ".ev", "rb").read()
    uc.mem_map(EVENTS_ADDR, (len(ev) + 0xFFF) & ~0xFFF)
    uc.mem_write(EVENTS_ADDR, ev)
    disk_size = 0
    if args.disk:
        disk = open(args.disk, "rb").read()
        disk_size = len(disk)
        uc.mem_map(DISK_ADDR, (disk_size + 0xFFF) & ~0xFFF)
        uc.mem_write(DISK_ADDR, disk)

    def call_fn(name, *regs):
        uc.reg_write(A.UC_ARM_REG_SP, STACK_TOP)
        uc.reg_write(A.UC_ARM_REG_LR, RET_TRAP | 1)
        for r, v in zip((A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2), regs):
            uc.reg_write(r, v)
        uc.emu_start(syms[name][0] | 1, RET_TRAP)

    cyc = Cycles(uc, syms if args.profile else None)
    call_fn("bench_init", disk_size, 1 if args.config == "standard" else 0)

    # Coût de la relecture seule (bench_replay_only), à retrancher : la carte
    # réelle paie à la place le pilote de bus (compté à part)
    pos_addr = syms["bench_pos"][0]
    h = uc.hook_add(unicorn.UC_HOOK_BLOCK, cyc.hook)
    uc.ctl_flush_tb()
    cyc.total, cyc.pending = 0, None
    call_fn("bench_replay_only", 20000)
    replay_per_tick = cyc.total / 20000
    uc.hook_del(h)
    uc.ctl_flush_tb()
    uc.mem_write(pos_addr, struct.pack("<I", 0))
    cyc.profile.clear()
    cyc.block_profile.clear()

    frames = n_cycles // 20000
    if args.frames:
        frames = min(frames, args.frames)
    if frames < 1:
        raise SystemExit("trace trop courte : %d cycles (au moins 20000, une trame, sont nécessaires)" % n_cycles)
    slice_costs, frame_costs, end_costs = [], [], []
    t0 = time.time()
    for f in range(frames):
        call_fn("bench_frame_begin", f)
        measure = (f % args.every) == 0
        h = None
        if measure:
            h = uc.hook_add(unicorn.UC_HOOK_BLOCK, cyc.hook)
            uc.ctl_flush_tb()  # le code déjà traduit ne verrait pas le crochet
        total = 0
        for s in range(20):
            if measure:
                cyc.total, cyc.pending, sio.accesses = 0, None, 0
            call_fn("bench_ticks", 1000)
            if measure:
                # accès SIO à 1 cycle au lieu de 2 ; relecture retranchée
                c = cyc.total - sio.accesses - replay_per_tick * 1000
                slice_costs.append(c)
                total += c
        if measure:
            cyc.total, cyc.pending, sio.accesses = 0, None, 0
        call_fn("bench_frame_end")
        if measure:
            end = cyc.total - sio.accesses
            end_costs.append(end)
            frame_costs.append((f, total, end))
            uc.hook_del(h)
            uc.ctl_flush_tb()
    elapsed = time.time() - t0

    mism = struct.unpack("<I", uc.mem_read(syms["bench_mismatch"][0], 4))[0]
    first = struct.unpack("<I", uc.mem_read(syms["bench_first_mismatch"][0], 4))[0]
    pos = struct.unpack("<I", uc.mem_read(syms["bench_pos"][0], 4))[0]

    # Accès en lecture / écriture de la trace (pilote : set_data / get_data)
    import array
    words = array.array("I")
    words.frombytes(trace[:frames * 20000 * 4])
    reads = sum(1 for w in words if (w >> 16) & 1)
    writes = len(words) - reads
    if probes:
        drv_per_tick = (reads * drv["telestrat_bus_probe_read"] + writes * drv["telestrat_bus_probe_write"]) / len(words)
    else:
        drv_per_tick = drv["wdc65C02cpu_tick"] + (reads * drv["wdc65C02cpu_set_data"] + writes * drv["wdc65C02cpu_get_data"]) / len(words) + drv["wdc65C02cpu_set_irq"] / 4

    per_tick = [c / 1000 for c in slice_costs]
    worst_slice = max(per_tick)
    avg_tick = sum(per_tick) / len(per_tick)
    worst_frame = max(frame_costs, key=lambda x: x[1] + x[2])
    avg_end = sum(end_costs) / len(end_costs)

    def frame_total(sys_ticks_cycles, end):
        return sys_ticks_cycles * TICKS_PER_FRAME / 20000 + end + drv_per_tick * TICKS_PER_FRAME

    print("Trace : %d cycles 6502 (%d trames), relecture ARM : %d cycles, %d octets différents%s" % (
        n_cycles, frames, pos, mism, "" if not mism else " (premier au cycle %d)" % first))
    print("Pilote de bus réel (firmware) : %s -> %.1f cycles par cycle 6502" % (
        ", ".join("%s %d" % (k, v) for k, v in drv.items()), drv_per_tick))
    print("Système (telestrat_tick, relecture de %.1f cycles retranchée) : %.1f cycles par cycle 6502 en moyenne,"
          " %.1f au pire (tranche de 1 ms)" % (replay_per_tick, avg_tick, worst_slice))
    print("Fin de trame (clavier + rendu de l'écran) : %.0f cycles en moyenne, %d au pire" % (avg_end, max(end_costs)))
    avg_frame = frame_total(sum(f[1] for f in frame_costs) / len(frame_costs), avg_end)
    wf = frame_total(worst_frame[1], worst_frame[2])
    print("Trame : %.2f Mcycles en moyenne, %.2f au pire (trame %d) ; budget %.2f Mcycles (%.1f MHz, 20 ms)" % (
        avg_frame / 1e6, wf / 1e6, worst_frame[0], FRAME_BUDGET / 1e6, SYSCLK / 1e6))
    print("Charge du cœur 0 : %.0f %% en moyenne, %.0f %% au pire (hors USB : tuh_task, clé, modem)" % (
        100 * avg_frame / FRAME_BUDGET, 100 * wf / FRAME_BUDGET))
    loads = sorted(100 * frame_total(t, e) / FRAME_BUDGET for _, t, e in frame_costs)
    pct = lambda p: loads[min(len(loads) - 1, int(p * len(loads)))]
    over = sum(1 for x in loads if x > 100)
    print("Répartition des trames mesurées : médiane %.0f %%, 90e centile %.0f %%, 99e %.0f %%, max %.0f %% ;"
          " %d trame(s) au-delà du budget" % (pct(0.5), pct(0.9), pct(0.99), loads[-1], over))
    worst = sorted(frame_costs, key=lambda x: -(x[1] + x[2]))[:5]
    print("Pires trames (n°, système, fin de trame en Mcycles) : " +
          ", ".join("%d (%.2f + %.2f)" % (f, t / 1e6, e / 1e6) for f, t, e in worst))
    if args.profile:
        tot = sum(cyc.profile.values())
        print("Profil (%% des cycles mesurés, système et fin de trame) :")
        for name, c in sorted(cyc.profile.items(), key=lambda x: -x[1])[:15]:
            print("  %5.1f %%  %s" % (100 * c / tot, name))
    if args.profile:
        import subprocess
        inst = {}
        for baddr, n in cyc.block_profile.items():
            for a, c in cyc.blocks[baddr][3]:
                inst[a] = inst.get(a, 0) + c * n
        addrs = sorted(inst)
        # addr2line -i sort plusieurs paires (fonction, ligne) par adresse : -a les sépare
        out = subprocess.run(["arm-none-eabi-addr2line", "-a", "-i", "-f", "-C", "-e", args.bench_elf] +
                             ["%x" % a for a in addrs], capture_output=True, text=True).stdout.splitlines()
        by_func, by_line, cur = {}, {}, None
        k = 0
        chains = {}
        while k < len(out):
            if out[k].startswith("0x"):
                cur = int(out[k], 16)
                chains[cur] = []
                k += 1
                continue
            chains[cur].append((out[k], out[k + 1].split("/")[-1].split(" ")[0]))
            k += 2
        tot = sum(inst.values())
        for a, c in inst.items():
            ch = chains.get(a) or [("?", "?")]
            fn, line = ch[0]
            by_func[fn] = by_func.get(fn, 0) + c
            key = "%s (%s)" % (line, fn)
            by_line[key] = by_line.get(key, 0) + c
        by_feat = {}
        for a, c in inst.items():
            ch = chains.get(a) or [("?", "?")]
            names = [f for f, _ in ch]
            if names[-1] == "telestrat_tick":
                feat = names[-2] + " (inliné)" if len(names) >= 2 else "telestrat_tick (propre)"
                # ligne d'appel dans telestrat_tick, pour distinguer les appels
                if len(ch) >= 2:
                    feat = "%s @%s" % (names[-2], ch[-1][1].split(":")[-1])
            else:
                feat = names[-1]
            by_feat[feat] = by_feat.get(feat, 0) + c
        print("Par fonctionnalité (appel direct depuis telestrat_tick, ligne d'appel) :")
        for fn, c in sorted(by_feat.items(), key=lambda x: -x[1])[:22]:
            print("  %5.1f %%  %s" % (100 * c / tot, fn))
        print("Par fonction (inlinées comprises) :")
        for fn, c in sorted(by_func.items(), key=lambda x: -x[1])[:18]:
            print("  %5.1f %%  %s" % (100 * c / tot, fn))
        print("Par ligne :")
        for ln, c in sorted(by_line.items(), key=lambda x: -x[1])[:25]:
            print("  %5.1f %%  %s" % (100 * c / tot, ln))
    print("(%d trames mesurées sur %d, %.0f s)" % (len(frame_costs), frames, elapsed))


if __name__ == "__main__":
    main()
