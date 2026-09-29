#!/bin/sh
# test_rs232.sh — prise RS232 (PA4 = 1) en liaison directe avec HYPER-BASIC.
# Le correspondant tests/rs232_peer.py écoute ; le banc s'y relie (-S connect:).
#   1. SOUT 65:SOUT 66, puis SSAVE d'un bloc mémoire (« ABCDEFGHIJKLMNOP ») :
#      octets, en-tête (50 x $16, $24, nom, adresses) et données reçus ;
#   2. SLOAD du même fichier (envoyé par le correspondant au « ! » de SOUT 33) :
#      nom affiché, mémoire restituée ;
#   3. CONSOLE : « HELLO » tapé part vers le correspondant, sa réponse
#      « SALUT » s'affiche.
set -u
BIN=${1:-build/telestrat_headless}
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
if [ ! -f "$DSK" ] || ! command -v python3 >/dev/null; then
    echo "test_rs232 : disquette ou python3 absent, ignoré"
    exit 0
fi
TMP=$(mktemp -d)
cp "$DSK" "$TMP/a.dsk"
PEER="$(dirname "$0")/rs232_peer.py"

# session NOM TEXTE [FICHIER] : correspondant à l'écoute, puis le banc s'y relie
session() {
    port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
    python3 "$PEER" "$port" "$TMP/$1.bin" 60 ${3:-} > /dev/null 2>&1 &
    pid=$!
    i=0
    while [ ! -f "$TMP/$1.bin.pret" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
    "$BIN" -c standard -0 "$TMP/a.dsk" -S "connect:127.0.0.1:$port" -k 8 -f 4000 -w 500 -t "$2" -s > "$TMP/$1.txt"
    kill $pid 2> /dev/null
    wait $pid 2> /dev/null
}

session envoi '1~~~~~~SOUT 65:SOUT 66\nFOR I=0 TO 15:POKE #9000+I,65+I:NEXT\nSSAVE "ESSAI",A#9000,E#9010\n~~~~~~~~~~'
# Fichier seul (sans « AB ») pour la relecture
python3 -c "import sys; d=open('$TMP/envoi.bin','rb').read(); open('$TMP/fichier.bin','wb').write(d[d.index(b'\x16'):])"
session relu '1~~~~~~SOUT 33:SLOAD\n~~~~~~~~~~FOR I=0 TO 15:PRINT CHR$(PEEK(#9000+I));:NEXT\n~~~~' "$TMP/fichier.bin"
session console '1~~~~~~CONSOLE\n~~~~HELLO~~~~~'

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
if [ "$fail" -ne 0 ]; then
    for f in envoi relu console; do echo "--- $f :"; grep -v '^$' "$TMP/$f.txt" | tail -5; od -c "$TMP/$f.bin" | head -3; done
fi
rm -rf "$TMP"
echo "test_rs232 : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
