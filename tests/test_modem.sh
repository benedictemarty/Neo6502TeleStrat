#!/bin/sh
# test_modem.sh — chemin modem du firmware en intégration : hayes_line.h,
# modem_mux.h, minitel_port.h et l'ACIA 6551, avec un « faux PicoWiFiModemUSB ».
# Le banc (option -H listen:0) offre les opérations du CDC sur un socket TCP local ;
# tests/modem_peer.py joue le modem. Le comportement du vrai modem n'est pas
# vérifié : le correspondant ne modélise que ce que hayes_line.h attend.
#
# Les échéances sont en trames d'émulation (le banc n'est pas cadencé) ; le banc
# attend le correspondant avant la trame 0 ; le correspondant s'arrête quand le
# banc ferme la liaison. Seul le temps réel des échanges TCP locaux intervient.
#   0. options -H / -d : validation stricte (sortie 2) ;
#   1. initialisation : ATE0V1 puis ATS0=0 dès que le modem est branché ;
#   2. appel entrant : RING, TELEMATIC décroche (ATA), pages Videotex reçues,
#      raccrochage (+++, ATH) ;
#   3. appel sortant Minitel (FUNCT+D) : ATD, CONNECT, données dans les deux sens ;
#   4. échec d'appel (BUSY, NO ANSWER) : le Minitel le sait aussitôt (P-15) ;
#   5. « NO CARRIER » dans une page : la ligne tient (P-14) ; vrai NO CARRIER :
#      données précédentes livrées, puis déconnexion ;
#   6. RESET (PA4 repasse à la RS232) en pleine communication : raccrochage
#      propre (+++, ATH), modem réinitialisé ensuite (P-03) ;
#   7. débit élevé : page de plusieurs Ko envoyée d'un coup, rien de perdu (P-02) ;
#   8. prise RS232 par le modem : CONSOLE de HYPER-BASIC, octets bruts.
set -u
. "$(dirname "$0")/lib.sh"
BIN=${1:-build/telestrat_headless}
HERE=$(dirname "$0")
if ! command -v python3 >/dev/null; then
    skip "python3 absent"
fi
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
tmp_init
fail=0
n=0
check() {  # description, commande
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [modem] : $1"
    fi
}

# Analyse des traces : tests/ n'a pas d'autre outil pour lire « TRAME TX|RX OCTET »
cat > "$TMP/ana.py" <<'PY'
import sys
# ana.py gap SERIE : trames entre la fin de ESC 9 h (TX) et le second SEP $59 (RX) qui suit
# ana.py rx SERIE FICHIER : octets RX remis à l'ACIA après le premier « 13 53 », en binaire
mode, serie = sys.argv[1], sys.argv[2]
lig = [l.split() for l in open(serie) if len(l.split()) == 3 and l.split()[1] in ("TX", "RX")]
if mode == "gap":
    tx = [(int(f), int(b, 16)) for f, d, b in lig if d == "TX"]
    t = None
    for i in range(2, len(tx)):
        if [x[1] for x in tx[i - 2:i + 1]] == [0x1B, 0x39, 0x68]:
            t = tx[i][0]
            break
    rx = [(int(f), int(b, 16)) for f, d, b in lig if d == "RX"]
    s59 = [f for (f, b), (_, b2) in zip(rx, rx[1:]) if b == 0x13 and b2 == 0x59]
    if t is None or len(s59) < 2:
        print(-1)
    else:
        print(s59[1] - t)
else:
    rx = bytes(int(b, 16) for f, d, b in lig if d == "RX")
    i = rx.find(b"\x13\x53")
    open(sys.argv[3], "wb").write(rx[i + 2:] if i >= 0 else b"")
PY

# scene NOM TRAMES TEXTE [options banc...] -- [options correspondant...]
# Le banc tourne TRAMES trames, le correspondant le rejoint ; les fichiers :
#   $TMP/NOM.ser (trace -T), NOM.err (stderr du banc), NOM.bin / NOM.bin.cmds (correspondant),
#   NOM.txt (écran texte), NOM.ram (RAM)
scene() {
    sc_nom=$1; sc_frames=$2; sc_texte=$3; shift 3
    sc_banc=""
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do sc_banc="$sc_banc $1"; shift; done
    [ $# -gt 0 ] && shift
    # shellcheck disable=SC2086
    sc_dsk="-0 $TMP/a.dsk"
    [ -n "${NODSK:-}" ] && sc_dsk=
    # shellcheck disable=SC2086
    "$BIN" -c standard $sc_dsk -H listen:0 -f "$sc_frames" -w 500 -t "$sc_texte" -T "$TMP/$sc_nom.ser" \
        -r "$TMP/$sc_nom.ram" -s $sc_banc > "$TMP/$sc_nom.txt" 2> "$TMP/$sc_nom.err" &
    sc_bpid=$!
    pid_add $sc_bpid
    if sc_port=$(port_attend "$TMP/$sc_nom.err"); then
        python3 "$HERE/modem_peer.py" "$sc_port" "$TMP/$sc_nom.bin" 120 "$@" > /dev/null 2>&1 &
        sc_ppid=$!
        pid_add $sc_ppid
    else
        echo "ÉCHEC [modem] : $sc_nom : le banc n'annonce pas son port"
        fail=$((fail + 1)); n=$((n + 1))
        sc_ppid=
    fi
    wait $sc_bpid
    sc_rc=$?
    [ $sc_rc -ne 0 ] && echo "$sc_nom : code $sc_rc" >> "$BANC_LOG"
    [ -n "$sc_ppid" ] && wait $sc_ppid
    touch "$TMP/$sc_nom.bin" "$TMP/$sc_nom.bin.cmds"
    awk '$2=="TX"{print $3}' "$TMP/$sc_nom.ser" | tr '\n' ' ' > "$TMP/$sc_nom.tx"
    awk '$2=="RX"{print $3}' "$TMP/$sc_nom.ser" | tr '\n' ' ' > "$TMP/$sc_nom.rx"
    tr '\n' ' ' < "$TMP/$sc_nom.bin.cmds" > "$TMP/$sc_nom.seq"
}
has() {  # FICHIER OCTETS_PYTHON : le fichier du TMP contient la séquence
    python3 -c "import sys; sys.exit(0 if $2 in open('$TMP/$1','rb').read() else 1)"
}

# --- 0. validation des options ------------------------------------------------
check "-H connect: refusé (sortie 2)" "'$BIN' -H connect:127.0.0.1:1 -f 1 > /dev/null 2>&1; [ \$? = 2 ]"
check "-H port invalide refusé (sortie 2)" "'$BIN' -H listen:abc -f 1 > /dev/null 2>&1; [ \$? = 2 ]"
check "-H sans listen: refusé (sortie 2)" "'$BIN' -H 127.0.0.1:1 -f 1 > /dev/null 2>&1; [ \$? = 2 ]"
check "-H avec -L refusé (sortie 2)" "'$BIN' -H listen:0 -L listen:0 -f 1 > /dev/null 2>&1; [ \$? = 2 ]"
check "-H avec -S refusé (sortie 2)" "'$BIN' -H listen:0 -S listen:0 -f 1 > /dev/null 2>&1; [ \$? = 2 ]"
check "-d sans -H refusé (sortie 2)" "'$BIN' -d h:1 -f 1 > /dev/null 2>&1; [ \$? = 2 ]"
check "-d invalide refusé (sortie 2)" "'$BIN' -H listen:0 -d 'a b:1' -f 1 > /dev/null 2>&1; [ \$? = 2 ]"
check "-d sans port refusé (sortie 2)" "'$BIN' -H listen:0 -d hote -f 1 > /dev/null 2>&1; [ \$? = 2 ]"
check "-h décrit -H" "'$BIN' -h | grep -q -- '-H listen'"

# --- 1. initialisation ----------------------------------------------------------
NODSK=1
scene init 60 ""
check "init : ATE0V1 puis ATS0=0 envoyés au modem branché" "grep -q '^ATE0V1 ATS0=0 ' '$TMP/init.seq'"
check "init : aucune autre commande sans appel" "[ \"\$(tr ' ' '\n' < '$TMP/init.seq' | grep -vc '^ATE0V1\$\|^ATS0=0\$\|^\$')\" = 0 ]"
check "init : modem_pc rapporte 0 octet perdu" "grep -q 'octets perdus 0 ' '$TMP/init.err'"

if [ ! -f "$DSK" ]; then
    skip_part "disquette absente : scénarios TELEMATIC / Minitel non joués"
    banc_bilan
    echo "test_modem : $((n - fail))/$n vérifications réussies"
    [ "$fail" -eq 0 ]
    exit $?
fi
cp "$DSK" "$TMP/a.dsk"
NODSK=
CL=$(printf '\014')
E=$(printf '\033')
W=$(printf '~%.0s' $(seq 1 120))   # attente de la connexion (3 s de 390 Hz émulées)

# --- 2. appel entrant : TELEMATIC par le modem Hayes --------------------------------
scene entrant 15000 "1~~~~~~APLIC 4\n~~~~\n~~~~NDEMO$CL~~~~$E~~2\n" -- --ring \
    --on 'SERVEUR REALISE ENTIEREMENT=\E' --on 'taper quelque chose avant ENVOI=\F'
check "entrant : ATA envoyé après ATE0V1 et ATS0=0" "grep -q 'ATE0V1 ATS0=0 .*ATA ' '$TMP/entrant.seq'"
check "entrant : XLIGNE, ESC 9 o puis ESC 9 h vers le Minitel" "grep -q '1B 39 6F 1B 39 68' '$TMP/entrant.tx'"
check "entrant : SEP \$50, \$59 puis \$53 à la connexion" "grep -q '^13 50 13 59 13 53 ' '$TMP/entrant.rx'"
check "entrant : page d'accueil reçue par le correspondant" "$(has entrant.bin "b'SERVEUR REALISE ENTIEREMENT'" && echo true || echo false)"
check "entrant : réponse à ENVOI" "$(has entrant.bin "b'taper quelque chose avant ENVOI'" && echo true || echo false)"
check "entrant : Connexion/Fin, TELEMATIC raccroche (ESC 9 g)" "grep -q '1B 39 67' '$TMP/entrant.tx'"
check "entrant : raccrochage Hayes +++ puis ATH" "grep -q 'ATA .*+++ ATH ' '$TMP/entrant.seq'"
check "entrant : SEP \$59 puis \$53 au raccrochage" "grep -q '13 59 13 53 \$' '$TMP/entrant.rx'"
check "entrant : retour en attente d'appel" "grep -q 'Attente de communication' '$TMP/entrant.txt'"
check "entrant : 0 octet perdu" "grep -q 'octets perdus 0 ' '$TMP/entrant.err'"

# --- 3. appel sortant Minitel (APLIC 1, FUNCT+D) ---------------------------------
scene sortant 5000 "1~~~~~~APLIC 1\n~~~~2\n~~~~~~\fD${W}BONJOUR\n~~~~~~~~~~" -- \
    --on '=\x0c\x1fAASERVEUR DE TEST NEO6502TELESTRAT' --on 'BONJOUR\x13A=\x1f\x45\x41RECU: BONJOUR'
python3 "$HERE/hires_texte.py" "$TMP/sortant.ram" > "$TMP/sortant.ecran"
check "sortant : ATD de la cible, après l'initialisation" "grep -q 'ATE0V1 ATS0=0 ATDserveur.test:3615 ' '$TMP/sortant.seq'"
check "sortant : ESC 9 o puis ESC 9 h vers le Minitel" "grep -q '1B 39 6F 1B 39 68' '$TMP/sortant.tx'"
check "sortant : SEP \$50, \$59 puis \$53 (CONNECT)" "grep -q '^13 50 13 59 13 53 ' '$TMP/sortant.rx'"
check "sortant : page du correspondant affichée" "grep -q 'SERVEUR DE TEST NEO6502TELESTRAT' '$TMP/sortant.ecran'"
check "sortant : BONJOUR puis ENVOI reçus par le correspondant" "$(has sortant.bin "b'BONJOUR\\x13A'" && echo true || echo false)"
check "sortant : SEP \$53 transmis au correspondant au CONNECT" "$(has sortant.bin "b'\\x13S'" && echo true || echo false)"
check "sortant : réponse du correspondant affichée" "grep -q 'RECU: BONJOUR' '$TMP/sortant.ecran'"
check "sortant : 0 octet perdu" "grep -q 'octets perdus 0 ' '$TMP/sortant.err'"

# --- 4. échec d'appel : le Minitel l'apprend aussitôt (P-15) ----------------------
# Deux FUNCT+D de suite : après l'échec la ligne est libre (second ATD envoyé).
# « Aussitôt » : moins de 1000 trames (20 s émulées) entre ESC 9 h et le second
# SEP $59, la recherche de porteuse de 40 s (2000 trames) n'étant pas attendue
for res in BUSY "NO ANSWER"; do
    tag=$(echo "$res" | tr -d ' ')
    scene "echec$tag" 2500 "1~~~~~~APLIC 1\n~~~~2\n~~~~~~\fD~~~~~~~~~~~~~~~~~~~~\fD~~~~~~~~~~~~~~~~~~~~~~~~~~~~" -- --dial "$res"
    check "$res : ATD envoyé deux fois (ligne libre après l'échec)" \
        "[ \"\$(grep -c '^ATDserveur.test:3615\$' '$TMP/echec$tag.bin.cmds')\" = 2 ]"
    check "$res : SEP \$50, \$59 puis \$59 (échec), jamais \$53" \
        "grep -q '^13 50 13 59 13 59 ' '$TMP/echec$tag.rx' && ! grep -q '13 53' '$TMP/echec$tag.rx'"
    gap=$(python3 "$TMP/ana.py" gap "$TMP/echec$tag.ser")
    check "$res : échec connu aussitôt ($gap trames après ESC 9 h, moins de 1000)" "[ $gap -ge 0 ] && [ $gap -lt 1000 ]"
done

# --- 5. « NO CARRIER » dans une page (P-14), puis le vrai ----------------------------
# Le texte « NO CARRIER » au milieu d'une ligne, et « \r\nNO CARRIX » (début de motif
# sans suite), ne coupent pas la ligne : BONJOUR + ENVOI arrivent ensuite au
# correspondant. Puis il raccroche vraiment (« \r\nNO CARRIER\r\n ») juste après
# « ADIEU » : ces octets, déjà reçus du modem, doivent être remis au Minitel avant
# la déconnexion (SEP $59 $53)
scene nocarrier 6000 "1~~~~~~APLIC 1\n~~~~2\n~~~~~~\fD${W}BONJOUR\n~~~~~~~~~~" -- \
    --on '=\x0c\x1f\x41\x41MOT NO CARRIER DANS UNE PAGE\r\nNO CARRIX FIN\r\n' \
    --on 'BONJOUR\x13A=ADIEU' --on '=@NOCARRIER'
python3 "$TMP/ana.py" rx "$TMP/nocarrier.ser" "$TMP/nocarrier.apres"
check "NO CARRIER : page complète remise au Minitel" \
    "$(has nocarrier.apres "b'\\x0c\\x1fAAMOT NO CARRIER DANS UNE PAGE\\r\\nNO CARRIX FIN\\r\\nADIEU'" && echo true || echo false)"
check "NO CARRIER dans la page : la ligne tient (BONJOUR + ENVOI reçus ensuite)" \
    "$(has nocarrier.bin "b'BONJOUR\\x13A'" && echo true || echo false)"
check "vrai NO CARRIER : déconnexion SEP \$59 \$53 après ADIEU (une fois pour la connexion, une pour la fin)" \
    "$(has nocarrier.apres "b'ADIEU\\x13\\x59\\x13\\x53'" && echo true || echo false) && grep -q '13 59 13 53 \$' '$TMP/nocarrier.rx' && [ \"\$(grep -o '13 59 13 53' '$TMP/nocarrier.rx' | wc -l)\" = 2 ]"
check "vrai NO CARRIER : pas de +++ ni ATH (le correspondant a déjà raccroché)" \
    "! grep -q '+++' '$TMP/nocarrier.bin.cmds'"

# --- 6. RESET en pleine communication : PA4 repasse à la RS232 (P-03) ---------------
# Le RESET à froid (-Q) remet le VIA 2 : TELEMON laisse d'abord la prise RS232
# (trace « PA4 RS232 »), le modem doit raccrocher proprement (+++ puis ATH) au lieu
# de rester ouvert, puis être réinitialisé au retour sur la prise Minitel. Aucun octet
# de donnée ne part pendant la bascule
scene bascule 4000 "1~~~~~~APLIC 1\n~~~~2\n~~~~~~\fD${W}~~~~" -Q 1800 -- \
    --on '=\x0c\x1f\x41\x41PAGE AVANT RESET'
check "bascule : communication établie avant le RESET" "grep -q 'ATDserveur.test:3615 ' '$TMP/bascule.seq' && grep -q '13 53 ' '$TMP/bascule.rx'"
check "bascule : la trace montre PA4 -> RS232 après le RESET" "awk '\$2==\"PA4\" && \$1>=1800 && \$3==\"RS232\"{f=1} END{exit !f}' '$TMP/bascule.ser'"
check "bascule : raccrochage propre +++ puis ATH" "grep -q 'ATD[^ ]* +++ ATH ' '$TMP/bascule.seq'"
check "bascule : modem réinitialisé ensuite (ATE0V1 ATS0=0 après ATH)" "grep -q 'ATH ATE0V1 ATS0=0 \$' '$TMP/bascule.seq'"
check "bascule : le correspondant ne reçoit rien d'autre que SEP \$53 pendant la bascule" \
    "$(has bascule.bin "b'\\x13S'" && echo true || echo false) && [ \"\$(wc -c < '$TMP/bascule.bin')\" = 2 ]"
check "bascule : 0 octet perdu" "grep -q 'octets perdus 0 ' '$TMP/bascule.err'"

# --- 7. débit élevé : une page de 4000 octets d'un seul envoi (P-02) -----------------
# Le correspondant envoie tout d'un coup ; le banc ne lit le socket que dans la
# limite de la place des files (contrôle de flux), le reste attend dans le socket.
# L'ACIA remet les octets à 1200 bauds : rien ne doit manquer ni être déplacé
scene gros 4500 "1~~~~~~APLIC 1\n~~~~2\n~~~~~~\fD${W}~~~~" -- --on '=@BIG:4000'
python3 "$TMP/ana.py" rx "$TMP/gros.ser" "$TMP/gros.apres"
check "gros : 4000 octets remis à l'ACIA, dans l'ordre, sans perte" \
    "python3 -B -c \"import sys; sys.path.insert(0, '$HERE'); import modem_peer as m; d = open('$TMP/gros.apres', 'rb').read(); sys.exit(0 if d == m.big(4000) else 1)\""
check "gros : octets perdus comptés par le banc : 0" "grep -q 'octets perdus 0 ' '$TMP/gros.err'"
grep 'modem_pc' "$TMP/gros.err" | sed 's/^/   gros : /'

# --- 8. prise RS232 par le modem : octets bruts (CONSOLE de HYPER-BASIC) ---------------
scene console 4000 "1~~~~~~CONSOLE\n~~~~HELLO~~~~~" -k 8 -- --console
check "console : HELLO tapé arrive au modem en octets bruts" "grep -q 'RS232 : HELLO' '$TMP/console.bin.cmds'"
check "console : la réponse du modem est affichée" "grep -q 'SALUT' '$TMP/console.txt'"
check "console : HELLO et SALUT passent par la prise RS232 (RTX / RRX tracés)" "grep -q 'RTX 48' '$TMP/console.ser' && grep -q 'RRX 53' '$TMP/console.ser'"

banc_bilan
echo "test_modem : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
