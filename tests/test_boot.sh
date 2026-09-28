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

# --- Disquette système (sprint 2) : STRATSED V2.0c, HYPER-BASIC, SAVE/LOAD, LPRINT
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
if [ -f "$DSK" ]; then
    TMP=$(mktemp -d)
    cp "$DSK" "$TMP/a.dsk"
    run_disk() {  # motif, puis arguments du banc
        n=$((n + 1))
        motif=$1
        shift
        out=$("$BIN" -c oricutron "$@" -s)
        if ! printf '%s\n' "$out" | grep -q -- "$motif"; then
            fail=$((fail + 1))
            echo "ÉCHEC [disque] : « $motif » absent de l'écran :"
            printf '%s\n' "$out" | grep -v '^$' | sed 's/^/    /'
        fi
    }
    # Écran de référence : Oricutron 1.2.0 (rev. 002279f), mêmes ROM, même disquette
    for m in "STRATSED V2.0c" "HYPER BASIC V2.0b" "TELEASS V1.0a" "1- HYPER-BASIC" "Votre choix:"; do
        run_disk "$m" -0 "$TMP/a.dsk" -f 600
    done
    run_disk "Imprimante,Drive:A-B-C-D" -0 "$TMP/a.dsk" -P /dev/null -f 600
    run_disk "44 Ko libres" -0 "$TMP/a.dsk" -f 900 -w 500 -t '1'
    run_disk "  42" -0 "$TMP/a.dsk" -f 1200 -w 500 -t '1~~~~~~PRINT 6*7\n'
    run_disk "secteurs libres, 88 fichiers" -0 "$TMP/a.dsk" -f 1500 -w 500 -t '1~~~~~~DIR\n'
    # Écriture : SAVE sur une copie, relecture dans une nouvelle session
    "$BIN" -c oricutron -0 "$TMP/a.dsk" -W "$TMP/b.dsk" -f 1600 -w 500 \
        -t '1~~~~~~10 PRINT "NEO6502"\nSAVE "ESSAI"\n' >/dev/null
    run_disk " NEO6502" -0 "$TMP/b.dsk" -f 1500 -w 500 -t '1~~~~~~LOAD "ESSAI"\nRUN\n'
    run_disk "secteurs libres, 89 fichiers" -0 "$TMP/b.dsk" -f 1500 -w 500 -t '1~~~~~~DIR\n'
    # Imprimante
    "$BIN" -c oricutron -0 "$TMP/a.dsk" -P "$TMP/lpr.txt" -f 1300 -w 500 \
        -t '1~~~~~~LPRINT "BONJOUR TELESTRAT"\n' >/dev/null
    n=$((n + 1))
    if ! grep -q "BONJOUR TELESTRAT" "$TMP/lpr.txt"; then
        fail=$((fail + 1))
        echo "ÉCHEC [imprimante] : sortie = $(od -c "$TMP/lpr.txt" | head -3)"
    fi
    rm -rf "$TMP"
else
    echo "test_boot : disquette système absente ($DSK), tests disque ignorés"
fi

echo "test_boot : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
