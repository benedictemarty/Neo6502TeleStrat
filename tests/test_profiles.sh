#!/bin/sh
# test_profiles.sh — choix des ROM au démarrage (TELESTRA.CFG « demarrage= »).
#   1. demarrage=choix : page « Démarrer sur… » à l'ouverture du menu ;
#      STRATORIC choisie : STRATORIC V4.0 démarre ; Échap : configuration de
#      la clé (Telestrat) ;
#   2. demarrage=atmos : ORIC BASIC 1.1 sans passer par le menu ;
#   3. demarrage=orix : ORIX 1.0 arrive au shell ; sans CH376, les commandes
#      répondent « Usb drive controller not found ! » (à changer quand le CH376
#      sera émulé).
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
printf 'demarrage=orix\n' > "$TMP/c3/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/c3" -f 900 -w 400 -k 8 -t 'help\n' -s > "$TMP/orix.txt" 2>&1

check "page de démarrage : titre et profils" "grep -q 'Démarrer sur' '$TMP/page.txt' && grep -q 'Configuration de la clé' '$TMP/page.txt' && grep -q 'ORIX 1.0' '$TMP/page.txt'"
check "choix STRATORIC : STRATORIC V4.0" "grep -q 'Démarrage : STRATORIC' '$TMP/strat_msg.txt' && grep -q 'STRATORIC V4.0' '$TMP/strat.txt'"
check "Échap : Telestrat de la clé" "grep -q 'TELESTRAT' '$TMP/echap.txt' && ! grep -q STRATORIC '$TMP/echap.txt'"
check "demarrage=atmos : BASIC 1.1" "grep -q 'ORIC EXTENDED BASIC V1.1' '$TMP/atmos.txt'"
check "demarrage=orix : shell d'ORIX, CH376 absent" "grep -q '^#' '$TMP/orix.txt' && grep -q 'Usb drive controller not found' '$TMP/orix.txt'"
rm -rf "$TMP"
echo "test_profiles : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
