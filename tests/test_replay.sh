#!/bin/sh
# test_replay.sh — le système optimisé (systems/telestrat.h) rejoue à
# l'identique des traces du modèle de référence (systems/telestrat_ref.h) :
# données du bus, ligne IRQ, échantillons audio, octets série, image.
set -u
. "$(dirname "$0")/lib.sh"
REF=${1:-build/telestrat_headless_ref}
REPLAY=${2:-build/replay}
HL=${3:-}  # système optimisé (telestrat_headless) : démarrage à froid comparé (-Q)
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
if [ ! -f "$DSK" ]; then
    skip "disquette absente ($DSK)"
fi
tmp_init
fail=0
n=0

rejoue() {  # nom config (la trace est déjà enregistrée dans $TMP/nom)
    n=$((n + 1))
    cp "$DSK" "$TMP/r.dsk"
    if ! "$REPLAY" "$TMP/$1" "$2" "$TMP/r.dsk" > "$TMP/$1.out"; then
        fail=$((fail + 1))
        echo "ÉCHEC [replay $1] : $(cat "$TMP/$1.out")"
    fi
}

# A : disquette, HYPER-BASIC, sons, DIR (configuration Oricutron)
cp "$DSK" "$TMP/r.dsk"
run "$REF" -c oricutron -0 "$TMP/r.dsk" -f 1000 -w 500 -t '1~~~~~~PING\n~~ZAP\n~~DIR\n' -B "$TMP/a" > /dev/null
rejoue a oricutron

# B : serveur TELEMATIC appelé par un correspondant (liaison série, sonnerie)
if command -v python3 > /dev/null; then
    CL=$(printf '\014')
    E=$(printf '\033')
    cp "$DSK" "$TMP/r.dsk"
    run "$REF" -c standard -0 "$TMP/r.dsk" -L "listen:0" -f 3000 -w 500 \
        -t "1~~~~~~APLIC 4\n~~~~\n~~~~NDEMO$CL~~~~$E~~2\n" -B "$TMP/b" > /dev/null 2> "$TMP/ligne_b.txt" &
    PID=$!
    pid_add $PID
    PORT=$(port_attend "$TMP/ligne_b.txt") || { echo "ÉCHEC [replay b] : le banc n'annonce pas son port"; exit 1; }
    run python3 "$(dirname "$0")/minitel_client.py" "$PORT" /dev/null 20 0.3 '\E?SERVEUR REALISE ENTIEREMENT' '\X' > /dev/null
    wait $PID
    if [ ! -s "$TMP/b.ser" ]; then
        fail=$((fail + 1))
        echo "ÉCHEC [replay b] : pas d'octets série enregistrés"
    fi
    rejoue b standard
fi

# C : démarrage à froid en cours de route (-Q, RESET du menu), variante RAM 64 Ko :
# HYPER-BASIC y exécute la banque 5 vide (bus flottant, docs/ARCHITECTURE.md),
# chemin chaotique qui ne tombe juste que si les deux modèles sont identiques
if [ -n "$HL" ]; then
    n=$((n + 1))
    cp "$DSK" "$TMP/q.dsk"
    run "$REF" -c ram64k -0 "$TMP/q.dsk" -Q 300 -f 700 -s > "$TMP/q_ref.txt" 2>&1
    cp "$DSK" "$TMP/q.dsk"
    run "$HL" -c ram64k -0 "$TMP/q.dsk" -Q 300 -f 700 -s > "$TMP/q_opt.txt" 2>&1
    if cmp -s "$TMP/q_ref.txt" "$TMP/q_opt.txt" && [ -s "$TMP/q_ref.txt" ]; then
        echo "démarrage à froid (-Q) : écrans identiques" > "$TMP/q.out"
    else
        fail=$((fail + 1))
        echo "ÉCHEC [démarrage à froid] : écrans différents entre référence et système optimisé"
    fi
fi

for f in "$TMP"/*.out; do [ -f "$f" ] && sed 's/^/  /' "$f"; done
banc_bilan
echo "test_replay : $((n - fail))/$n traces rejouées à l'identique"
[ "$fail" -eq 0 ]
