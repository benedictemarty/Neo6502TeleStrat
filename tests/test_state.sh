#!/bin/sh
# test_state.sh — instantanés (savestates) au banc.
#   1. HYPER-BASIC (STRATSED) : variables, instantané (-X), reprise (-J) dans
#      une nouvelle session sans démarrer la disquette : PRINT les retrouve ;
#   2. mode Atmos : programme BASIC 1.1 en RAM, reprise, RUN ;
#   3. menu (-U) : Instantanés → « Enregistrer » (ETAT0001.STA) ; reprise par
#      le menu depuis une autre configuration de cartouches (STRATORIC remise) ;
#   4. fichier abîmé refusé, machine intacte.
set -u
BIN=${1:-build/telestrat_headless}
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
TMP=$(mktemp -d)
fail=0
n=0
check() {
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [instantané] : $1"
    fi
}

# 1. HYPER-BASIC
if [ -f "$DSK" ]; then
    cp "$DSK" "$TMP/a.dsk"
    "$BIN" -c oricutron -0 "$TMP/a.dsk" -f 1300 -w 500 -t '1~~~~~~A=1234:B$="INSTANTANE"\n' \
        -X "1290:$TMP/hb.sta" > /dev/null
    "$BIN" -c oricutron -0 "$TMP/a.dsk" -J "$TMP/hb.sta" -f 200 -w 20 -t 'PRINT A;B$\n' -s > "$TMP/hb.txt"
    check "HYPER-BASIC : variables retrouvées" "grep -q '1234INSTANTANE' '$TMP/hb.txt'"
else
    echo "test_state : disquette système absente ($DSK), test HYPER-BASIC ignoré"
fi

# 2. Mode Atmos
"$BIN" -c atmos -f 700 -w 150 -k 8 -t '10 PRINT "REPRIS"\n20 PRINT 6*7\n' -X "690:$TMP/at.sta" > /dev/null
"$BIN" -c atmos -J "$TMP/at.sta" -f 200 -w 20 -k 8 -t 'RUN\n' -s > "$TMP/at.txt"
check "Atmos : programme en RAM, RUN" "grep -q '^  REPRIS' '$TMP/at.txt' && grep -q '^   42' '$TMP/at.txt'"
check "Atmos : l'écran revient avec la machine" "grep -q '20 PRINT 6\\*7' '$TMP/at.txt'"

# 3. Menu : u × 7 = Instantanés, e = sélecteur, e = Enregistrer
mkdir "$TMP/cle"
printf 'bank7=@stratoric\n' > "$TMP/cle/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/cle" -f 400 -M "300:uuuuuuuee" > /dev/null 2> "$TMP/m1.txt"
check "menu : instantané enregistré" "grep -q 'Instantané enregistré : ETAT0001.STA' '$TMP/m1.txt' && [ -f '$TMP/cle/ETAT0001.STA' ]"
# Reprise depuis une configuration sans STRATORIC : u × 7, e, d (le fichier), e
rm "$TMP/cle/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/cle" -f 20 -M "5:uuuuuuuede" -O "$TMP/m2.txt" > /dev/null 2> "$TMP/m2_msg.txt"
check "menu : instantané repris, STRATORIC remise en banque 7" "grep -q 'Instantané repris : ETAT0001.STA' '$TMP/m2_msg.txt' && grep -q 'Banque 7 *STRATORIC' '$TMP/m2.txt'"
check "menu : ligne Instantanés, dernier fichier" "grep -q 'Instantanés *enregistrer ou reprendre la machine.*ETAT0001.STA' '$TMP/m2.txt'"

# 4. Fichier abîmé
head -c 1000 "$TMP/at.sta" > "$TMP/cle/ABIME.STA"
printf 'XXXX' | dd of="$TMP/cle/ABIME.STA" bs=1 seek=0 conv=notrunc 2> /dev/null
"$BIN" -c standard -U "$TMP/cle" -f 20 -M "5:uuuuuuuede" > /dev/null 2> "$TMP/m3.txt"
check "fichier abîmé refusé" "grep -q 'ABIME.STA : pas un instantané' '$TMP/m3.txt'"

rm -rf "$TMP"
echo "test_state : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
