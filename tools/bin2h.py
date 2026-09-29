#!/usr/bin/env python3
"""Fichier binaire -> en-tête C (tableau const, donc en flash sur le RP2040).

Usage : bin2h.py ENTRÉE SORTIE NOM
"""
import sys

src, dst, name = sys.argv[1:4]
data = open(src, "rb").read()
with open(dst, "w") as f:
    f.write("#pragma once\n// Généré par tools/bin2h.py depuis %s (%d octets) : ne pas modifier.\n" % (src, len(data)))
    f.write("#include <stdint.h>\nstatic const uint8_t %s[%d] = {\n" % (name, len(data)))
    for i in range(0, len(data), 32):
        f.write(",".join(str(b) for b in data[i:i + 32]) + ",\n")
    f.write("};\n")
