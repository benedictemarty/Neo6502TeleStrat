#!/bin/sh
# test_tape.sh — cassette en mode Atmos (configuration « atmos » : ROM BASIC
# 1.1 en banque 7). Le programme est tapé, relevé dans la RAM (-r), mis en
# cassette par tools/mktap.py, puis rechargé par CLOAD"" (-K) et exécuté.
# Puis par le menu, depuis un Telestrat standard : ROM Atmos intégrée en
# banque 7, cassette de la clé (-U), RESET, CLOAD, RUN ; bandeau de la
# cassette sur la sortie DVI (-D) pendant la lecture. Enfin CSAVE (-C) : le
# fichier ESSAI.TAP écrit est rechargé par CLOAD et exécuté ; bandeau
# « Écriture » pendant l'enregistrement.
set -u
BIN=${1:-build/telestrat_headless}
if ! command -v python3 > /dev/null; then
    echo "test_tape : python3 absent, ignoré"
    exit 0
fi
TMP=$(mktemp -d)
W=$(printf '~%.0s' $(seq 1 120))   # attente du chargement (~2 s de bande)
"$BIN" -c atmos -f 700 -w 150 -k 8 -t '10 PRINT "CASSETTE OK"\n20 PRINT 6*7\n' -r "$TMP/ram.bin" > /dev/null
python3 "$(dirname "$0")/../tools/mktap.py" "$TMP/ram.bin" "$TMP/essai.tap" ESSAI > /dev/null
"$BIN" -c atmos -K "$TMP/essai.tap" -f 1500 -w 150 -k 8 -t "CLOAD\"\"\n${W}LIST\nRUN\n" -s > "$TMP/ecran.txt"
# Sans cassette : la ROM cherche toujours
"$BIN" -c atmos -f 700 -w 150 -k 8 -t 'CLOAD""\n' -s > "$TMP/vide.txt"
# Chargement suivi : nom affiché pendant la lecture
"$BIN" -c atmos -K "$TMP/essai.tap" -f 260 -w 150 -k 8 -t 'CLOAD""\n' -s > "$TMP/pendant.txt"

# Par le menu : h r e d d e = banque 7 <- ROM intégrée Atmos ; l d d d d e d e =
# cassette <- essai.tap ; z u u e = RESET (à froid)
mkdir "$TMP/cle"
cp "$TMP/essai.tap" "$TMP/cle/essai.tap"
"$BIN" -c standard -U "$TMP/cle" -M "300:hreddelddddedezuue" -f 1700 -w 450 -k 8 -t "CLOAD\"\"\n${W}RUN\n" -s \
    > "$TMP/menu.txt" 2> "$TMP/menu_msg.txt"
"$BIN" -c standard -U "$TMP/cle" -M "300:hreddelddddedezuue" -f 560 -w 450 -k 8 -t 'CLOAD""\n' -D "$TMP/dvi.ppm" \
    > /dev/null 2>&1

# CSAVE vers un fichier, puis relecture
mkdir "$TMP/rec"
"$BIN" -c atmos -C "$TMP/rec" -f 1500 -w 150 -k 8 -t '10 PRINT "CSAVE OK"\n20 PRINT 6*7\nCSAVE"ESSAI"\n' -s \
    > "$TMP/csave.txt" 2> "$TMP/csave_msg.txt"
"$BIN" -c atmos -K "$TMP/rec/ESSAI.TAP" -f 1500 -w 150 -k 8 -t "CLOAD\"\"\n${W}RUN\n" -s > "$TMP/relu.txt"
"$BIN" -c atmos -C "$TMP/rec" -f 620 -w 150 -k 8 -t '10 PRINT "CSAVE OK"\n20 PRINT 6*7\nCSAVE"ESSAI"\n' \
    -D "$TMP/ecrit.ppm" > /dev/null 2>&1

fail=0
n=0
check() {
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [cassette] : $1"
    fi
}
check "mode Atmos : BASIC 1.1" "grep -q 'ORIC EXTENDED BASIC V1.1' '$TMP/ecran.txt'"
check "cassette .tap de 32 octets" "[ \$(wc -c < '$TMP/essai.tap') -eq 51 ]"
check "sans cassette : Searching" "grep -q 'Searching' '$TMP/vide.txt'"
check "pendant la lecture : Loading .. ESSAI" "grep -q 'Loading .. *ESSAI' '$TMP/pendant.txt'"
check "LIST : programme rechargé" "grep -q '10 PRINT \"CASSETTE OK\"' '$TMP/ecran.txt' && grep -q '20 PRINT 6\\*7' '$TMP/ecran.txt'"
check "RUN : CASSETTE OK puis 42" "grep -q '^  CASSETTE OK' '$TMP/ecran.txt' && grep -q '^   42' '$TMP/ecran.txt'"
check "menu : ROM Atmos en banque 7, cassette insérée" "grep -q 'Banque 7 : ORIC BASIC 1.1 (Atmos)' '$TMP/menu_msg.txt' && grep -q 'Cassette : essai.tap' '$TMP/menu_msg.txt'"
check "menu puis RESET : mode Atmos, CLOAD, RUN" "grep -q 'ORIC EXTENDED BASIC V1.1' '$TMP/menu.txt' && grep -q '^  CASSETTE OK' '$TMP/menu.txt'"
check "bandeau de la cassette sur la sortie DVI" "python3 -c \"
import sys
d = open('$TMP/dvi.ppm', 'rb').read()
px = d[len(b'P6\\n960 544\\n255\\n'):]
row = [px[((256 * 2 + 3) * 960 + x) * 3:((256 * 2 + 3) * 960 + x) * 3 + 3] for x in range(960)]
sys.exit(0 if b'\\xff\\xff\\x00' in row and b'\\x00\\x00\\xff' in row else 1)\""
check "CSAVE : ESSAI.TAP écrit (en-tête, nom)" "grep -q 'enregistrement de .*ESSAI.TAP' '$TMP/csave_msg.txt' && head -c 19 '$TMP/rec/ESSAI.TAP' | tail -c 6 | grep -q ESSAI"
check "CSAVE puis CLOAD : RUN donne CSAVE OK et 42" "grep -q '^  CSAVE OK' '$TMP/relu.txt' && grep -q '^   42' '$TMP/relu.txt'"
check "bandeau pendant l'enregistrement (point rouge)" "python3 -c \"
import sys
d = open('$TMP/ecrit.ppm', 'rb').read()
px = d[len(b'P6\\n960 544\\n255\\n'):]
row = [px[((256 * 2 + 5) * 960 + x) * 3:((256 * 2 + 5) * 960 + x) * 3 + 3] for x in range(120, 140)]
sys.exit(0 if b'\\xff\\x00\\x00' in row else 1)\""
if [ "$fail" -ne 0 ]; then
    cat "$TMP/menu_msg.txt" "$TMP/csave_msg.txt"
    for f in ecran vide pendant menu; do echo "--- $f :"; grep -v '^$' "$TMP/$f.txt" | head -14; done
fi
rm -rf "$TMP"
echo "test_tape : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
