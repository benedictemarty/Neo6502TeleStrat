# ROM embarquées : provenance et droits

Ce dépôt **ne contient aucune ROM** : `src/roms/telestrat_roms.h` est généré par
`tools/fetch_roms.py` (listé dans `.gitignore`, jamais versionné).

## Droits

**Droits non établis par ce dépôt.** Ces images sont des œuvres de tiers ; le dépôt
n'en détient aucune licence, n'en affirme aucune et n'établit pas qui en est
l'ayant droit. La licence zlib/libpng du code (en-têtes des sources) ne s'applique
pas à elles. Ne pas redistribuer `telestrat_roms.h`, un `.uf2` qui l'embarque ni les
ROM elles-mêmes sans avoir établi, par ailleurs, le droit de le faire. Les noms
d'auteurs ou de sociétés cités dans d'anciens commentaires du projet n'ont pas été
vérifiés ici.

## Provenance de chaque ROM

La provenance est celle déclarée dans `tools/fetch_roms.py` (la source utilisée n'est
plus écrite en clair dans l'en-tête généré : aucun chemin de machine). Les sommes
MD5 sont celles que le script exige ; les SHA-256 ont été calculés sur les octets
de `telestrat_roms.h` dont le MD5 était le bon, les fichiers d'origine n'étant pas
présents sur cette machine (sauf `oric_roms.h`).

| Tableau C | Contenu | Source déclarée | Taille | MD5 | SHA-256 |
|---|---|---|---|---|---|
| `telestrat_telemon24` | TELEMON 2.4 | GitHub `jedeoric/telemon`, `original/telemon.rom` | 16 Ko | `9a432244d9ee4a49e8ddcde64af94e05` | `0ed991887342fba9e4b71668ff4c14ed93c2b3a19b4874dd0282404a1c442094` |
| `telestrat_hyperbas` | HYPER-BASIC | GitHub `assinie/Hyper-Basic`, `original/hyperbas.rom` | 16 Ko | `364bf095e0dc4222d75354d50b8cddfc` | `08eaf63a55a7659a6da7c91cf4a57bb0e2e21ff856d0957928605aa5feed09c6` |
| `telestrat_teleass` | TELE-ASS | GitHub `jedeoric/tele-ass`, `original/teleass.rom` | 16 Ko | `2324c9cc227c1327a72a667c97ed2990` | `49921a9f932f7ecc2947b9b08f0451a393dc48f55317209c44338dc8bcf227ee` |
| `telestrat_telematic` | TELEMATIC (cartouche) | GitHub `assinie/Telematic`, `original/telematic.rom` | 8 Ko | `0a814078410353744e2947a8e9342e4e` | `f213042b9b166b443b8a5a336a325ba4a2ae9482db369754932a7b3c802ed980` |
| `telestrat_atmos` | ORIC EXTENDED BASIC V1.1 | `roms/basic11b.rom` du dépôt local Phosphoric (pas d'URL ; origine de l'image non documentée) | 16 Ko | `a330779c42ad7d0c4ac6ef9e92788ec6` | `ed28568574716eef5d7c0fde2568d7a47a6e4b1fbca81daff3be05e45723466d` |
| `telestrat_stratoric` | STRATORIC 4.0 (`B7STRA40.ROM`) | GitHub `jedeoric/stratoric`, `B7STRA40.ROM` | 16 Ko | `19c56dfcab72a082f449d2bc6ec15032` | `88446367807254eafea8fc048a12c90aa19ea2f3347ce37c467a460926d7f5b3` |
| `telestrat_basic10` | ORIC BASIC V1.0 (Oric-1) | `roms/basic10.rom` du dépôt local Phosphoric (pas d'URL ; origine de l'image non documentée) | 16 Ko | `ebe418ec8a6c85d5ac32956c9a96c179` | `83d341dab126092f7a6d56f6a92bca0643ff3aa6529d896085004f958c9be2ff` |

TELEMATIC occupe 8 Ko (`$E000-$FFFF`, vecteur RESET en `$E000`) : l'image est recopiée
dans les **deux moitiés** de la banque de 16 Ko (hypothèse : la ligne d'adresse A13
n'est pas décodée par la cartouche ; non mesuré sur le matériel).

## Obligatoires et optionnelles

- **Obligatoires** : TELEMON, HYPER-BASIC, TELE-ASS, TELEMATIC. Sans l'une d'elles,
  `fetch_roms.py` s'arrête sans rien écrire.
- **Optionnelles** (menu : `@stratoric`, `@atmos`, `@basic10`) : STRATORIC, Atmos,
  Oric-1. Sans source, le tableau est généré **à zéro** et la macro
  `TELESTRAT_HAS_STRATORIC` / `TELESTRAT_HAS_ATMOS` / `TELESTRAT_HAS_BASIC10` vaut
  `0` (elle vaut `1` quand l'image est celle de la ROM ; `0` aussi quand
  `TELESTRAT_ROMS_CLE` retire ces cartouches de la flash). Les tableaux gardent leur
  taille pour que le firmware et les tests compilent ; **`src/osd/rom_builtin.h` ne
  teste pas encore ces macros** : une ROM absente reste proposée par le menu et se
  comporte comme une cartouche à zéro (docs/CRITIQUE.md, O-02).

## Régénération

```sh
make roms                     # python3 tools/fetch_roms.py --out src/roms/telestrat_roms.h
python3 tools/fetch_roms.py --offline   # sans réseau
```

Sources essayées dans l'ordre, chaque image étant vérifiée (taille, MD5, SHA-256) :

1. clone local `~/Projets/<dépôt>/…` puis `~/<dépôt>/…` (chemins relatifs dans
   `tools/fetch_roms.py`) ;
2. l'en-tête déjà généré (`src/roms/telestrat_roms.h` ou la sortie demandée) : permet de
   régénérer sans réseau ni clones ;
3. téléchargement HTTPS (taille limitée à 1 Mo, délai de 30 s), sauf `--offline`.

L'écriture est atomique (fichier temporaire du même répertoire, puis renommage) : un
échec ne laisse pas un en-tête tronqué. `make` ne régénère l'en-tête que s'il est
absent (modifier le script ne le déclenche pas) ; `make roms` force la régénération.

Les tableaux sont `static const` (une copie par unité de compilation, sans définition
multiple à l'édition de liens) ; `TELESTRAT_ROM_SECTION(nom)` permet au firmware de
les placer ailleurs qu'en flash.
