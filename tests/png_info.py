#!/usr/bin/env python3
"""png_info.py — vérifie une page PNG de la FX-80 émulée et compte l'encre.

    png_info.py FICHIER [y0 y1 x0 x1]

Vérifie la signature, les CRC de chaque bloc, le flux zlib (Adler-32), la
taille (1224 x 1584, 1 bit, gris). Les filtres PNG (0 à 4) de chaque ligne sont
défaits avant de compter. Affiche « largeur hauteur encre » : encre = pixels noirs
dans le rectangle (toute la page par défaut). Code de sortie 1 si le fichier est
invalide. Importable (fonction lire_png) sans rien exécuter.
"""
import struct
import sys
import zlib


def defiltre(raw, width, height):
    """Lignes de pixels (octets) après défiltrage PNG ; 1 bit par pixel : 1 octet de décalage (bpp)."""
    stride = (width + 7) // 8
    lignes = []
    prec = bytearray(stride)
    pos = 0
    for _ in range(height):
        ftype = raw[pos]
        cur = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for x in range(stride):
            a = cur[x - 1] if x else 0
            b = prec[x]
            c = prec[x - 1] if x else 0
            if ftype == 1:
                cur[x] = (cur[x] + a) & 255
            elif ftype == 2:
                cur[x] = (cur[x] + b) & 255
            elif ftype == 3:
                cur[x] = (cur[x] + ((a + b) >> 1)) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[x] = (cur[x] + pr) & 255
            elif ftype != 0:
                raise ValueError("filtre PNG %d inconnu" % ftype)
        lignes.append(cur)
        prec = cur
    return lignes


def lire_png(chemin):
    """(largeur, hauteur, lignes défiltrées) ; ValueError si le fichier est invalide."""
    data = open(chemin, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("signature")
    i, idat, width, height = 8, b"", 0, 0
    while i < len(data):
        (n,) = struct.unpack(">I", data[i:i + 4])
        kind, body = data[i + 4:i + 8], data[i + 8:i + 8 + n]
        (crc,) = struct.unpack(">I", data[i + 8 + n:i + 12 + n])
        if zlib.crc32(kind + body) != crc:
            raise ValueError("CRC du bloc %s" % kind)
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            if depth != 1 or color != 0:
                raise ValueError("format")
        elif kind == b"IDAT":
            idat += body
        i += 12 + n
    raw = zlib.decompress(idat)  # vérifie aussi l'Adler-32
    if len(raw) != ((width + 7) // 8 + 1) * height:
        raise ValueError("taille des données")
    return width, height, defiltre(raw, width, height)


def main():
    try:
        width, height, lignes = lire_png(sys.argv[1])
    except (ValueError, zlib.error, struct.error, OSError) as e:
        sys.exit("png_info : %s : %s" % (sys.argv[1], e))
    y0, y1, x0, x1 = (int(v) for v in sys.argv[2:6]) if len(sys.argv) >= 6 else (0, height, 0, width)
    ink = 0
    for y in range(max(y0, 0), min(y1, height)):
        row = lignes[y]
        for x in range(max(x0, 0), min(x1, width)):
            ink += not (row[x >> 3] >> (7 - (x & 7)) & 1)
    print(width, height, ink)


if __name__ == "__main__":
    main()
