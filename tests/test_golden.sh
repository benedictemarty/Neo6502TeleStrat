#!/bin/sh
# test_golden.sh — image rendue comparée pixel par pixel à celle d'Oricutron.
#
# Référence : tests/golden/stratsed_menu_oricutron.ppm.gz, produite une fois
# par tools/oracle/oracle.sh (Oricutron 1.2.0 rev. 002279f, patch
# oricutron-dump.patch, ORIC_DUMP_PPM) : configuration par défaut d'Oricutron
# (oricutron), STRATSED.DSK dans A, imprimante branchée, 400 trames, menu
# « Votre choix: ». Commande : ORIC_DUMP_PPM=ref.ppm sh tools/oracle/oracle.sh
# 400 STRATSED.DSK. La case du curseur (rangée 20, colonne 12) est ignorée :
# sa phase de clignotement dépend du minutage du démarrage (identique au
# pixel près à la trame 420, v0.16.23).
set -u
BIN=${1:-build/telestrat_headless}
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
GOLD=$(dirname "$0")/golden/stratsed_menu_oricutron.ppm.gz
if [ ! -f "$DSK" ]; then
    echo "test_golden : disquette absente ($DSK), ignoré"
    exit 0
fi
TMP=$(mktemp -d)
cp "$DSK" "$TMP/a.dsk"
"$BIN" -c oricutron -0 "$TMP/a.dsk" -P "$TMP/lp.txt" -f 400 -p "$TMP/nous.ppm" > /dev/null 2>&1
python3 - "$GOLD" "$TMP/nous.ppm" <<'PY'
import gzip, sys
def pixels(data):
    head = data.split(b"\n", 3)
    assert head[0] == b"P6" and head[1] == b"240 224", "image 240 x 224 attendue"
    return head[3]
ref = pixels(gzip.open(sys.argv[1]).read())
nous = pixels(open(sys.argv[2], "rb").read())
masque = {(20, 12)}  # case du curseur
diff = [i // 3 for i in range(0, len(ref), 3)
        if ref[i:i + 3] != nous[i:i + 3] and ((i // 3) // 240 // 8, (i // 3) % 240 // 6) not in masque]
n, ok = 1, not diff and len(ref) == len(nous)
if not ok:
    cases = sorted({(p // 240 // 8, p % 240 // 6) for p in diff})
    print(f"ÉCHEC [golden] : {len(diff)} pixels différents d'Oricutron, cases (rangée, colonne) {cases[:12]}")
print(f"test_golden : {n - (0 if ok else 1)}/{n} image identique à Oricutron (hors curseur)")
sys.exit(0 if ok else 1)
PY
r=$?
rm -rf "$TMP"
exit $r
