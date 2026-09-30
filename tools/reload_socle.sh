#!/bin/sh
# reload_socle.sh — socle reload-emulator épinglé sur une étiquette vérifiée
#
#   tools/reload_socle.sh [ÉTIQUETTE] [DÉPÔT_RELOAD]
#
# Clone local (sans réseau) de DÉPÔT_RELOAD (~/reload-emulator) à ÉTIQUETTE
# (socle-AAAA-MM-JJ, posée par la session reload-emulator) dans
# ~/.cache/reload-socle/ÉTIQUETTE, avec les sous-modules de la carte (pico-sdk,
# PicoDVI, tinyusb) repris des copies locales de reload. Le dépôt reload
# n'est pas modifié. Déjà présent : rien n'est refait.
# Le Makefile s'en sert par défaut (RELOAD_SOCLE, RELOAD_DIR).
set -e
TAG="${1:-socle-2026-10-01-2}"
SRC="${2:-$HOME/reload-emulator}"
DST="${RELOAD_SOCLE_DIR:-$HOME/.cache/reload-socle}/$TAG"

if [ -f "$DST/.socle-ok" ]; then
    exit 0
fi
git -C "$SRC" rev-parse -q --verify "refs/tags/$TAG" > /dev/null || {
    echo "reload_socle : étiquette $TAG absente de $SRC" >&2
    exit 1
}
rm -rf "$DST"
mkdir -p "$(dirname "$DST")"
git clone -q --no-checkout "$SRC" "$DST"
git -C "$DST" -c advice.detachedHead=false checkout -q "$TAG"
for m in platforms/rp2040/lib/pico-sdk platforms/rp2040/lib/PicoDVI platforms/rp2040/lib/tinyusb; do
    name=$(git -C "$DST" config -f .gitmodules --get-regexp '^submodule\..*\.path$' | awk -v p="$m" '$2 == p { sub(/^submodule\./, "", $1); sub(/\.path$/, "", $1); print $1 }')
    git -C "$DST" config "submodule.$name.url" "$SRC/$m"
    git -C "$DST" -c protocol.file.allow=always submodule update -q --init "$m"
done
echo "$TAG $(git -C "$DST" rev-parse --short HEAD)" > "$DST/.socle-ok"
echo "reload_socle : $DST ($(cat "$DST/.socle-ok"))"
