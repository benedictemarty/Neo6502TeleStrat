#!/bin/sh
# test_minitel_emul.sh — TELEMATIC en émulation Minitel (APLIC 1), appel sortant.
# Le Telestrat appelle un serveur de test (tests/minitel_server.py) par la
# ligne du banc (-L connect:...) : FUNCT+D (XLIGNE : OPPO, CONNEXION), page du
# serveur affichée, « BONJOUR » + ENVOI (RETURN) reçus par le serveur, sa
# réponse affichée (écran HIRES lu par tests/hires_texte.py).
set -u
. "$(dirname "$0")/lib.sh"
BIN=${1:-build/telestrat_headless}
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
if [ ! -f "$DSK" ] || ! command -v python3 >/dev/null; then
    skip "disquette ou python3 absent"
fi
tmp_init
cp "$DSK" "$TMP/a.dsk"
W=$(printf '~%.0s' $(seq 1 80))   # attente de la connexion (négociation du modem)
# Le serveur choisit son port (0) et l'annonce dans recu.bin.pret une fois à l'écoute
python3 "$(dirname "$0")/minitel_server.py" 0 "$TMP/recu.bin" 30 > /dev/null 2>&1 &
SPID=$!
pid_add $SPID
PORT=$(port_attend "$TMP/recu.bin.pret") || { echo "ÉCHEC [minitel] : le serveur n'est pas à l'écoute"; exit 1; }
banc -c standard -0 "$TMP/a.dsk" -L "connect:127.0.0.1:$PORT" -f 4000 -w 500 \
    -t "1~~~~~~APLIC 1\n~~~~2\n~~~~~~\fD${W}BONJOUR\n~~~~~~~~~~" -T "$TMP/serie.txt" -r "$TMP/ram.bin" > /dev/null
kill $SPID 2> /dev/null
wait $SPID 2> /dev/null
# Écran HIRES de l'émulation : lu d'après le jeu de caractères
python3 "$(dirname "$0")/hires_texte.py" "$TMP/ram.bin" > "$TMP/ecran.txt"
awk '$2=="TX"{print $3}' "$TMP/serie.txt" | tr '\n' ' ' > "$TMP/tx.txt"

fail=0
n=0
check() {
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [minitel] : $1"
    fi
}
check "FUNCT+D : ESC 9 o puis ESC 9 h vers le Minitel" "grep -q '1B 39 6F 1B 39 68' '$TMP/tx.txt'"
check "page du serveur affichée" "grep -q 'SERVEUR DE TEST NEO6502TELESTRAT' '$TMP/ecran.txt'"
check "BONJOUR puis ENVOI reçus par le serveur" "python3 -c \"import sys; sys.exit(0 if b'BONJOUR\\\\x13A' in open('$TMP/recu.bin','rb').read() else 1)\""
check "réponse du serveur affichée" "grep -q 'RECU: BONJOUR' '$TMP/ecran.txt'"
if [ "$fail" -ne 0 ]; then
    echo "--- écran :"; grep -v '^$' "$TMP/ecran.txt"
    echo "--- émis :"; head -c 300 "$TMP/tx.txt"; echo
fi
banc_bilan
echo "test_minitel_emul : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
