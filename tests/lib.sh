# lib.sh — fonctions communes des scripts de test (à lire avec « . "$(dirname "$0")/lib.sh" »)
#
#   skip RAISON         test ignoré : écrit « SKIP nom : raison », l'ajoute à $SKIP_LOG
#                       (que le Makefile résume ; STRICT=1 make test échoue s'il n'est
#                       pas vide) puis sort avec 0
#   skip_part RAISON    partie du test ignorée (comptée comme SKIP), le script continue
#   tmp_init            crée $TMP (mktemp -d, droits 700) ; supprimé en sortie, sauf si GARDE est
#                       défini (le chemin est alors affiché)
#   pid_add PID         processus d'arrière-plan arrêtés en sortie
#   run COMMANDE...     lance la commande ; un code de retour non nul est un échec du test
#   sed_i EXPR FICHIER  sed -i portable
#   banc ARGS...        lance $BIN ; un code de retour non nul est un échec du test
#                       (compté par banc_bilan), sans arrêter le script
#   banc_bilan          ajoute les échecs du banc aux compteurs fail/n du script
#   port_attend FICHIER annonce « écoute sur 127.0.0.1:N » du banc ou fichier .port d'un
#                       correspondant : affiche N (attente 10 s au plus), code 1 sinon
#
# Le script appelant fait « set -u » et définit BIN avant d'appeler banc. Attention :
# run et banc ne rendent jamais le code de la commande (le test continue) ; les
# échecs sont relus par banc_bilan en fin de script.

TEST_NAME=$(basename "$0" .sh)
TMP=
LIB_PIDS=
BANC_LOG=/dev/null

skip() {
    echo "SKIP $TEST_NAME : $*"
    [ -n "${SKIP_LOG:-}" ] && echo "$TEST_NAME : $*" >> "$SKIP_LOG"
    exit 0
}

# Partie du test ignorée : signalée et comptée, le script continue
skip_part() {
    echo "SKIP $TEST_NAME : $*"
    [ -n "${SKIP_LOG:-}" ] && echo "$TEST_NAME : $* (partie)" >> "$SKIP_LOG"
    return 0
}

lib_cleanup() {
    for p in $LIB_PIDS; do kill "$p" 2> /dev/null; done
    if [ -n "$TMP" ] && [ -d "$TMP" ]; then
        if [ -n "${GARDE:-}" ]; then echo "$TMP"; else rm -rf "$TMP"; fi
    fi
}
trap lib_cleanup EXIT
trap 'exit 130' INT TERM

tmp_init() {
    TMP=$(mktemp -d "${TMPDIR:-/tmp}/neo6502telestrat.XXXXXX") || exit 1
    chmod 700 "$TMP"
    BANC_LOG=$TMP/.banc_echecs
    : > "$BANC_LOG"
}

pid_add() { LIB_PIDS="$LIB_PIDS $1"; }

run() {
    "$@"
    lib_rc=$?
    if [ $lib_rc -ne 0 ]; then
        echo "code $lib_rc : ${1##*/} ${2:-}${3:+ $3}…" >> "$BANC_LOG"
    fi
    return 0
}

banc() { run "$BIN" "$@"; }

# sed -i portable (BSD et GNU) : sed_i EXPRESSION FICHIER
sed_i() {
    sed "$1" "$2" > "$2.sed.$$" && mv "$2.sed.$$" "$2"
}

banc_bilan() {
    lib_n=$(grep -c . "$BANC_LOG" 2> /dev/null || true)
    [ "${lib_n:-0}" -gt 0 ] && sed "s/^/ÉCHEC [$TEST_NAME] : le banc a rendu /" "$BANC_LOG"
    fail=$((fail + ${lib_n:-0}))
    n=$((n + ${lib_n:-0}))
}

port_attend() {
    lib_i=0
    while [ $lib_i -lt 200 ]; do
        if [ -s "$1" ]; then
            lib_p=$(sed -n 's/.*127\.0\.0\.1:\([0-9][0-9]*\).*/\1/p; t; /^[0-9][0-9]*$/p' "$1" | head -n 1)
            if [ -n "$lib_p" ]; then echo "$lib_p"; return 0; fi
        fi
        sleep 0.05
        lib_i=$((lib_i + 1))
    done
    return 1
}
