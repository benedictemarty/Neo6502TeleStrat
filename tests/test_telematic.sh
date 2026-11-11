#!/bin/sh
# test_telematic.sh — serveur TELEMATIC de bout en bout (sprint 3).
# Le Telestrat (config. standard, TELEMATIC en banque 3) démarre sur
# STRATSED.DSK, charge l'arborescence DEMO.SRV et lance le serveur ; un
# correspondant (tests/minitel_client.py) appelle la ligne TCP du banc :
# sonnerie, CONNEXION, page d'accueil, touches, raccrochage.
set -u
. "$(dirname "$0")/lib.sh"
BIN=${1:-build/telestrat_headless}
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
if [ ! -f "$DSK" ] || ! command -v python3 >/dev/null; then
    skip "disquette ou python3 absent"
fi
tmp_init
cp "$DSK" "$TMP/a.dsk"
CL=$(printf '\014')
E=$(printf '\033')
# Port choisi par le banc (listen:0) et annoncé sur stderr : aucune fenêtre entre
# le choix et l'ouverture
banc -c standard -0 "$TMP/a.dsk" -L "listen:0" -f 15000 -w 500 \
    -t "1~~~~~~APLIC 4\n~~~~\n~~~~NDEMO$CL~~~~$E~~2\n" -T "$TMP/serie.txt" -s > "$TMP/ecran.txt" 2> "$TMP/ligne.txt" &
PID=$!
pid_add $PID
PORT=$(port_attend "$TMP/ligne.txt") || { echo "ÉCHEC [telematic] : le banc n'annonce pas son port"; cat "$TMP/ligne.txt"; exit 1; }
# ENVOI (page d'accueil reçue), puis Connexion/Fin (SEP $49) une fois la réponse
# du serveur reçue : TELEMATIC raccroche aussitôt (XDECON, $ECB7) ; puis le client
# coupe la ligne. Les touches partent sur contenu reçu, pas sur un silence mesuré
run python3 "$(dirname "$0")/minitel_client.py" "$PORT" "$TMP/recu.bin" 60 1.0 \
    '\E?SERVEUR REALISE ENTIEREMENT' '\F?taper quelque chose avant ENVOI' '\X' > /dev/null
wait $PID
awk '{print $2 $3}' "$TMP/serie.txt" | tr '\n' ' ' > "$TMP/seq.txt"
awk '$2=="TX"{print $3}' "$TMP/serie.txt" | tr '\n' ' ' > "$TMP/tx.txt"
awk '$2=="RX"{print $3}' "$TMP/serie.txt" | tr '\n' ' ' > "$TMP/rx.txt"

fail=0
n=0
check() {  # description, commande
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [telematic] : $1"
    fi
}
check "XLIGNE : ESC 9 o puis ESC 9 h vers le Minitel" "grep -q '1B 39 6F 1B 39 68' '$TMP/tx.txt'"
check "Minitel (STUM 1B) : SEP \$50, SEP \$59 puis SEP \$53 à la connexion" "grep -q '^13 50 13 59 13 53 ' '$TMP/rx.txt'"
check "page d'accueil de DEMO reçue" "grep -q 'SERVEUR REALISE ENTIEREMENT' '$TMP/recu.bin'"
check "le serveur réagit à ENVOI" "grep -q 'taper quelque chose avant ENVOI' '$TMP/recu.bin'"
check "Connexion/Fin du correspondant : TELEMATIC raccroche (ESC 9 g)" "grep -q '1B 39 67' '$TMP/tx.txt'"
check "raccrochage : SEP \$59 puis SEP \$53" "grep -q '13 59 13 53 \$' '$TMP/rx.txt'"
check "retour en attente d'appel" "grep -q 'Attente de communication' '$TMP/ecran.txt'"
if [ "$fail" -ne 0 ]; then
    echo "--- trace série :"; head -c 400 "$TMP/seq.txt"; echo
    echo "--- écran :"; grep -v '^$' "$TMP/ecran.txt"
fi
cp "$TMP/recu.bin" "${KEEP_RECU:-/dev/null}" 2>/dev/null; cp "$TMP/serie.txt" "${KEEP_SERIE:-/dev/null}" 2>/dev/null
banc_bilan
echo "test_telematic : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
