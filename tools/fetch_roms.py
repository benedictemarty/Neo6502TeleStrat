#!/usr/bin/env python3
"""Récupère les ROM du Telestrat et génère src/roms/telestrat_roms.h.

Les ROM ne sont pas versionnées dans ce dépôt, qui n'établit pas les droits dessus
(voir docs/ROMS.md : provenance de chacune, aucune licence affirmée). Ce script les
prend, dans l'ordre, dans un clone local, dans l'en-tête déjà généré (régénération
sans réseau), puis les télécharge (HTTPS) ; chaque image est vérifiée par sa taille,
son MD5 et, quand il est connu, son SHA-256, puis écrite en en-tête C (un tableau
`static const` par banque).

ROM optionnelles (STRATORIC, Atmos, Oric-1 : proposées par le menu, absentes de la
flash avec TELESTRAT_ROMS_CLE) : sans source, le tableau est généré à zéro et la
macro TELESTRAT_HAS_<NOM> vaut 0 (1 sinon) ; les autres ROM sont obligatoires.

Usage : tools/fetch_roms.py [--offline] [--out src/roms/telestrat_roms.h]
"""
import argparse
import hashlib
import os
import re
import sys
import tempfile
import urllib.request
import zipfile

HOME = os.path.expanduser("~")
RACINES = (os.path.join(HOME, "Projets"), HOME)  # clones locaux : ~/Projets/X puis ~/X
MAX_TELECHARGEMENT = 1 << 20

# nom C, taille dans la banque, décalage dans la banque, md5, sha256 (None : non établi),
# chemin relatif du clone local, URL (None : aucune source publique connue du dépôt),
# provenance (texte de l'en-tête généré et de docs/ROMS.md), optionnelle
ROMS = [
    ("telestrat_telemon24", 0x4000, 0x0000, "9a432244d9ee4a49e8ddcde64af94e05",
     "0ed991887342fba9e4b71668ff4c14ed93c2b3a19b4874dd0282404a1c442094",
     "telemon/original/telemon.rom",
     "https://raw.githubusercontent.com/jedeoric/telemon/master/original/telemon.rom",
     "TELEMON 2.4, dépôt jedeoric/telemon (GitHub)", False),
    ("telestrat_hyperbas", 0x4000, 0x0000, "364bf095e0dc4222d75354d50b8cddfc",
     "08eaf63a55a7659a6da7c91cf4a57bb0e2e21ff856d0957928605aa5feed09c6",
     "Hyper-Basic/original/hyperbas.rom",
     "https://raw.githubusercontent.com/assinie/Hyper-Basic/master/original/hyperbas.rom",
     "HYPER-BASIC, dépôt assinie/Hyper-Basic (GitHub)", False),
    ("telestrat_teleass", 0x4000, 0x0000, "2324c9cc227c1327a72a667c97ed2990",
     "49921a9f932f7ecc2947b9b08f0451a393dc48f55317209c44338dc8bcf227ee",
     "tele-ass/original/teleass.rom",
     "https://raw.githubusercontent.com/jedeoric/tele-ass/master/original/teleass.rom",
     "TELE-ASS, dépôt jedeoric/tele-ass (GitHub)", False),
    # Cartouche de 8 Ko câblée en $E000-$FFFF (vecteur RESET = $E000) ; l'image est
    # recopiée dans les DEUX moitiés de la banque de 16 Ko (A13 non décodée :
    # hypothèse, pas une mesure sur le matériel)
    ("telestrat_telematic", 0x2000, 0x2000, "0a814078410353744e2947a8e9342e4e",
     "f213042b9b166b443b8a5a336a325ba4a2ae9482db369754932a7b3c802ed980",
     "Telematic/original/telematic.rom",
     "https://raw.githubusercontent.com/assinie/Telematic/master/original/telematic.rom",
     "TELEMATIC, dépôt assinie/Telematic (GitHub)", False),
    # Cartouche Atmos : ORIC EXTENDED BASIC V1.1 (ROM d'oric.uf2, en-tête C de
    # reload-emulator, non versionné dans ce dépôt : pas d'URL). Proposée par le
    # menu, en banque 7 pour démarrer en mode Atmos (lecteur de cassette).
    ("telestrat_atmos", 0x4000, 0x0000, "a330779c42ad7d0c4ac6ef9e92788ec6",
     "ed28568574716eef5d7c0fde2568d7a47a6e4b1fbca81daff3be05e45723466d",
     "Phosphoric/roms/basic11b.rom", None,
     "ORIC EXTENDED BASIC V1.1 (basic11b.rom), dépôt local Phosphoric/roms (pas d'URL ; origine de l'image non documentée)", True),
    # Cartouche STRATORIC (manuel du développeur Telestrat, F. Broche 1987) :
    # banque 7 SEDORIC + démarrage, banque 6 ORIC BASIC V1.1, banque 5 ORIC
    # BASIC V1.0 — mode Atmos avec disquettes SEDORIC et cassettes
    ("telestrat_stratoric", 0x4000, 0x0000, "19c56dfcab72a082f449d2bc6ec15032",
     "88446367807254eafea8fc048a12c90aa19ea2f3347ce37c467a460926d7f5b3",
     "stratoric/B7STRA40.ROM",
     "https://raw.githubusercontent.com/jedeoric/stratoric/master/B7STRA40.ROM",
     "STRATORIC 4.0 (B7STRA40.ROM), dépôt jedeoric/stratoric (GitHub)", True),
    # ORIC BASIC V1.0 (Oric-1) : copie locale dans le dépôt Phosphoric (roms/basic10.rom,
    # MD5 identique), aucune URL publique connue ; sinon depuis un en-tête déjà généré
    ("telestrat_basic10", 0x4000, 0x0000, "ebe418ec8a6c85d5ac32956c9a96c179",
     "83d341dab126092f7a6d56f6a92bca0643ff3aa6529d896085004f958c9be2ff",
     "Phosphoric/roms/basic10.rom", None,
     "ORIC BASIC V1.0 (basic10.rom), dépôt local Phosphoric/roms (pas d'URL ; origine de l'image non documentée)", True),
]

DEFAUT_SORTIE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "roms", "telestrat_roms.h")


def octets_de_tableau_c(texte):
    """Octets 0x.. d'un texte C, dans l'ordre."""
    return bytes(int(x, 16) for x in re.findall(rb"0x([0-9A-Fa-f]{2})", texte))


def verifie(nom, data, taille, md5, sha256):
    """Retourne None si l'image est conforme, sinon le motif du refus."""
    if len(data) != taille:
        return f"taille {len(data)} (attendu {taille})"
    got = hashlib.md5(data).hexdigest()
    if got != md5:
        return f"md5 {got} (attendu {md5})"
    if sha256 and hashlib.sha256(data).hexdigest() != sha256:
        return "sha256 différent de celui attendu"
    return None


# Archives locales (relatives à ~) : (archive, membre) essayés après les clones ;
# l'image n'est acceptée que si taille, MD5 et SHA-256 sont ceux attendus. Les
# autres membres de ces archives (autres versions) sont ignorés par ce contrôle.
ARCHIVES = {
    "telestrat_telemon24": [("oriclib/games/zip/telemon.zip", "telemon-2.4.rom")],
    "telestrat_hyperbas": [("oriclib/games/zip/HyperBasic.zip", "HyperBasic")],
    "telestrat_teleass": [("oriclib/games/zip/teleass.zip", "teleass.rom")],
}


def depuis_archive(nom, taille, md5, sha256):
    """(octets, source) ou (None, [motifs de refus]) depuis les archives locales."""
    refus = []
    for rel, membre in ARCHIVES.get(nom, []):
        chemin = os.path.join(HOME, rel)
        if not os.path.isfile(chemin):
            continue
        try:
            with zipfile.ZipFile(chemin) as z:
                info = z.getinfo(membre)
                if info.file_size > MAX_TELECHARGEMENT:
                    refus.append(f"{chemin}:{membre} : trop gros")
                    continue
                data = z.read(info)
        except (OSError, KeyError, zipfile.BadZipFile) as e:
            refus.append(f"{chemin}:{membre} : {e}")
            continue
        motif = verifie(nom, data, taille, md5, sha256)
        if motif is None:
            return data, f"archive locale {rel}"
        refus.append(f"{chemin}:{membre} : {motif}")
    return None, refus


def candidats_locaux(rel):
    return [os.path.join(r, rel) for r in RACINES]


def depuis_entete_genere(chemin, nom):
    """Banque `nom` d'un en-tête déjà généré (régénération sans source d'origine)."""
    try:
        with open(chemin, "r", encoding="utf-8") as f:
            texte = f.read()
    except OSError:
        return None
    m = re.search(r"\b" + re.escape(nom) + r"\[0x4000\] = \{(.*?)\};", texte, re.S)
    if not m:
        return None
    banque = octets_de_tableau_c(m.group(1).encode())
    return banque if len(banque) == 0x4000 else None


def charge(rom, offline, ancien):
    """(octets de l'image, description de la source) ou (None, raison)."""
    nom, taille, decalage, md5, sha256, rel, url, _prov, _opt = rom
    essais = []
    for chemin in candidats_locaux(rel):
        if not os.path.isfile(chemin):
            continue
        with open(chemin, "rb") as f:
            data = f.read()
        if chemin.endswith(".h"):  # tableau C : les 16 premiers Ko
            data = octets_de_tableau_c(data)[:0x4000]
        motif = verifie(nom, data, taille, md5, sha256)
        if motif is None:
            return data, "copie locale"
        essais.append(f"{chemin} : {motif}")
    data, src = depuis_archive(nom, taille, md5, sha256)
    if data is not None:
        return data, src
    essais.extend(src)
    for entete in ancien:
        banque = depuis_entete_genere(entete, nom)
        if banque is not None:
            data = banque[decalage:decalage + taille]
            motif = verifie(nom, data, taille, md5, sha256)
            if motif is None:
                return data, "en-tête déjà généré"
            essais.append(f"{entete} : {motif}")
    if url and not offline:
        try:
            with urllib.request.urlopen(url, timeout=30) as r:
                data = r.read(MAX_TELECHARGEMENT + 1)
        except OSError as e:
            essais.append(f"{url} : {e}")
        else:
            motif = verifie(nom, data, taille, md5, sha256) if len(data) <= MAX_TELECHARGEMENT else "trop gros"
            if motif is None:
                return data, "téléchargée"
            essais.append(f"{url} : {motif}")
    elif url:
        essais.append("téléchargement désactivé (--offline)")
    else:
        essais.append("aucune URL connue")
    return None, "; ".join(essais) if essais else "aucune source"


def macro_presence(nom):
    return "TELESTRAT_HAS_" + nom.removeprefix("telestrat_").upper()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--offline", action="store_true", help="n'utiliser que les copies locales et l'en-tête déjà généré")
    ap.add_argument("--out", default=DEFAUT_SORTIE)
    args = ap.parse_args()
    sortie = os.path.normpath(os.path.abspath(args.out))

    lignes = ["#pragma once", "",
              "// Généré par tools/fetch_roms.py : ne pas modifier, ne pas versionner.",
              "// Droits sur ces ROM non établis par ce dépôt : provenance et réserves dans docs/ROMS.md.",
              "#include <stdint.h>", "",
              "// Emplacement des images : en flash par défaut (le firmware les copie dans ses",
              "// emplacements de banque en RAM) ; TELESTRAT_ROM_SECTION(nom) les place ailleurs.",
              "// Tableaux static const : une définition par unité de compilation, sans conflit d'édition de liens.",
              "// TELESTRAT_HAS_<NOM> : 1 si l'image est celle de la ROM, 0 si absente (tableau à zéro).",
              "#ifndef TELESTRAT_ROM_SECTION", "#define TELESTRAT_ROM_SECTION(x)", "#endif", ""]
    manquantes = []
    # En-têtes déjà générés, sources de repli : la sortie, puis celui du dépôt
    anciens = [sortie]
    if os.path.normpath(DEFAUT_SORTIE) != sortie:
        anciens.append(os.path.normpath(DEFAUT_SORTIE))
    for rom in ROMS:
        nom, taille, decalage, md5, sha256, _rel, _url, prov, optionnelle = rom
        data, source = charge(rom, args.offline, anciens)
        macro = macro_presence(nom)
        if optionnelle:
            lignes.append("#ifndef TELESTRAT_ROMS_CLE")
        if data is None:
            if not optionnelle:
                manquantes.append(f"{nom} : {source}")
                continue
            print(f"{nom:22s} ABSENTE (optionnelle) : {source}", file=sys.stderr)
            lignes.append(f"// {prov} : ROM ABSENTE à la génération ({md5} attendu), tableau à zéro")
            lignes.append(f"#define {macro} 0")
            banque = bytearray(0x4000)
            ligne_hash = None
        else:
            banque = bytearray(0x4000)
            for base in range(0, 0x4000, taille):
                banque[base:base + taille] = data
            ligne_hash = f"md5 {md5}" + (f", sha256 {sha256}" if sha256 else "")
            dup = " ; recopiée dans chaque tranche de la banque" if taille < 0x4000 else ""
            lignes.append(f"// {prov} ({ligne_hash}, {taille} octets, en +${decalage:04X} dans la banque{dup})")
            lignes.append(f"#define {macro} 1")
            print(f"{nom:22s} <- {source}")
        lignes.append(f"static const uint8_t TELESTRAT_ROM_SECTION(\"{nom}\") {nom}[0x4000] = {{")
        for i in range(0, 0x4000, 16):
            lignes.append("    " + ", ".join(f"0x{b:02X}" for b in banque[i:i + 16]) + ",")
        lignes.append("};")
        if optionnelle:
            lignes.append("#else")
            lignes.append(f"#define {macro} 0")
            lignes.append("#endif")
        lignes.append("")
    if manquantes:
        sys.exit("ROM obligatoire(s) introuvable(s), rien écrit :\n  " + "\n  ".join(manquantes)
                 + "\nPlacer les clones (voir docs/ROMS.md) ou lancer sans --offline.")

    # Écriture atomique : fichier temporaire du même répertoire, puis renommage
    os.makedirs(os.path.dirname(sortie), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(sortie), prefix=".telestrat_roms.", suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write("\n".join(lignes))
        os.replace(tmp, sortie)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise
    print(f"écrit : {sortie}")


if __name__ == "__main__":
    main()
