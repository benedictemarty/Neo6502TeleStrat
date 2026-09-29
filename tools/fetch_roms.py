#!/usr/bin/env python3
"""Récupère les ROM du Telestrat et génère src/roms/telestrat_roms.h.

Les ROM ne sont pas versionnées dans ce dépôt (droits d'auteur ORIC
International / Fabrice Broche) : ce script les copie depuis un clone local
(~/telemon, ~/Hyper-Basic, ~/tele-ass, ~/Telematic) ou les télécharge depuis GitHub, vérifie
leur MD5 et écrit un en-tête C par banque.

Usage : tools/fetch_roms.py [--offline] [--out src/roms/telestrat_roms.h]
"""
import argparse
import hashlib
import os
import re
import sys
import urllib.request

HOME = os.path.expanduser("~")

# nom C, taille dans la banque, décalage dans la banque, md5, clone local, URL
ROMS = [
    ("telestrat_telemon24", 0x4000, 0x0000, "9a432244d9ee4a49e8ddcde64af94e05",
     os.path.join(HOME, "telemon/original/telemon.rom"),
     "https://raw.githubusercontent.com/jedeoric/telemon/master/original/telemon.rom"),
    ("telestrat_hyperbas", 0x4000, 0x0000, "364bf095e0dc4222d75354d50b8cddfc",
     os.path.join(HOME, "Hyper-Basic/original/hyperbas.rom"),
     "https://raw.githubusercontent.com/assinie/Hyper-Basic/master/original/hyperbas.rom"),
    ("telestrat_teleass", 0x4000, 0x0000, "2324c9cc227c1327a72a667c97ed2990",
     os.path.join(HOME, "tele-ass/original/teleass.rom"),
     "https://raw.githubusercontent.com/jedeoric/tele-ass/master/original/teleass.rom"),
    # Cartouche de 8 Ko câblée en $E000-$FFFF (vecteur RESET = $E000) ;
    # la moitié basse de la banque la reflète (A13 non décodée : hypothèse).
    ("telestrat_telematic", 0x2000, 0x2000, "0a814078410353744e2947a8e9342e4e",
     os.path.join(HOME, "Telematic/original/telematic.rom"),
     "https://raw.githubusercontent.com/assinie/Telematic/master/original/telematic.rom"),
    # Cartouche Atmos : ORIC EXTENDED BASIC V1.1 (ROM d'oric.uf2, en-tête C de
    # reload-emulator, non versionné : pas d'URL). Proposée par le menu, en
    # banque 7 pour démarrer en mode Atmos (lecteur de cassette).
    ("telestrat_atmos", 0x4000, 0x0000, "a330779c42ad7d0c4ac6ef9e92788ec6",
     os.path.join(HOME, "reload-emulator/src/roms/oric_roms.h"), None),
]


def load(local, url, offline):
    if os.path.isfile(local):
        with open(local, "rb") as f:
            data = f.read()
        if local.endswith(".h"):  # tableau C : octets 0x.. dans l'ordre, les 16 premiers Ko
            data = bytes(int(x, 16) for x in re.findall(rb"0x([0-9A-Fa-f]{2})", data))[:0x4000]
        return data, local
    if offline or not url:
        raise FileNotFoundError(local)
    with urllib.request.urlopen(url, timeout=30) as r:
        return r.read(), url


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--offline", action="store_true", help="n'utiliser que les clones locaux")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "../src/roms/telestrat_roms.h"))
    args = ap.parse_args()

    lines = ["#pragma once", "", "// Généré par tools/fetch_roms.py : ne pas modifier, ne pas versionner.",
             "#include <stdint.h>", "",
             "// Emplacement des images : en flash par défaut (le firmware les copie dans ses",
             "// emplacements de banque en RAM) ; TELESTRAT_ROM_SECTION(nom) les place ailleurs",
             "#ifndef TELESTRAT_ROM_SECTION", "#define TELESTRAT_ROM_SECTION(x)", "#endif", ""]
    for name, size, offset, md5, local, url in ROMS:
        data, src = load(local, url, args.offline)
        got = hashlib.md5(data).hexdigest()
        if len(data) != size or got != md5:
            sys.exit(f"{name}: {src} : taille {len(data)} / md5 {got} inattendus (attendu {size} / {md5})")
        bank = bytearray(0x4000)
        for base in range(0, 0x4000, size):
            bank[base:base + size] = data
        lines.append(f"// {src} (md5 {md5}, {size} octets en +${offset:04X})")
        lines.append(f"const uint8_t TELESTRAT_ROM_SECTION(\"{name}\") {name}[0x4000] = {{")
        for i in range(0, 0x4000, 16):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in bank[i:i + 16]) + ",")
        lines.append("};")
        lines.append("")
        print(f"{name:22s} <- {src}")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as f:
        f.write("\n".join(lines))
    print(f"écrit : {os.path.normpath(args.out)}")


if __name__ == "__main__":
    main()
