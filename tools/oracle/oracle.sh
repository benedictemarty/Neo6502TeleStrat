#!/bin/sh
# oracle.sh — écran texte d'Oricutron (oracle de comparaison), sans fenêtre.
#
# Construit une fois une copie d'Oricutron (ORICUTRON=~/oricutron par défaut,
# GPL v2, non distribué ici) avec tools/oracle/oricutron-dump.patch : après
# ORIC_DUMP_FRAMES trames (en vitesse maximale), l'écran texte ($BB80, 28 x 40)
# est écrit sur la sortie ; ORIC_RAM_BANKS=12345 met ces banques en RAM (un
# fichier vide dans la configuration d'Oricutron n'y parvient pas) ;
# ORIC_DUMP_PPM=fichier.ppm écrit aussi l'image rendue (240 x 224, palette
# d'Oricutron), à comparer à `telestrat_headless -p`. Copie reconstruite
# quand le patch change (empreinte dans $WORK/.patch).
#
# Usage : oracle.sh TRAMES DISQUETTE.dsk "telebank6 = 'roms/hyperbas'" … [RAM=12345]
#   Les lignes telebankN remplacent celles de la configuration ; la banque 5
#   garde TELE-ASS (défaut d'Oricutron) sauf si RAM=… la cite.
set -eu
ORICUTRON=${ORICUTRON:-$HOME/oricutron}
WORK=${ORACLE_DIR:-${TMPDIR:-/tmp}/neo6502telestrat-oracle}
ICI=$(cd "$(dirname "$0")" && pwd)
SUM=$(cksum < "$ICI/oricutron-dump.patch")
if [ ! -x "$WORK/oricutron" ] || [ "$(cat "$WORK/.patch" 2>/dev/null)" != "$SUM" ]; then
    rm -rf "$WORK"
    cp -r "$ORICUTRON" "$WORK"
    (cd "$WORK" && patch -p1 < "$ICI/oricutron-dump.patch" > /dev/null && make -j8 > build.log 2>&1)
    cp "$WORK/oricutron.cfg" "$WORK/oricutron.cfg.orig"
    echo "$SUM" > "$WORK/.patch"
fi
frames=$1
disk=$2
shift 2
ram=""
cfg="$WORK/oricutron.cfg"
sed '/^telebank[0-9]/d; /^machine *=/d; s/^rendermode = opengl/rendermode = soft/' "$WORK/oricutron.cfg.orig" > "$cfg"
for a in "$@"; do
    case "$a" in
        RAM=*) ram=${a#RAM=} ;;
        *) echo "$a" >> "$cfg" ;;
    esac
done
echo "machine = telestrat" >> "$cfg"
cp "$disk" "$WORK/disque.dsk"
cd "$WORK"
ORIC_RAM_BANKS=$ram SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ORIC_DUMP_FRAMES=$frames \
    timeout 300 ./oricutron -mt -kmicrodisc -d "$WORK/disque.dsk" 2> /dev/null
