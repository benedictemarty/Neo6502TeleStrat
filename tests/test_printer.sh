#!/bin/sh
# test_printer.sh — imprimantes émulées : Epson FX-80 (pages PNG) et table
# traçante MCP-40 (SVG).
#   1. printer_render : un flux FX-80 (texte, modes, graphiques ESC K, saut de
#      page) donne deux pages PNG valides ; un flux MCP-40 un SVG valide ;
#   2. de bout en bout (disquette STRATSED) : LPRINT sous HYPER-BASIC, banc
#      -G fx80 : page PNG avec le texte en haut à gauche ; -G mcp40 : trait
#      tracé par CHR$(18) et « D » ; octets bruts toujours dans -P ;
#   3. menu : Entrée sur Imprimante fait défiler texte, FX-80, MCP-40, coupée ;
#      Enregistrer écrit imprimante_type= dans TELESTRA.CFG.
set -u
BIN=${1:-build/telestrat_headless}
RENDER=${2:-build/printer_render}
DSK=${STRATSED_DSK:-$HOME/oriclib/games/dsk/STRATSED.DSK}
TMP=$(mktemp -d)
fail=0
n=0
check() {
    n=$((n + 1))
    if ! sh -c "$2"; then
        fail=$((fail + 1))
        echo "ÉCHEC [imprimante] : $1"
    fi
}

# 1. Rendu seul
python3 - "$TMP" <<'PY'
import sys
t = sys.argv[1]
E = b"\x1b"
d = b"Bonjour Telestrat\r\n" + E + b"E" + b"Gras" + E + b"F\r\n" + b"\x0fCondense\x12\r\n"
d += E + b"K" + bytes([16, 0]) + bytes([0xFF] * 16) + b"\r\n" + b"\x0cPage 2\r\n"
open(t + "/fx.bin", "wb").write(d)
p = b"Texte\r\n\x12D100,0,100,-100\rC3\rJ-100,0\rS1\rPOK\rA\r"
open(t + "/mcp.bin", "wb").write(p)
PY
mkdir "$TMP/r1"
"$RENDER" fx80 "$TMP/fx.bin" "$TMP/r1" > "$TMP/r1.txt"
"$RENDER" mcp40 "$TMP/mcp.bin" "$TMP/r1" >> "$TMP/r1.txt"
check "FX-80 : deux pages" "grep -q '2 page(s) PNG' '$TMP/r1.txt' && [ -f '$TMP/r1/IMPR0001.PNG' ] && [ -f '$TMP/r1/IMPR0002.PNG' ]"
check "FX-80 : PNG valides, 1224 x 1584" "python3 tests/png_info.py '$TMP/r1/IMPR0001.PNG' | grep -q '^1224 1584 ' && python3 tests/png_info.py '$TMP/r1/IMPR0002.PNG' | grep -q '^1224 1584 '"
check "FX-80 : texte en haut de la page 1" "[ \$(python3 tests/png_info.py '$TMP/r1/IMPR0001.PNG' 0 18 36 400 | cut -d' ' -f3) -gt 300 ]"
check "FX-80 : graphiques ESC K (4e ligne pleine)" "[ \$(python3 tests/png_info.py '$TMP/r1/IMPR0001.PNG' 72 88 36 72 | cut -d' ' -f3) -gt 400 ]"
check "MCP-40 : SVG valide (numéro suivant)" "python3 -c 'import xml.dom.minidom as m; m.parse(\"$TMP/r1/IMPR0003.SVG\")'"
check "MCP-40 : traits, rouge, texte" "grep -q 'd=\"M0 18L100 0L100 100\"' '$TMP/r1/IMPR0003.SVG' && grep -q '#d42020' '$TMP/r1/IMPR0003.SVG' && grep -q '>OK</text>' '$TMP/r1/IMPR0003.SVG'"

# 2. De bout en bout
if [ -f "$DSK" ]; then
    cp "$DSK" "$TMP/a.dsk"
    mkdir "$TMP/fx" "$TMP/mcp"
    "$BIN" -c oricutron -0 "$TMP/a.dsk" -P "$TMP/fx/raw.bin" -G "fx80:$TMP/fx" -f 1400 -w 500 \
        -t '1~~~~~~LPRINT "BONJOUR TELESTRAT"\n' > /dev/null
    check "LPRINT : octets bruts dans -P" "grep -q 'BONJOUR TELESTRAT' '$TMP/fx/raw.bin'"
    check "LPRINT : une page PNG" "[ -f '$TMP/fx/IMPR0001.PNG' ] && [ ! -f '$TMP/fx/IMPR0002.PNG' ]"
    check "LPRINT : texte en haut à gauche" "[ \$(python3 tests/png_info.py '$TMP/fx/IMPR0001.PNG' 0 18 36 330 | cut -d' ' -f3) -gt 300 ] && [ \$(python3 tests/png_info.py '$TMP/fx/IMPR0001.PNG' 18 1584 0 1224 | cut -d' ' -f3) -eq 0 ]"
    "$BIN" -c oricutron -0 "$TMP/a.dsk" -G "mcp40:$TMP/mcp" -f 1700 -w 500 -k 6 \
        -t '1~~~~~~LPRINT CHR$(18)\nLPRINT "D100,0,100,50"\nLPRINT "A"\n' > /dev/null
    check "LPRINT : tracé MCP-40" "grep -q 'd=\"M0 0L100 0L100 -50\"' '$TMP/mcp/IMPR0001.SVG'"
else
    echo "test_printer : disquette système absente ($DSK), tests de bout en bout ignorés"
fi

# 3. Menu (-G fx80 : FX-80 choisie) : u u u u = imprimante ; e = MCP-40 ; z u e =
#    Enregistrer
mkdir "$TMP/cle"
"$BIN" -c standard -U "$TMP/cle" -G "fx80:$TMP/cle" -M "5:uuuuezuex" -f 10 > /dev/null 2> "$TMP/menu.txt"
check "menu : Imprimante : Traceur MCP-40" "grep -q 'Imprimante : Traceur MCP-40' '$TMP/menu.txt'"
check "TELESTRA.CFG : imprimante_type=mcp40" "grep -qx 'imprimante_type=mcp40' '$TMP/cle/TELESTRA.CFG' && grep -qx 'impression=oui' '$TMP/cle/TELESTRA.CFG'"

rm -rf "$TMP"
echo "test_printer : $((n - fail))/$n vérifications réussies"
[ "$fail" -eq 0 ]
