#!/usr/bin/env python3
"""Coût, sur le cœur 1, d'une ligne du menu (osd_render_line) comparé à une
ligne de l'image du Telestrat (telestrat_video_line), sans carte.

Exécute les fonctions bench_osd_line et bench_video_line de l'ELF de mesure
(cible telestrat_bench) dans unicorn, avec le modèle de cycles Cortex-M0+ de
tools/rp2040_load.py (mêmes hypothèses : code et données en SRAM).

Usage : osd_cost.py BENCH.elf
"""
import os
import sys

import unicorn
from unicorn import arm_const as A

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rp2040_load as L  # noqa: E402

SYSCLK = 372_000_000
LINES = 272  # lignes de tampon du menu


def main():
    segs, syms = L.read_elf(sys.argv[1])
    uc = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_THUMB | unicorn.UC_MODE_MCLASS)
    uc.mem_map(0x00000000, 0x1000)
    uc.mem_write(L.RET_TRAP, b"\x00\xbf" * 8)
    uc.mem_map(0x10000000, 0x200000)
    uc.mem_map(0x20000000, 0x100000)
    for vaddr, data, _ in segs:
        uc.mem_write(vaddr, data)

    def call(name, *regs):
        uc.reg_write(A.UC_ARM_REG_SP, L.STACK_TOP)
        uc.reg_write(A.UC_ARM_REG_LR, L.RET_TRAP | 1)
        for r, v in zip((A.UC_ARM_REG_R0, A.UC_ARM_REG_R1), regs):
            uc.reg_write(r, v)
        uc.emu_start(syms[name][0] | 1, L.RET_TRAP)

    call("bench_osd_setup")
    cyc = L.Cycles(uc)
    uc.hook_add(unicorn.UC_HOOK_BLOCK, cyc.hook)
    uc.ctl_flush_tb()
    res = {}
    for name, n in (("bench_osd_line", LINES), ("bench_video_line", 224)):
        costs = []
        for line in range(n):
            cyc.total, cyc.pending = 0, None
            call(name, line)
            costs.append(cyc.total)
        res[name] = (sum(costs) / n, max(costs))
    for name, (avg, worst) in res.items():
        print(f"{name:18s} {avg:7.0f} cycles en moyenne, {worst:6d} au pire"
              f" = {avg / SYSCLK * 1e6:5.1f} µs ({worst / SYSCLK * 1e6:5.1f} au pire) à 372 MHz")
    d = (res["bench_osd_line"][1] - res["bench_video_line"][1]) / SYSCLK * 1e6
    print(f"écart au pire : {d:+.1f} µs par ligne (image du Telestrat mesurée sur carte : 35 µs par ligne, encodage TMDS compris)")


if __name__ == "__main__":
    main()
