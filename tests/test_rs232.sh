#!/bin/sh
# test_rs232.sh — prise RS232 (PA4 = 1) en liaison directe avec HYPER-BASIC.
# Le correspondant tests/rs232_peer.py écoute ; le banc s'y relie (-S connect:).
#   1. SOUT 65:SOUT 66, puis SSAVE d'un bloc mémoire (« ABCDEFGHIJKLMNOP ») :
#      octets, en-tête (50 x $16, $24, nom, adresses) et données reçus ;
#   2. SLOAD du même fichier (envoyé par le correspondant au « ! » de SOUT 33) :
#      nom affiché, mémoire restituée ;
#   3. CONSOLE : « HELLO » tapé part vers le correspondant, sa réponse
#      « SALUT » s'affiche ;
#   4. mode écoute (-S listen:0, port annoncé par le banc, temps réel -R) :
#      deux appels successifs ; le premier raccroche, le banc libère la ligne
#      et accepte le second (tests/rs232_client.py).
set -u
. "$(dirname "$0")/lib.sh"
BIN=${1:-build/telestrat_headless}
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
if [ ! -f "$DSK" ] || ! command -v python3 >/dev/null; then
    skip "disquette ou python3 absent"
fi
tmp_init
cp "$DSK" "$TMP/a.dsk"
PEER="$(dirname "$0")/rs232_peer.py"

# session NOM TEXTE [FICHIER] : correspondant à l'écoute, puis le banc s'y relie
session() {
    python3 "$PEER" 0 "$TMP/$1.bin" 60 ${3:-} > /dev/null 2>&1 &
    pid=$!
    pid_add $pid
    port=$(port_attend "$TMP/$1.bin.pret") || { echo "ÉCHEC [rs232] : le correspondant n'est pas à l'écoute"; return 1; }
    banc -c standard -0 "$TMP/a.dsk" -S "connect:127.0.0.1:$port" -k 8 -f 4000 -w 500 -t "$2" -s > "$TMP/$1.txt"
    kill $pid 2> /dev/null
    wait $pid 2> /dev/null
}

session envoi '1~~~~~~SOUT 65:SOUT 66\nFOR I=0 TO 15:POKE #9000+I,65+I:NEXT\nSSAVE "ESSAI",A#9000,E#9010\n~~~~~~~~~~'
# Fichier seul (sans « AB ») pour la relecture
python3 -c "import sys; d=open('$TMP/envoi.bin','rb').read(); open('$TMP/fichier.bin','wb').write(d[d.index(b'\x16'):])"
session relu '1~~~~~~SOUT 33:SLOAD\n~~~~~~~~~~FOR I=0 TO 15:PRINT CHR$(PEEK(#9000+I));:NEXT\n~~~~' "$TMP/fichier.bin"
session console '1~~~~~~CONSOLE\n~~~~HELLO~~~~~'

# 4. Mode écoute : le banc attend les clients ; -R cadence les trames sur le temps
# réel, de sorte que les deux « HELLO » tapés (à ≈ 15 s d'écart) encadrent
# sûrement le raccrochage et la reconnexion du correspondant
W=$(printf '~%.0s' $(seq 1 40))
banc -c standard -0 "$TMP/a.dsk" -S "listen:0" -R -k 8 -f 1150 -w 500 \
    -t "1~~~~~~CONSOLE\n~~~~HELLO${W}HELLO~~~~~~~~~~" -s > "$TMP/ecoute.txt" 2> "$TMP/ecoute_err.txt" &
BPID=$!
pid_add $BPID
if LPORT=$(port_attend "$TMP/ecoute_err.txt"); then
    run python3 "$(dirname "$0")/rs232_client.py" "$LPORT" "$TMP/ecoute_client.txt" 40
else
    echo "ÉCHEC [rs232] : le banc n'annonce pas son port d'écoute"
fi
wait $BPID

fail=0
n=0
check() {
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [rs232] : $1"
    fi
}
has() { python3 -c "import sys; sys.exit(0 if $2 in open('$TMP/$1','rb').read() else 1)"; }
check "SOUT 65:SOUT 66 : « AB » reçu" "$(has envoi.bin "b'AB\x16'" && echo true || echo false)"
check "SSAVE : synchro (50 x \$16) et \$24" "$(has envoi.bin "b'\x16' * 50 + b'\$ESSAI'" && echo true || echo false)"
check "SSAVE : données du bloc" "$(has envoi.bin "b'ABCDEFGHIJKLMNOP'" && echo true || echo false)"
check "SLOAD : nom du fichier affiché" "grep -q 'ESSAI    COM' '$TMP/relu.txt'"
check "SLOAD : mémoire restituée" "grep -q '^ ABCDEFGHIJKLMNOP' '$TMP/relu.txt'"
check "CONSOLE : touches tapées envoyées" "$(has console.bin "b'HELLO'" && echo true || echo false)"
check "CONSOLE : réponse du correspondant affichée" "grep -q 'SALUT' '$TMP/console.txt'"
check "écoute : premier appel servi (HELLO reçu par le correspondant)" "grep -q '^appel 1 : .*HELLO' '$TMP/ecoute_client.txt'"
check "écoute : second appel servi après le raccrochage du premier" "grep -q '^appel 2 : .*HELLO' '$TMP/ecoute_client.txt'"
check "écoute : réponses des deux appels affichées" "grep -q 'SALUT1' '$TMP/ecoute.txt' && grep -q 'SALUT2' '$TMP/ecoute.txt'"
if [ "$fail" -ne 0 ]; then
    echo "--- écoute :"; cat "$TMP/ecoute_client.txt" "$TMP/ecoute_err.txt" 2> /dev/null; grep -v '^$' "$TMP/ecoute.txt" | tail -5
    for f in envoi relu console; do echo "--- $f :"; grep -v '^$' "$TMP/$f.txt" | tail -5; od -c "$TMP/$f.bin" | head -3; done
fi
banc_bilan
echo "test_rs232 : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
