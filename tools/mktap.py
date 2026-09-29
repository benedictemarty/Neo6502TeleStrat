#!/usr/bin/env python3
"""Fabrique une cassette .tap (programme BASIC) à partir d'une image de la RAM
(banc -r), pour les tests : programme de TXTTAB ($9A) à VARTAB ($9C) exclu.

En-tête : $16 $16 $16 $24, puis 9 octets — 2 inutilisés, type ($00 BASIC),
lancement automatique ($00 non), adresse du dernier octet (poids fort
d'abord ; incluse : avec l'adresse suivante, la ROM BASIC 1.1 attend un octet
de plus, observé au banc), adresse de début, 1 inutilisé — puis le nom
terminé par $00, puis les octets.

Usage : mktap.py RAM.bin SORTIE.tap NOM
"""
import sys


def main():
    ram = open(sys.argv[1], "rb").read()
    out, name = sys.argv[2], sys.argv[3].encode("ascii")
    start = ram[0x9A] | ram[0x9B] << 8
    end = ram[0x9C] | ram[0x9D] << 8  # premier octet après le programme
    data = ram[start:end]
    last = end - 1
    header = bytes([0x16, 0x16, 0x16, 0x24, 0, 0, 0x00, 0x00, last >> 8, last & 0xFF, start >> 8, start & 0xFF, 0])
    open(out, "wb").write(header + name + b"\0" + data)
    print(f"{out} : {len(data)} octets de ${start:04X} à ${end - 1:04X}")


if __name__ == "__main__":
    main()
