#!/bin/sh
# test_menu.sh — menu (OSD) du banc : un répertoire tient lieu de clé USB (-U).
#   1. configuration « telemon » (TELEMON seul) : par le menu (-M), STRATSED.DSK
#      dans A, teleass.rom en banque 5, Enregistrer, RESET (à froid) :
#      TELEMON voit 32 Ko de ROM, STRATSED et TELE-ASS démarrent. (Pas
#      HYPER-BASIC seul : sans BONJOUR.COM il appelle la banque 5 vide et
#      exécute le bus flottant, résultat qui dépend du cycle exact : voir
#      docs/ARCHITECTURE.md, « Banque vide et démarrage à froid ».)
#   2. TELESTRA.CFG écrit (a=, bank5=) ; configuration « standard » avec ce
#      TELESTRA.CFG plus bank6= (emplacement de HYPER-BASIC réutilisé) relu au
#      démarrage : TELE-ASS deux fois (banques 2 et 5) ;
#   3. SAVE sur la disquette de la clé : réécrite dans le fichier ;
#   4. périphériques : imprimante coupée par le menu et enregistrée
#      (impression=non) : LPRINT n'écrit rien ; impression=oui : il écrit ;
#   5. .rom de taille invalide refusée ; une cartouche de trop refusée (un
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

# Menu : h = lecteur A, e = sélecteur, S = STRATSED, e ; r d d = banque 5, e,
# T = teleass.rom, e ; z u = Enregistrer, e ; u = RESET, e
"$BIN" -c telemon -U "$TMP/cle" -M "400:heSerddeTezueue" -f 1700 -w 1600 -t '1~~~~~~' -s \
    > "$TMP/ecran1.txt" 2> "$TMP/menu1.txt"
# Démarrage suivant, configuration standard : TELESTRA.CFG seul, puis SAVE
# sur la disquette de la clé
cp "$TMP/cle/TELESTRA.CFG" "$TMP/cfg1"
echo "bank6=hyperbas.rom" >> "$TMP/cle/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/cle" -f 1800 -w 1200 -k 8 -t '1~~~~~~10 PRINT 42\nSAVE "MENUOK"\n~~~~~~~~~~' -s \
    > "$TMP/ecran2.txt" 2>&1
# Banque 5 : ROM abîmée puis teleass.rom ; banque 4 : hyperbas.rom de trop ;
# image du menu
rm "$TMP/cle/TELESTRA.CFG"
"$BIN" -c telemon -U "$TMP/cle" -M "10:hrddeAeeTedeHe" -O "$TMP/menu.ppm" -f 20 > /dev/null 2> "$TMP/menu3.txt"

# Périphériques : u × 6 = imprimante, e = coupée ; z u e = Enregistrer
mkdir "$TMP/prn"
cp "$DSK" "$TMP/prn/a.dsk"
"$BIN" -c standard -U "$TMP/prn" -P "$TMP/lp0.txt" -M "5:uuuuuuezuex" -f 10 > /dev/null 2> "$TMP/prn_msg.txt"
cp "$TMP/prn/TELESTRA.CFG" "$TMP/cfg_prn"
"$BIN" -c standard -U "$TMP/prn" -0 "$TMP/prn/a.dsk" -P "$TMP/lp1.txt" -f 1500 -w 1200 -k 8 \
    -t '1~~~~~~LPRINT "IMPRIME"\n' > /dev/null 2>&1
sed -i 's/^impression=non/impression=oui/' "$TMP/prn/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/prn" -0 "$TMP/prn/a.dsk" -P "$TMP/lp2.txt" -f 1500 -w 1200 -k 8 \
    -t '1~~~~~~LPRINT "IMPRIME"\n' > /dev/null 2>&1

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
check "menu : teleass.rom en banque 5" "grep -q 'Banque 5 : teleass.rom' '$TMP/menu1.txt'"
check "après RESET : TELEMON voit la cartouche (32 Ko ROM)" "grep -q '32 Ko ROM' '$TMP/ecran1.txt'"
check "après RESET : STRATSED et TELE-ASS" "grep -q 'STRATSED V2.0c' '$TMP/ecran1.txt' && grep -q 'TELEASS V1.0a' '$TMP/ecran1.txt'"
check "TELESTRA.CFG écrit" "grep -qx 'a=STRATSED.DSK' '$TMP/cfg1' && grep -qx 'bank5=teleass.rom' '$TMP/cfg1'"
check "TELESTRA.CFG relu au démarrage : TELE-ASS en banques 2 et 5" "[ \$(grep -c 'TELEASS V1.0a' '$TMP/ecran2.txt') -eq 2 ]"
check "SAVE réécrit dans le fichier de la clé" "grep -q 'MENUOK' '$TMP/cle/STRATSED.DSK'"
check "menu : imprimante coupée, enregistrée" "grep -q 'Imprimante coupée' '$TMP/prn_msg.txt' && grep -qx 'impression=non' '$TMP/cfg_prn' && grep -qx 'modem=oui' '$TMP/cfg_prn'"
check "impression=non : LPRINT n'écrit rien" "! grep -q IMPRIME '$TMP/lp1.txt'"
check "impression=oui : LPRINT écrit" "grep -q IMPRIME '$TMP/lp2.txt'"
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
