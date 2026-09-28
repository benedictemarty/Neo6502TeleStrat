#!/bin/sh
# test_telematic.sh — serveur TELEMATIC de bout en bout (sprint 3).
# Le Telestrat (config. standard, TELEMATIC en banque 3) démarre sur
# STRATSED.DSK, charge l'arborescence DEMO.SRV et lance le serveur ; un
# correspondant (tests/minitel_client.py) appelle la ligne TCP du banc :
# sonnerie, CONNEXION, page d'accueil, touches, raccrochage.
set -u
BIN=${1:-build/telestrat_headless}
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
if [ ! -f "$DSK" ] || ! command -v python3 >/dev/null; then
    echo "test_telematic : disquette ou python3 absent, ignoré"
    exit 0
fi
TMP=$(mktemp -d)
cp "$DSK" "$TMP/a.dsk"
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
CL=$(printf '\014')
E=$(printf '\033')
"$BIN" -c standard -0 "$TMP/a.dsk" -L "listen:$PORT" -f 15000 -w 500 \
    -t "1~~~~~~APLIC 4\n~~~~\n~~~~NDEMO$CL~~~~$E~~2\n" -T "$TMP/serie.txt" -s > "$TMP/ecran.txt" &
PID=$!
python3 "$(dirname "$0")/minitel_client.py" "$PORT" "$TMP/recu.bin" 60 1.0 '\E' '\X' > /dev/null
wait $PID
awk '{print $2 $3}' "$TMP/serie.txt" | tr '\n' ' ' > "$TMP/seq.txt"

fail=0
n=0
check() {  # description, commande
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [telematic] : $1"
    fi
}
check "XLIGNE : ESC 9 o puis ESC 9 h vers le Minitel" "grep -q 'TX1B TX39 TX6F TX1B TX39 TX68' '$TMP/seq.txt'"
check "réponse du Minitel : connexion \$13 \$53" "grep -q 'RX13 RX53' '$TMP/seq.txt'"
check "page d'accueil de DEMO reçue" "grep -q 'SERVEUR REALISE ENTIEREMENT' '$TMP/recu.bin'"
check "le serveur réagit à ENVOI" "grep -q 'taper quelque chose avant ENVOI' '$TMP/recu.bin'"
check "raccrochage du correspondant signalé (\$13 \$54)" "grep -q 'RX13 RX54' '$TMP/seq.txt'"
check "retour en attente d'appel" "grep -q 'Attente de communication' '$TMP/ecran.txt'"
if [ "$fail" -ne 0 ]; then
    echo "--- trace série :"; head -c 400 "$TMP/seq.txt"; echo
    echo "--- écran :"; grep -v '^$' "$TMP/ecran.txt"
fi
cp "$TMP/recu.bin" "${KEEP_RECU:-/dev/null}" 2>/dev/null; cp "$TMP/serie.txt" "${KEEP_SERIE:-/dev/null}" 2>/dev/null; rm -rf "$TMP"
echo "test_telematic : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
