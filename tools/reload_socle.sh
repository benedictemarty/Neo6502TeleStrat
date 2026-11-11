#!/bin/sh
# reload_socle.sh — socle reload-emulator épinglé sur une étiquette vérifiée
#
#   tools/reload_socle.sh [ÉTIQUETTE] [DÉPÔT_RELOAD]
#
# ÉTIQUETTE : par défaut le contenu de tools/SOCLE (seule source de l'étiquette,
# lue aussi par le Makefile et platforms/rp2040/CMakeLists.txt).
# DÉPÔT_RELOAD : par défaut le premier trouvé parmi $RELOAD_REPO,
# ~/Projets/reload-emulator, ~/reload-emulator qui contienne l'étiquette.
#
# Clone local (sans réseau) du dépôt à l'étiquette dans
# ${RELOAD_SOCLE_DIR:-${XDG_CACHE_HOME:-~/.cache}/reload-socle}/ÉTIQUETTE, avec les
# sous-modules de la carte (pico-sdk, PicoDVI, tinyusb) repris des copies locales
# de reload. Le dépôt reload n'est pas modifié ; rien n'est publié.
#
# Cache déjà présent : .socle-ok contient « ÉTIQUETTE HASH » ; le cache n'est
# accepté que si son HEAD est ce HASH et, quand le dépôt est trouvé, si l'étiquette
# désigne toujours ce commit. Sinon : erreur (supprimer le cache à la main).
set -eu

ICI=$(cd "$(dirname "$0")" && pwd)
TAG="${1:-$(tr -d ' \t\r\n' < "$ICI/SOCLE")}"
CACHE="${RELOAD_SOCLE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/reload-socle}"
DST="$CACHE/$TAG"

if [ -z "$TAG" ]; then
    echo "reload_socle : étiquette vide (tools/SOCLE illisible)" >&2
    exit 1
fi

# Dépôt reload : argument, RELOAD_REPO, puis les emplacements habituels
SRC=
for c in "${2:-}" "${RELOAD_REPO:-}" "$HOME/Projets/reload-emulator" "$HOME/reload-emulator"; do
    [ -n "$c" ] || continue
    if git -C "$c" rev-parse -q --verify "refs/tags/$TAG^{commit}" > /dev/null 2>&1; then
        SRC=$c
        break
    fi
done

aide() {
    cat >&2 << MSG
reload_socle : socle reload-emulator « $TAG » introuvable.
  L'étiquette est locale au dépôt reload-emulator (aucun remote ne la publie).
  Pour obtenir le socle :
    - avoir un dépôt reload-emulator portant l'étiquette $TAG, puis
      RELOAD_REPO=/chemin/du/dépôt make … (cherchés : \$RELOAD_REPO,
      ~/Projets/reload-emulator, ~/reload-emulator) ;
    - ou fournir un arbre déjà prêt : make RELOAD_DIR=/chemin/de/l/arbre … ;
    - l'étiquette retenue est dans tools/SOCLE.
MSG
}

# Cache valide ?
if [ -f "$DST/.socle-ok" ]; then
    lu_tag=$(cut -d' ' -f1 "$DST/.socle-ok")
    lu_hash=$(cut -d' ' -f2 "$DST/.socle-ok")
    tete=$(git -C "$DST" rev-parse HEAD 2> /dev/null || echo "")
    case "$tete" in
        "$lu_hash"*) ;;
        *)
            echo "reload_socle : cache $DST incohérent (.socle-ok : $lu_hash, HEAD : ${tete:-illisible}) ; le supprimer pour le refaire" >&2
            exit 1
            ;;
    esac
    if [ "$lu_tag" != "$TAG" ]; then
        echo "reload_socle : $DST/.socle-ok annonce l'étiquette $lu_tag, pas $TAG ; supprimer le cache" >&2
        exit 1
    fi
    if [ -n "$SRC" ]; then
        attendu=$(git -C "$SRC" rev-parse "refs/tags/$TAG^{commit}")
        case "$attendu" in
            "$lu_hash"*) ;;
            *)
                echo "reload_socle : l'étiquette $TAG de $SRC désigne $attendu, le cache $DST est à $lu_hash ; supprimer le cache" >&2
                exit 1
                ;;
        esac
    fi
    exit 0
fi

if [ -z "$SRC" ]; then
    aide
    exit 1
fi

# Construction dans un répertoire temporaire du même dossier, puis renommage :
# un échec ou une interruption ne laisse jamais un cache à moitié fait
mkdir -p "$CACHE"
TMP=$(mktemp -d "$CACHE/.en-cours.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM
git clone -q --no-checkout "$SRC" "$TMP/socle"
git -C "$TMP/socle" -c advice.detachedHead=false checkout -q "$TAG"
for m in platforms/rp2040/lib/pico-sdk platforms/rp2040/lib/PicoDVI platforms/rp2040/lib/tinyusb; do
    name=$(git -C "$TMP/socle" config -f .gitmodules --get-regexp '^submodule\..*\.path$' | awk -v p="$m" '$2 == p { sub(/^submodule\./, "", $1); sub(/\.path$/, "", $1); print $1 }')
    git -C "$TMP/socle" config "submodule.$name.url" "$SRC/$m"
    git -C "$TMP/socle" -c protocol.file.allow=always submodule update -q --init "$m"
done
echo "$TAG $(git -C "$TMP/socle" rev-parse HEAD)" > "$TMP/socle/.socle-ok"
rm -rf "$DST"
mv "$TMP/socle" "$DST"
echo "reload_socle : $DST ($(cat "$DST/.socle-ok"))"
