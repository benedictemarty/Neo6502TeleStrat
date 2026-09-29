#!/usr/bin/env python3
"""Lit le texte d'un écran HIRES (émulation Minitel de TELEMATIC) dans une
image de la RAM de base (banc -r) : chaque cellule de 6 x 8 points en $A000 est
comparée au jeu de caractères HIRES en $9800. Rangées de 8 lignes, 40 colonnes ;
cellule inconnue = « ? », cellule vide ou attribut
série = espace ;
le bit 7 d'un octet (vidéo inverse) est appliqué.

Usage : hires_texte.py RAM.bin
"""
import sys


def points(octet):
    """6 points affichés : attribut série (bit 6 à 0) = fond ; le bit 7 inverse la vidéo."""
    if not octet & 0x40:
        return 0x3F if octet & 0x80 else 0
    return (octet ^ 0x3F if octet & 0x80 else octet) & 0x3F


def main():
    m = open(sys.argv[1], "rb").read()
    glyphes = {bytes(8): " "}
    for ch in range(33, 128):
        glyphes.setdefault(bytes(m[0x9800 + ch * 8 + k] & 0x3F for k in range(8)), chr(ch))
    for r in range(25):
        print("".join(glyphes.get(bytes(points(m[0xA000 + (r * 8 + k) * 40 + c]) for k in range(8)), "?")
                      for c in range(40)).rstrip())


if __name__ == "__main__":
    main()
