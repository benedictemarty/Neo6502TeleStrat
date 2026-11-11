#!/usr/bin/env python3
"""extrait_rom.py — extrait une ROM de src/roms/telestrat_roms.h (fichier généré
par fetch_roms.py) vers un fichier binaire, après contrôle du MD5 annoncé.

    tools/extrait_rom.py NOM SORTIE [EN_TETE]

NOM : nom du tableau sans préfixe (hyperbas, teleass, telemon24, ...). Sert aux
tests qui ont besoin du fichier .rom d'origine (tests/test_menu.sh) quand il
n'existe plus à son emplacement habituel. Code de sortie 1 si la ROM est
absente de l'en-tête (macro TELESTRAT_HAS_* à 0), illisible ou de MD5 faux.
"""
import hashlib
import os
import re
import sys


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__, file=sys.stderr)
        return 2
    nom, sortie = argv[1], argv[2]
    racine = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    entete = argv[3] if len(argv) == 4 else os.path.join(racine, "src", "roms", "telestrat_roms.h")
    try:
        texte = open(entete, encoding="utf-8").read()
    except OSError as e:
        print(f"extrait_rom : {entete} illisible : {e}", file=sys.stderr)
        return 1
    m = re.search(r"\n// ([^\n]*)\n(?:#define [^\n]*\n)?static const uint8_t [^\n]*\btelestrat_%s\[(0x[0-9A-Fa-f]+|\d+)\] = \{(.*?)\n\};" % re.escape(nom), texte, re.S)
    if not m:
        print(f"extrait_rom : tableau telestrat_{nom} introuvable", file=sys.stderr)
        return 1
    commentaire, taille, corps = m.group(1), int(m.group(2), 0), m.group(3)
    octets = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", corps))
    if len(octets) != taille:
        print(f"extrait_rom : {len(octets)} octets lus, {taille} attendus", file=sys.stderr)
        return 1
    md5 = re.search(r"md5 ([0-9a-f]{32})", commentaire)
    if md5 and hashlib.md5(octets).hexdigest() != md5.group(1):
        print("extrait_rom : MD5 différent de celui annoncé (ROM absente ou altérée)", file=sys.stderr)
        return 1
    with open(sortie, "wb") as f:
        f.write(octets)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
