#!/bin/sh
# test_menu.sh — menu (OSD) du banc : un répertoire tient lieu de clé USB (-U).
#   1. configuration « telemon » (TELEMON seul) : par le menu (-M), STRATSED.DSK
#      dans A, hyperbas.rom en banque 6, Enregistrer, RESET (à froid) :
#      TELEMON voit 32 Ko de ROM, STRATSED et HYPER-BASIC démarrent ;
#   2. TELESTRA.CFG écrit (a=, bank6=) ; configuration « standard » avec un
#      TELESTRA.CFG a=, bank6= (emplacement de HYPER-BASIC réutilisé), bank5=
#      (emplacement supplémentaire) relu au démarrage : TELE-ASS deux fois ;
#   3. SAVE sur la disquette de la clé : réécrite dans le fichier ;
#   4. .rom de taille invalide refusée ; une cartouche de trop refusée (un
#      seul emplacement supplémentaire) ; image du menu (-O) produite.
set -u
BIN=${1:-build/telestrat_headless}
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
ROM=${HYPERBAS_ROM:-$HOME/Hyper-Basic/original/hyperbas.rom}
ROM2=${TELEASS_ROM:-$HOME/tele-ass/original/teleass.rom}
if [ ! -f "$DSK" ] || [ ! -f "$ROM" ] || [ ! -f "$ROM2" ]; then
    echo "test_menu : disquette, hyperbas.rom ou teleass.rom absente, ignoré"
    exit 0
fi
TMP=$(mktemp -d)
mkdir "$TMP/cle"
cp "$DSK" "$TMP/cle/STRATSED.DSK"
cp "$ROM" "$TMP/cle/hyperbas.rom"
cp "$ROM2" "$TMP/cle/teleass.rom"
head -c 1000 "$ROM" > "$TMP/cle/abime.rom"

# Menu : h = lecteur A, e = sélecteur, S = STRATSED, e ; r d = banque 6, e, H =
# hyperbas.rom, e ; z u = Enregistrer, e ; u = RESET, e
"$BIN" -c telemon -U "$TMP/cle" -M "400:heSerdeHezueue" -f 1700 -w 1600 -t '1~~~~~~' -s \
    > "$TMP/ecran1.txt" 2> "$TMP/menu1.txt"
# Démarrage suivant, configuration standard : TELESTRA.CFG seul, puis SAVE
# sur la disquette de la clé
cp "$TMP/cle/TELESTRA.CFG" "$TMP/cfg1"
echo "bank5=teleass.rom" >> "$TMP/cle/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/cle" -f 1800 -w 1200 -k 8 -t '1~~~~~~10 PRINT 42\nSAVE "MENUOK"\n~~~~~~~~~~' -s \
    > "$TMP/ecran2.txt" 2>&1
# Banque 5 : ROM abîmée puis teleass.rom ; banque 4 : hyperbas.rom de trop ;
# image du menu
rm "$TMP/cle/TELESTRA.CFG"
"$BIN" -c telemon -U "$TMP/cle" -M "10:hrddeAeeTedeHe" -O "$TMP/menu.ppm" -f 20 > /dev/null 2> "$TMP/menu3.txt"

fail=0
n=0
check() {
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [menu] : $1"
    fi
}
check "menu : disquette insérée dans A" "grep -q 'Lecteur A : STRATSED.DSK' '$TMP/menu1.txt'"
check "menu : hyperbas.rom en banque 6" "grep -q 'Banque 6 : hyperbas.rom' '$TMP/menu1.txt'"
check "après RESET : TELEMON voit la cartouche (32 Ko ROM)" "grep -q '32 Ko ROM' '$TMP/ecran1.txt'"
check "après RESET : STRATSED et HYPER BASIC" "grep -q 'STRATSED V2.0c' '$TMP/ecran1.txt' && grep -q 'HYPER BASIC V2.0b' '$TMP/ecran1.txt'"
check "TELESTRA.CFG écrit" "grep -qx 'a=STRATSED.DSK' '$TMP/cfg1' && grep -qx 'bank6=hyperbas.rom' '$TMP/cfg1'"
check "TELESTRA.CFG relu au démarrage : TELE-ASS en banques 2 et 5" "[ \$(grep -c 'TELEASS V1.0a' '$TMP/ecran2.txt') -eq 2 ]"
check "SAVE réécrit dans le fichier de la clé" "grep -q 'MENUOK' '$TMP/cle/STRATSED.DSK'"
check ".rom de 1000 octets refusée" "grep -q 'abime.rom : taille invalide' '$TMP/menu3.txt'"
check "emplacement supplémentaire pris (banque 5)" "grep -q 'Banque 5 : teleass.rom' '$TMP/menu3.txt'"
check "cartouche de trop refusée (banque 4)" "grep -q 'hyperbas.rom : plus de place' '$TMP/menu3.txt'"
check "image du menu 960 x 544" "head -c 20 '$TMP/menu.ppm' | grep -q '960 544'"
if [ "$fail" -ne 0 ]; then
    for f in menu1 menu3; do echo "--- $f :"; cat "$TMP/$f.txt"; done
    for f in ecran1 ecran2; do echo "--- $f :"; grep -v '^$' "$TMP/$f.txt" | head -12; done
fi
rm -rf "$TMP"
echo "test_menu : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
