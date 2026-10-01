# ADR-01 — Socle reload épinglé, écarts aux règles communes

- **Date** : 2026-09-30 (v0.16.12)
- **État** : accepté
- **Contexte commun** : `docs/STRATEGIE-NEO6502.md` de reload-emulator
  (commit `4480c51`), qui demande un ADR pour tout écart à ses règles (section 3).

## Contexte

Le Telestrat compile contre reload-emulator (`RELOAD_DIR`). Jusqu'à la
v0.16.11, c'était la tête de `~/reload-emulator`, qui bouge plusieurs fois par
jour. Le 2026-09-30, un changement de reload (`2c35d91` : `audio.c` définit
`hid_media_key_down`) a cassé l'édition des liens du firmware Telestrat, sans
que personne ne le voie avant le build suivant.

## Décision 1 — socle épinglé

Le projet compile contre une **étiquette** posée par la session reload-emulator
après vérification (`socle-AAAA-MM-JJ` : tests PC de reload, ses firmwares et
les deux variantes du Telestrat compilés). `tools/reload_socle.sh` en fait un
clone local dans `~/.cache/reload-socle/<étiquette>` (sans réseau, sous-modules
repris de `~/reload-emulator`, qui n'est pas modifié) ; le `Makefile`
(`RELOAD_SOCLE`) et `platforms/rp2040/CMakeLists.txt` le prennent par défaut.

Changer d'étiquette est une décision du projet : nouvelle valeur de
`RELOAD_SOCLE`, `make` entier, deux variantes compilées, `make charge`, puis une
version. `RELOAD_DIR=~/reload-emulator` reste possible pour essayer la tête.

Écarté : `git worktree` dans `~/reload-emulator` (proposé par reload) —
fonctionne aussi, mais écrit dans le dépôt d'un autre projet.

## Décision 2 — écarts aux règles communes

| Règle (stratégie, section 3) | Écart | Raison | Fin prévue |
|---|---|---|---|
| ~~7 : `CFG_TUH_ENUMERATION_BUFSIZE` 512~~ | ~~256 dans la variante RAM 64 Ko~~ | levé en v0.16.20 : 512 dans les deux variantes (place regagnée par les composants du socle) | fait |
| 6 : bus du W65C02 en PIO par défaut | pilote SIO/GPIO intégré au tick (`platforms/rp2040/src/neo6502_bus.h`) | éprouvé sur carte (v0.4.1 à v0.16.4), charge mesurée par `make charge` | étape 5 du plan (le Telestrat reprend le pilote du socle) |

## Suivi

- v0.16.12 : `socle-2026-09-30`.
- v0.16.14 : `socle-2026-09-30-4` (cassette, puces identiques et `osd.h`
  pris dans le socle, copies supprimées ; `make uf2` recrée `build/rp2040`
  quand le socle change, témoin `.reload_dir`).
- v0.16.15 : `socle-2026-09-30-5` (WD1793 et `oric_dsk.h` du socle, notre
  WD1793 supprimé ; DRQ corrigé dans le socle à notre demande ; `OSD_NOINLINE` ;
  cœur 65C02 commun avec la NES).
- v0.16.16 : `socle-2026-10-01` (`ay38910psg.h`, `hid_app.c` du socle ; WD1793 :
  `$80` sans disque en type II/III, fiche FD179X ; cadence réelle du PWM).
- v0.16.17 : `socle-2026-10-01-2` (`AY38910_HOT` : l'AY en flash sans
  `push_macro`).
- v0.16.18 : `socle-2026-10-01-4` (AY : aigus justes, périodes < 8 à
  mi-volume ; `audio.c` : plus de `memset` en flash sur le cœur 1, verrou
  réservé pour la section critique du son). Charge : 59 / 67 % (+1 point).
- v0.16.20 : écart à la règle 7 levé, tampon d'énumération USB de 512 octets
  aussi en RAM 64 Ko (976 octets de marge au-delà du tas).
- v0.16.21 : `socle-2026-10-01-6` ; montage de la clé par `msc_app.c` du
  socle (`MSC_VOLUMES=1`), notre `usb_msc.c` retiré ; `carte.py cle` lit
  `msc_slot_addr[0]`.

## Conséquences

- Une modification de reload n'arrive au Telestrat que par un changement
  d'étiquette, vérifié ici.
- Un correctif du socle (ex. verrous DVI, v0.16.11) doit être repris en changeant
  d'étiquette, ou recopié en attendant ; dans ce cas, le noter dans le CHANGELOG.
- Les copies figées (`src/chips/`, `hid_app.c`) restent jusqu'à la décision de
  bmarty (fusion dans reload ou projet séparé consommateur du socle).
