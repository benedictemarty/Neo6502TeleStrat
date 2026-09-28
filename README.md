# Neo6502TeleStrat — Oric Telestrat pour Olimex Neo6502

> ⚠️ Avertissement : ce programme est un programme généré par Claude Code sous la supervision d'un être humain : il a été utilisé pour améliorer, développer, rendre compatible ou traduire ce logiciel.

Portage de l'**Oric Telestrat** (1986) sur l'**Olimex Neo6502**, sur le modèle
de `oric.uf2` de [reload-emulator](https://github.com/benedictemarty/reload-emulator) :
le **vrai W65C02S** du Neo6502 exécute TELEMON, le **RP2040** sert la mémoire
(8 banques de 16 Ko en `$C000-$FFFF`) et émule les périphériques (VIA 1 et 2,
AY-3-8912, Microdisc intégré, ACIA 6551, vidéo ULA en DVI). Livré en
`telestrat.uf2`, compatible avec le multi-boot du firmware Neo6502.

**Version : 0.2.0 (sprint 2).** Le Telestrat démarre sur disquette : TELEMON 2.4,
STRATSED V2.0c, HYPER-BASIC V2.0b et TELE-ASS, menu « Votre choix » identique à
Oricutron ; HYPER-BASIC calcule, liste le disque (`DIR`), sauvegarde et recharge
(`SAVE`/`LOAD`), imprime (`LPRINT`). Contrôleur WD1793 complet sur images
`MFM_DISK`, lues et écrites sur la clé USB du Neo6502. Firmware **compilé, non
encore essayé sur carte**.

## Construire

Prérequis : `~/reload-emulator` avec ses sous-modules (`pico-sdk`, `PicoDVI`,
`tinyusb`), `gcc`, `python3`, `cmake` et `arm-none-eabi-gcc` pour le firmware.

```sh
tools/fetch_roms.py     # ROM -> src/roms/telestrat_roms.h (non versionné)
make                    # banc PC + tests
make uf2                # build/rp2040/telestrat.uf2
cmake -S platforms/rp2040 -B build/rp2040-ram64k -DTELESTRAT_RAM64K=ON && make -C build/rp2040-ram64k telestrat
```

`RELOAD_DIR=...` change l'emplacement de reload-emulator. Pour un slot
multi-boot : `make uf2 NEO_MULTIBOOT_DIR=~/Neo6502firmware/multiboot NEO_SLOT_TELESTRAT=3`.

## ROM

Les ROM ne sont pas versionnées. `tools/fetch_roms.py` les prend dans les
clones locaux ou les télécharge, et vérifie leur MD5 :

| Banque | ROM | Source | MD5 |
|---|---|---|---|
| 7 | TELEMON 2.4 | [jedeoric/telemon](https://github.com/jedeoric/telemon/tree/master/original) | `9a432244…4e05` |
| 6 | HYPER-BASIC 2.0b | [assinie/Hyper-Basic](https://github.com/assinie/Hyper-Basic) | `364bf095…8cddfc` |
| 2 | TELE-ASS | [jedeoric/tele-ass](https://github.com/jedeoric/tele-ass) | `2324c9cc…2990` |
| 3 | TELEMATIC 2.0b (8 Ko en `$E000`) | [assinie/Telematic](https://github.com/assinie/Telematic) | `0a814078…2e4e` |

STRATSED (le DOS, chargé en banque 0) vient de la disquette système : aucun
binaire à fournir. Source commentée : [assinie/STRATSED](https://github.com/assinie/STRATSED).

Carte des banques : notice « Extension RAM 64 Ko pour Oric Telestrat »
(F. Broche, 1987), voir [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Utilisation (Neo6502)

Copier des images `.dsk` (format `MFM_DISK`, comme pour Oricutron) à la racine
d'une clé USB (FAT) : la première est insérée dans le lecteur A dès le montage,
et les écritures (`SAVE`…) sont réécrites dans le fichier, piste par piste.

| Touche | Effet |
|---|---|
| F1 | image suivante de la clé dans le lecteur A |
| F12 | RESET |
| F11 | NMI |
| Windows gauche | FUNCT |
| Pause | retour au firmware Neo6502 (multi-boot) |
| Manette USB | joystick droit (la 2ᵉ manette : joystick gauche) |

## Banc PC sans écran

```sh
build/telestrat_headless -c standard -f 300 -s -b     # écran texte + état des banques
build/telestrat_headless -c ram64k -p boot.ppm        # image 240x224
build/telestrat_headless -c oricutron -0 STRATSED.DSK -f 1200 -w 500 -t '1~~~~~~DIR\n' -s
build/telestrat_headless -0 a.dsk -W a2.dsk -P imprimante.txt ...   # disque réécrit, imprimante
```

Dans `-t`, `\n` tape RETURN et `~` fait une pause de 50 trames. Les tests disque
utilisent `STRATSED.DSK` (`STRATSED_DSK=...`, voir [docs/TESTS.md](docs/TESTS.md)).

Configurations : `standard` (notice), `ram64k` (cartouche RAM 64 Ko),
`oricutron` (défaut d'Oricutron, oracle de comparaison), `telemon` (seul).

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — matériel émulé, carte mémoire, choix
- [docs/BACKLOG.md](docs/BACKLOG.md) — backlog produit, sprints
- [docs/TESTS.md](docs/TESTS.md) — stratégie et inventaire des tests
- [docs/REFERENCES.md](docs/REFERENCES.md) — sources consultées

## Licence

zlib/libpng, comme reload-emulator (voir [LICENSE](LICENSE)). Les ROM restent la
propriété de leurs auteurs.
