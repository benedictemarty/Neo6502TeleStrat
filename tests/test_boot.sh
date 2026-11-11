#!/bin/sh
# test_boot.sh — démarrage de TELEMON 2.4 avec les vraies ROM, par configuration.
# Référence : écran d'Oricutron (Telestrat, mêmes ROM) et notice « Extension
# RAM 64 Ko » (128 Ko affichés avec la cartouche RAM).
set -u
. "$(dirname "$0")/lib.sh"
BIN=${1:-build/telestrat_headless}
fail=0
n=0

expect() {  # config motif
    n=$((n + 1))
    out=$(banc -c "$1" -f 300 -s -b)
    if printf '%s\n' "$out" | grep -q -- "$2"; then
        :
    else
        fail=$((fail + 1))
        echo "ÉCHEC [$1] : « $2 » absent de l'écran :"
        printf '%s\n' "$out" | grep -v '^$' | sed 's/^/    /'
    fi
}

for c in standard ram64k oricutron telemon; do
    expect "$c" "TELEMON V2.4"
    expect "$c" "(c) 1986 ORIC International"
    expect "$c" "Drive:A-B-C-D"
    expect "$c" "Inserez une disquette"
done
expect standard  "64 Ko RAM, 56 Ko ROM"
expect ram64k    "128 Ko RAM, 32 Ko ROM"
expect oricutron "128 Ko RAM, 48 Ko ROM"
expect telemon   "64 Ko RAM, 16 Ko ROM"
# État des banques laissé par le RESET : $0F = RAM (notice, chap. V-10)
expect ram64k    "etat des banques (\$0200-\$0207) : 00 0F 0F 0F 0F"

# --- Disquette système (sprint 2) : STRATSED V2.0c, HYPER-BASIC, SAVE/LOAD, LPRINT
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
if [ -f "$DSK" ]; then
    tmp_init
    cp "$DSK" "$TMP/a.dsk"
    run_disk() {  # motif, puis arguments du banc
        n=$((n + 1))
        motif=$1
        shift
        out=$(banc -c oricutron "$@" -s)
        if ! printf '%s\n' "$out" | grep -q -- "$motif"; then
            fail=$((fail + 1))
            echo "ÉCHEC [disque] : « $motif » absent de l'écran :"
            printf '%s\n' "$out" | grep -v '^$' | sed 's/^/    /'
        fi
    }
    # Écran de référence : Oricutron 1.2.0 (rev. 002279f), mêmes ROM, même disquette
    for m in "STRATSED V2.0c" "HYPER BASIC V2.0b" "TELEASS V1.0a" "1- HYPER-BASIC" "Votre choix:"; do
        run_disk "$m" -0 "$TMP/a.dsk" -f 600
    done
    run_disk "Imprimante,Drive:A-B-C-D" -0 "$TMP/a.dsk" -P /dev/null -f 600
    run_disk "44 Ko libres" -0 "$TMP/a.dsk" -f 900 -w 500 -t '1'
    run_disk "  42" -0 "$TMP/a.dsk" -f 1200 -w 500 -t '1~~~~~~PRINT 6*7\n'
    run_disk "secteurs libres, 88 fichiers" -0 "$TMP/a.dsk" -f 1500 -w 500 -t '1~~~~~~DIR\n'
    # Écriture : SAVE sur une copie, relecture dans une nouvelle session
    # (-k 6 : à 4 trames par touche, le second S d'ESSAI peut se perdre, la
    # ROM ne voyant pas le relâchement ; vu avec le VIA du socle, v0.16.29)
    banc -c oricutron -0 "$TMP/a.dsk" -W "$TMP/b.dsk" -f 1800 -w 500 -k 6 \
        -t '1~~~~~~10 PRINT "NEO6502"\nSAVE "ESSAI"\n' >/dev/null
    run_disk " NEO6502" -0 "$TMP/b.dsk" -f 1700 -w 500 -k 6 -t '1~~~~~~LOAD "ESSAI"\nRUN\n'
    run_disk "secteurs libres, 89 fichiers" -0 "$TMP/b.dsk" -f 1500 -w 500 -t '1~~~~~~DIR\n'
    # Imprimante
    banc -c oricutron -0 "$TMP/a.dsk" -P "$TMP/lpr.txt" -f 1300 -w 500 \
        -t '1~~~~~~LPRINT "BONJOUR TELESTRAT"\n' >/dev/null
    n=$((n + 1))
    if ! grep -q "BONJOUR TELESTRAT" "$TMP/lpr.txt"; then
        fail=$((fail + 1))
        echo "ÉCHEC [imprimante] : sortie = $(od -c "$TMP/lpr.txt" | head -3)"
    fi
else
    skip_part "disquette système absente ($DSK), tests disque"
fi

# Frappe du banc (-t, v0.16.24) : SHIFT seul appuyé une trame avant un
# caractère qui en a besoin ; pressé avec la touche, il manquait parfois
# (« A95) » pour « A(5) ») ; lignes à parenthèses et guillemets, -k 10 (le
# banc d'avant y perdait SHIFT ; à -k 8, une lettre tapée juste après une
# ligne longue à exécuter peut encore se perdre : autre cause, non examinée)
n=$((n + 1))
FRAPPE=$(banc -c atmos -f 2200 -w 150 -k 10 \
    -t 'DIM A(9)\nA(1)=1:A(2)=2\nA(3)=(4)\nA(5)=(6)\nA(7)=(8)\nPRINT "(OK)";A(5)\nB$="()"\nA(9)=(1)\nPRINT A(1)+A(9)\n' -s 2>&1)
if echo "$FRAPPE" | grep -q 'SYNTAX' || ! echo "$FRAPPE" | grep -q '(OK) 6'; then
    fail=$((fail + 1))
    echo "ÉCHEC [frappe] : SHIFT perdu ? $(echo "$FRAPPE" | grep -v '^ *$' | tail -6 | tr '\n' '|')"
fi

# Options du banc : validation des arguments (codes de retour : 0 réussite, 1
# erreur d'exécution, 2 option invalide), lecteurs B à D (-1 à -3), -h
[ -n "$TMP" ] || tmp_init
code() {  # attendu description -- arguments du banc
    n=$((n + 1))
    attendu=$1
    desc=$2
    shift 3
    "$BIN" "$@" > "$TMP/opt.out" 2> "$TMP/opt.err"
    rc=$?
    if [ "$rc" -ne "$attendu" ]; then
        fail=$((fail + 1))
        echo "ÉCHEC [options] : $desc : code $rc (attendu $attendu) : $(head -c 200 "$TMP/opt.err")"
    fi
}
if [ -f "$DSK" ]; then
    cp "$DSK" "$TMP/o.dsk"
    for d in 1 2 3; do code 0 "-$d : image insérée dans le lecteur" -- -c oricutron -$d "$TMP/o.dsk" -f 100; done
    code 1 "-1 : image invalide refusée" -- -c oricutron -1 /dev/null -f 10
fi
code 0 "-h : aide" -- -h
code 2 "option inconnue" -- -Q9 -z
code 2 "-f non numérique" -- -f abc
code 2 "-w négatif" -- -w -5
code 2 "-X sans « : »" -- -X 1290 -f 10
code 2 "-M sans -U" -- -M 5:u -f 10
code 2 "-W sans -0" -- -W "$TMP/w.dsk" -f 10
code 2 "configuration inconnue" -- -c nexistepas -f 10
code 2 "-L listen:abc : port invalide" -- -L listen:abc -f 10
code 2 "-L listen:99999 : port hors limites" -- -L listen:99999 -f 10
code 2 "-L connect:hote sans port" -- -L connect:hote -f 10
code 2 "-S connect:127.0.0.1:0 : port 0 refusé en sortie" -- -S connect:127.0.0.1:0 -f 10
code 1 "-S connect:[::1]:1 : crochets IPv6 acceptés, connexion refusée (rien n'écoute)" -- -S "connect:[::1]:1" -f 10
code 1 "-G : répertoire absent" -- -G "fx80:$TMP/absent" -f 10
code 0 "-k 1 ramené à 2 (avertissement)" -- -k 1 -f 20
if ! grep -q 'ramené à 2' "$TMP/opt.err"; then fail=$((fail + 1)); echo "ÉCHEC [options] : -k 1 sans avertissement"; fi
n=$((n + 1))
banc_bilan
echo "test_boot : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
