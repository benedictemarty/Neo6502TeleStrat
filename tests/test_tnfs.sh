#!/bin/sh
# test_tnfs.sh — volume Réseau du banc (-N) contre le serveur TNFS de
# référence tnfsd (Spectranet, licence MIT), servant un répertoire temporaire :
#   1. TELESTRA.CFG a=net:/STRATSED.DSK : disquette du réseau lue piste par
#      piste, DIR de STRATSED ;
#   2. SAVE sur cette disquette : écrit dans le fichier du répertoire de
#      tnfsd, relu par un DIR dans une nouvelle session ;
#   3. menu : lecteur A, source Réseau, STRATSED.DSK ; Enregistrer écrit
#      « a=net:/STRATSED.DSK » ; le choix de la source n'apparaît qu'avec -N,
#      le sélecteur du réseau montre les noms sans préfixe ;
#   4. instantané : son texte garde le préfixe net:/ de la disquette ;
#   5. cassette du réseau par le menu (source Réseau), CLOAD, RUN.
#
# tnfsd écoute toujours le port 16384 (config.h). Port libre : ce binaire ;
# sinon (autre serveur sur la machine) une copie de ses sources compilée dans
# le répertoire temporaire sur un port libre. Ignoré sans tnfsd ni sources.
#   TELESTRAT_TNFSD   binaire (défaut ~/spectranet/tnfs/tnfsd/bin/tnfsd)
#   TELESTRAT_TNFSD_SRC sources (défaut : le répertoire parent du binaire)
set -u
BIN=${1:-build/telestrat_headless}
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
TNFSD=${TELESTRAT_TNFSD:-$HOME/spectranet/tnfs/tnfsd/bin/tnfsd}
SRC=${TELESTRAT_TNFSD_SRC:-$(dirname "$TNFSD")/..}
if [ ! -f "$DSK" ] || ! command -v python3 > /dev/null; then
    echo "test_tnfs : disquette ou python3 absent, ignoré"
    exit 0
fi
TMP=$(mktemp -d)
PID=
trap '[ -n "$PID" ] && kill "$PID" 2> /dev/null' EXIT

# Port UDP et TCP libre (tnfsd ouvre les deux)
port_free() {
    python3 -c "
import socket, sys
for t in (socket.SOCK_DGRAM, socket.SOCK_STREAM):
    s = socket.socket(socket.AF_INET, t)
    try:
        s.bind(('127.0.0.1', int(sys.argv[1])))
    except OSError:
        sys.exit(1)
    finally:
        s.close()" "$1"
}
PORT=16384
SERVER=
if [ -x "$TNFSD" ] && port_free $PORT; then
    SERVER=$TNFSD
elif [ -f "$SRC/config.h" ] && [ -f "$SRC/main.c" ] && command -v cc > /dev/null; then
    PORT=$(python3 -c "
import socket
for p in range(20000, 30000, 7):
    ok = True
    for t in (socket.SOCK_DGRAM, socket.SOCK_STREAM):
        s = socket.socket(socket.AF_INET, t)
        try:
            s.bind(('127.0.0.1', p))
        except OSError:
            ok = False
        s.close()
    if ok:
        print(p)
        break")
    mkdir "$TMP/src"
    cp "$SRC"/*.c "$SRC"/*.h "$TMP/src/"
    sed -i "s/^#define TNFSD_PORT.*/#define TNFSD_PORT $PORT/" "$TMP/src/config.h"
    # Options de son Makefile (OS=LINUX), sans optimisation : avec
    # _FORTIFY_SOURCE (-O1 et plus), tnfsd s'arrête sur « buffer overflow »
    if cc -w -O0 -DUNIX -DNEED_BSDCOMPAT -DENABLE_CHROOT -DNEED_ERRTABLE -o "$TMP/tnfsd" "$TMP"/src/*.c 2> "$TMP/cc.log"; then
        SERVER=$TMP/tnfsd
    fi
fi
if [ -z "$SERVER" ]; then
    echo "test_tnfs : tnfsd absent ou port 16384 occupé sans sources pour un autre port, ignoré"
    exit 0
fi

mkdir "$TMP/tnfs" "$TMP/cle"
cp "$DSK" "$TMP/tnfs/STRATSED.DSK"
"$SERVER" "$TMP/tnfs" > "$TMP/tnfsd.log" 2>&1 &
PID=$!
i=0
while port_free $PORT && [ $i -lt 30 ]; do
    sleep 0.1
    i=$((i + 1))
done
if port_free $PORT; then
    echo "test_tnfs : tnfsd n'écoute pas sur le port $PORT (voir $TMP/tnfsd.log), ignoré"
    exit 0
fi
NET="127.0.0.1:$PORT"

# 1. Disquette du réseau désignée par TELESTRA.CFG, DIR
echo "a=net:/STRATSED.DSK" > "$TMP/cle/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/cle" -N "$NET" -f 1500 -w 1200 -k 8 -t '1~~~~~~DIR\n' -s > "$TMP/dir1.txt" 2> "$TMP/dir1_msg.txt"
# 2. SAVE sur la disquette du réseau, puis DIR dans une nouvelle session
"$BIN" -c standard -U "$TMP/cle" -N "$NET" -f 1800 -w 1200 -k 8 -t '1~~~~~~10 PRINT 4242\nSAVE "TNFSOK"\n~~~~~~~~~~' -s \
    > /dev/null 2>&1
"$BIN" -c standard -U "$TMP/cle" -N "$NET" -f 1500 -w 1200 -k 8 -t '1~~~~~~DIR\n' -s > "$TMP/dir2.txt" 2>&1
# 3. Menu : h e = lecteur A, source ; d e = Réseau ; S e = STRATSED.DSK ;
# z u e = Enregistrer. Images du menu : choix de la source (curseur sur celle
# de l'image en place : z = Réseau), sélecteur du réseau ; sans -N : le sélecteur de la clé directement (aucune image)
rm "$TMP/cle/TELESTRA.CFG"
"$BIN" -c standard -U "$TMP/cle" -N "$NET" -M "5:hedeSezue" -f 10 > /dev/null 2> "$TMP/menu_msg.txt"
cp "$TMP/cle/TELESTRA.CFG" "$TMP/cfg_menu"
"$BIN" -c standard -U "$TMP/cle" -N "$NET" -M "5:he" -O "$TMP/source.txt" -f 10 > /dev/null 2>&1
"$BIN" -c standard -U "$TMP/cle" -N "$NET" -M "5:heze" -O "$TMP/reseau.txt" -f 10 > /dev/null 2>&1
"$BIN" -c standard -U "$TMP/cle" -M "5:he" -O "$TMP/sans.txt" -f 10 > /dev/null 2>&1
# 4. Instantané avec la disquette du réseau (TELESTRA.CFG du menu)
"$BIN" -c standard -U "$TMP/cle" -N "$NET" -f 300 -X "299:$TMP/etat.sta" > /dev/null 2>&1
# 5. Cassette du réseau par le menu (programme tapé, relevé, mis en cassette
# par tools/mktap.py) : h r e e d d e = banque 7, Clé USB (ROM intégrées),
# ROM Atmos ; l d d d d e d e d e = cassette, Réseau, ESSAI.TAP ; z u u e = RESET
rm "$TMP/cle/TELESTRA.CFG"
W=$(printf '~%.0s' $(seq 1 120))
"$BIN" -c atmos -f 700 -w 150 -k 8 -t '10 PRINT "RESEAU OK"\n20 PRINT 6*7\n' -r "$TMP/ram.bin" > /dev/null
python3 "$(dirname "$0")/../tools/mktap.py" "$TMP/ram.bin" "$TMP/tnfs/ESSAI.TAP" ESSAI > /dev/null
"$BIN" -c standard -U "$TMP/cle" -N "$NET" -M "300:hreeddelddddededezuue" -f 1700 -w 450 -k 8 \
    -t "CLOAD\"\"\n${W}RUN\n" -s > "$TMP/cload.txt" 2> "$TMP/cload_msg.txt"

fail=0
n=0
check() {
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [tnfs] : $1"
    fi
}
check "volume Réseau monté (-N)" "grep -q 'réseau : $NET monté' '$TMP/dir1_msg.txt'"
check "TELESTRA.CFG a=net:/ : DIR de la disquette du réseau" "grep -q 'secteurs libres' '$TMP/dir1.txt' && grep -q 'INI800K' '$TMP/dir1.txt'"
check "SAVE écrit dans le fichier de tnfsd" "grep -q 'TNFSOK' '$TMP/tnfs/STRATSED.DSK'"
check "nouvelle session : DIR voit le fichier enregistré" "grep -q 'TNFSOK' '$TMP/dir2.txt'"
check "menu : source Réseau, STRATSED.DSK dans A" "grep -q 'Lecteur A : net:/STRATSED.DSK' '$TMP/menu_msg.txt'"
check "menu : Enregistrer écrit a=net:/STRATSED.DSK" "grep -qx 'a=net:/STRATSED.DSK' '$TMP/cfg_menu'"
check "menu : choix de la source avec -N" "grep -q 'Disquette pour le lecteur A . source ?' '$TMP/source.txt' && grep -q 'Clé USB' '$TMP/source.txt' && grep -q 'Réseau  *$NET' '$TMP/source.txt'"
check "menu : sélecteur du réseau, noms sans préfixe" "grep -q 'Disquette pour le lecteur A . réseau' '$TMP/reseau.txt' && grep 'STRATSED.DSK' '$TMP/reseau.txt' | grep -qv 'net:/'"
check "menu : sans -N, sélecteur de la clé directement" "grep -q 'Aucune image .dsk sur la clé' '$TMP/sans.txt' && ! grep -q 'source ?' '$TMP/sans.txt'"
check "instantané : a=net:/STRATSED.DSK dans son texte" "grep -aq 'a=net:/STRATSED.DSK' '$TMP/etat.sta'"
check "menu : cassette du réseau insérée" "grep -q 'Cassette : net:/ESSAI.TAP' '$TMP/cload_msg.txt'"
check "cassette du réseau : CLOAD puis RUN" "grep -q '^  RESEAU OK' '$TMP/cload.txt' && grep -q '^   42' '$TMP/cload.txt'"
echo "test_tnfs : $((n - fail))/$n vérifications réussies"
[ $fail -eq 0 ] || { echo "test_tnfs : fichiers dans $TMP"; exit 1; }
