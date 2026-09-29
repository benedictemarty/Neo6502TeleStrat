#!/bin/sh
# test_stratoric.sh — cartouche STRATORIC intégrée (banque 7 SEDORIC + démarrage,
# 6 ORIC BASIC V1.1, 5 ORIC BASIC V1.0 : manuel du développeur Telestrat),
# sur un Telestrat standard (-U : répertoire tenant lieu de clé).
#   1. TELESTRA.CFG bank7=@stratoric : STRATORIC V4.0, PRINT 6*7 ;
#   2. par le menu (banque 7 -> STRATORIC, RESET) : les trois banques ;
#   3. cassette : CLOAD d'un programme puis RUN ;
#   4. disquette SEDORIC (3D Munch, Loriciels, si présente) : le jeu démarre.
set -u
BIN=${1:-build/telestrat_headless}
SED=${SEDORIC_DSK:-$HOME/oriclib/games/dsk/3DMunch.dsk}
TMP=$(mktemp -d)
mkdir "$TMP/cle" "$TMP/menu"
printf 'bank7=@stratoric\n' > "$TMP/cle/TELESTRA.CFG"
W=$(printf '~%.0s' $(seq 1 120))

"$BIN" -c standard -U "$TMP/cle" -f 400 -w 150 -k 8 -t 'PRINT 6*7\n' -s > "$TMP/cfg.txt" 2>&1
# Menu : h r e = choix pour la banque 7 ; d e = STRATORIC ; z u u e = RESET
"$BIN" -c standard -U "$TMP/menu" -M "300:hrededzuue" -f 500 -s > "$TMP/menu.txt" 2> "$TMP/menu_msg.txt"
# Cassette faite en mode Atmos, relue sous STRATORIC
"$BIN" -c atmos -f 700 -w 150 -k 8 -t '10 PRINT "STRATORIC OK"\n' -r "$TMP/ram.bin" > /dev/null
python3 "$(dirname "$0")/../tools/mktap.py" "$TMP/ram.bin" "$TMP/p.tap" P > /dev/null
"$BIN" -c standard -U "$TMP/cle" -K "$TMP/p.tap" -f 1300 -w 150 -k 8 -t "CLOAD\"\"\n${W}RUN\n" -s > "$TMP/k7.txt" 2>&1
if [ -f "$SED" ]; then
    cp "$SED" "$TMP/sed.dsk"
    "$BIN" -c standard -U "$TMP/cle" -0 "$TMP/sed.dsk" -f 1500 -r "$TMP/sed.ram" > /dev/null 2>&1
fi

fail=0
n=0
check() {
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [stratoric] : $1"
    fi
}
check "TELESTRA.CFG : STRATORIC V4.0" "grep -q 'STRATORIC V4.0' '$TMP/cfg.txt'"
check "BASIC 1.1 : PRINT 6*7" "grep -q '^   42' '$TMP/cfg.txt'"
check "menu : STRATORIC en banque 7, RESET" "grep -q 'Banque 7 : STRATORIC 4.0' '$TMP/menu_msg.txt' && grep -q 'STRATORIC V4.0' '$TMP/menu.txt'"
check "cassette : CLOAD puis RUN" "grep -q '^  STRATORIC OK' '$TMP/k7.txt'"
if [ -f "$SED" ]; then
    # Écran HIRES : les trois lignes de texte sont en \$BF68
    check "disquette SEDORIC : 3D Munch démarre" "python3 -c \"
import sys
m = open('$TMP/sed.ram', 'rb').read()
sys.exit(0 if b'LORICIELS' in bytes(c & 0x7F for c in m[0xBF68:0xBFE0]) else 1)\""
else
    echo "test_stratoric : $SED absente, disquette SEDORIC non essayée"
fi
if [ "$fail" -ne 0 ]; then
    cat "$TMP/menu_msg.txt"
    for f in cfg menu k7; do echo "--- $f :"; grep -v '^$' "$TMP/$f.txt" | head -8; done
fi
rm -rf "$TMP"
echo "test_stratoric : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
