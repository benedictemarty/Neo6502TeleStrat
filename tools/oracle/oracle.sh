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
# quand le patch change (empreinte dans $WORK/.patch). $WORK : ORACLE_DIR, sinon
# ~/.cache/neo6502telestrat-oracle (droits 700, propriétaire vérifié, marque
# .oracle-neo6502telestrat exigée avant toute suppression).
#
# Usage : oracle.sh TRAMES DISQUETTE.dsk "telebank6 = 'roms/hyperbas'" … [RAM=12345]
#   Les lignes telebankN remplacent celles de la configuration ; la banque 5
#   garde TELE-ASS (défaut d'Oricutron) sauf si RAM=… la cite.
set -eu
ORICUTRON=${ORICUTRON:-$HOME/oricutron}
WORK=${ORACLE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/neo6502telestrat-oracle}
ICI=$(cd "$(dirname "$0")" && pwd)
SUM=$(cksum < "$ICI/oricutron-dump.patch")
MARQUE=.oracle-neo6502telestrat

# Répertoire de travail : jamais un chemin dangereux, jamais un répertoire qui ne
# nous appartient pas ou que d'autres peuvent modifier (le binaire qui s'y trouve
# est exécuté, le répertoire est supprimé et refait quand le patch change)
case "$WORK" in
    "" | / | "$HOME" | "$HOME"/ | /tmp | /tmp/)
        echo "oracle : répertoire de travail refusé : « $WORK »" >&2
        exit 1
        ;;
esac
if [ -e "$WORK" ]; then
    if [ ! -d "$WORK" ] || [ -L "$WORK" ] || [ ! -O "$WORK" ] || [ ! -f "$WORK/$MARQUE" ]; then
        echo "oracle : $WORK existe sans être un répertoire de oracle.sh appartenant à l'utilisateur courant ; le supprimer ou fixer ORACLE_DIR" >&2
        exit 1
    fi
    case "$(stat -c %a "$WORK" 2> /dev/null || echo 700)" in
        ?[2367]? | ??[2367])
            echo "oracle : $WORK modifiable par d'autres utilisateurs, refusé" >&2
            exit 1
            ;;
    esac
fi
if [ ! -x "$WORK/oricutron" ] || [ "$(cat "$WORK/.patch" 2>/dev/null)" != "$SUM" ]; then
    [ -d "$WORK" ] && rm -rf "$WORK"
    mkdir -p "$(dirname "$WORK")"
    cp -r "$ORICUTRON" "$WORK"
    chmod 700 "$WORK"
    : > "$WORK/$MARQUE"
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
