#!/usr/bin/env python3
"""png_info.py — vérifie une page PNG de la FX-80 émulée et compte l'encre.

    png_info.py FICHIER [y0 y1 x0 x1]

Vérifie la signature, les CRC de chaque bloc, le flux zlib (Adler-32), la
taille (1224 x 1584, 1 bit, gris). Affiche « largeur hauteur encre » : encre =
pixels noirs dans le rectangle (toute la page par défaut). Code de sortie 1 si
le fichier est invalide.
"""
import struct
import sys
import zlib


def main():
    data = open(sys.argv[1], "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("signature")
    i, idat, width, height = 8, b"", 0, 0
    while i < len(data):
        (n,) = struct.unpack(">I", data[i:i + 4])
        kind, body = data[i + 4:i + 8], data[i + 8:i + 8 + n]
        (crc,) = struct.unpack(">I", data[i + 8 + n:i + 12 + n])
        if zlib.crc32(kind + body) != crc:
            sys.exit("CRC du bloc %s" % kind)
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            if depth != 1 or color != 0:
                sys.exit("format")
        elif kind == b"IDAT":
            idat += body
        i += 12 + n
    raw = zlib.decompress(idat)  # vérifie aussi l'Adler-32
    stride = width // 8 + 1
    if len(raw) != stride * height:
        sys.exit("taille des données")
    y0, y1, x0, x1 = (int(v) for v in sys.argv[2:6]) if len(sys.argv) >= 6 else (0, height, 0, width)
    ink = 0
    for y in range(y0, y1):
        row = raw[y * stride + 1:(y + 1) * stride]
        for x in range(x0, x1):
            ink += not (row[x >> 3] >> (7 - (x & 7)) & 1)
    print(width, height, ink)


main()
