#!/bin/sh
# test_boot.sh — démarrage de TELEMON 2.4 avec les vraies ROM, par configuration.
# Référence : écran d'Oricutron (Telestrat, mêmes ROM) et notice « Extension
# RAM 64 Ko » (128 Ko affichés avec la cartouche RAM).
set -u
BIN=${1:-build/telestrat_headless}
fail=0
n=0

expect() {  # config motif
    n=$((n + 1))
    out=$("$BIN" -c "$1" -f 300 -s -b)
    if printf '%s\n' "$out" | grep -q -- "$2"; then
        :
    else
        fail=$((fail + 1))
        echo "ÉCHEC [$1] : « $2 » absent de l'écran :"
        printf '%s\n' "$out" | grep -v '^$' | sed 's/^/    /'
    fi
}

for c in standard ram64k oricutron telemon; do
    expect "$c" "TELEMON V2.4"
    expect "$c" "(c) 1986 ORIC International"
    expect "$c" "Drive:A-B-C-D"
    expect "$c" "Inserez une disquette"
done
expect standard  "64 Ko RAM, 56 Ko ROM"
expect ram64k    "128 Ko RAM, 32 Ko ROM"
expect oricutron "128 Ko RAM, 48 Ko ROM"
expect telemon   "64 Ko RAM, 16 Ko ROM"
# État des banques laissé par le RESET : $0F = RAM (notice, chap. V-10)
expect ram64k    "etat des banques (\$0200-\$0207) : 00 0F 0F 0F 0F"

echo "test_boot : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
