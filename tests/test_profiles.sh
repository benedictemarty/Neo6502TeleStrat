#!/bin/sh
# test_profiles.sh — choix des ROM au démarrage (TELESTRA.CFG « demarrage= »).
#   1. demarrage=choix : page « Démarrer sur… » à l'ouverture du menu ;
#      STRATORIC choisie : STRATORIC V4.0 démarre ; Échap : configuration de
#      la clé (Telestrat) ;
#   2. demarrage=atmos : ORIC BASIC 1.1 sans passer par le menu ;
#   3. demarrage=stratoric : STRATORIC sans passer par le menu ; profil inconnu
#      (demarrage=orix, retiré) : configuration de la clé.
set -u
BIN=${1:-build/telestrat_headless}
TMP=$(mktemp -d)
fail=0
n=0
check() {
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [démarrage] : $1"
    fi
}
mkdir "$TMP/c1" "$TMP/c2" "$TMP/c3"
printf 'demarrage=choix\n' > "$TMP/c1/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/c1" -M "5:" -O "$TMP/page.txt" -f 8 > /dev/null 2>&1
"$BIN" -c standard -U "$TMP/c1" -M "5:dde" -f 400 -s > "$TMP/strat.txt" 2> "$TMP/strat_msg.txt"
"$BIN" -c standard -U "$TMP/c1" -M "5:x" -f 300 -s > "$TMP/echap.txt" 2>&1
printf 'demarrage=atmos\n' > "$TMP/c2/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/c2" -f 300 -s > "$TMP/atmos.txt" 2>&1
printf 'demarrage=stratoric\n' > "$TMP/c3/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/c3" -f 400 -s > "$TMP/strat2.txt" 2>&1
mkdir "$TMP/c4"
printf 'demarrage=orix\n' > "$TMP/c4/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/c4" -f 300 -s > "$TMP/inconnu.txt" 2>&1

check "page de démarrage : titre et profils" "grep -q 'Démarrer sur' '$TMP/page.txt' && grep -q 'Configuration de la clé' '$TMP/page.txt' && grep -q 'ORIC BASIC 1.1' '$TMP/page.txt' && ! grep -q ORIX '$TMP/page.txt'"
check "choix STRATORIC : STRATORIC V4.0" "grep -q 'Démarrage : STRATORIC' '$TMP/strat_msg.txt' && grep -q 'STRATORIC V4.0' '$TMP/strat.txt'"
check "Échap : Telestrat de la clé" "grep -q 'TELESTRAT' '$TMP/echap.txt' && ! grep -q STRATORIC '$TMP/echap.txt'"
check "demarrage=atmos : BASIC 1.1" "grep -q 'ORIC EXTENDED BASIC V1.1' '$TMP/atmos.txt'"
check "demarrage=stratoric : STRATORIC V4.0" "grep -q 'STRATORIC V4.0' '$TMP/strat2.txt'"
check "profil inconnu : Telestrat de la clé" "grep -q 'TELESTRAT' '$TMP/inconnu.txt' && ! grep -q STRATORIC '$TMP/inconnu.txt'"
rm -rf "$TMP"
echo "test_profiles : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
