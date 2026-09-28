# Architecture

## Principe

Même schéma que `oric.uf2` de reload-emulator : sur le Neo6502, le **W65C02S
réel** fait tourner le logiciel ; à chaque cycle, le RP2040 lit l'adresse et
R/W sur le bus (`chips/wdc65C02cpu.h` de reload), répond ou capte la donnée,
puis fait avancer les périphériques émulés. Sur PC, le même système tourne
avec le cœur W65C02S cycle à cycle de reload (`chips/w65c02cpu.h`, validé par
les tests de Klaus Dormann) : c'est le banc de test.

```
src/systems/telestrat.h     système (format header-only de reload)
src/devices/telestrat_fdc.h Microdisc intégré (WD1793 + contrôle $0314/$0318)
src/devices/mos6551acia.h   ACIA 6551
src/roms/telestrat_roms.h   généré par tools/fetch_roms.py (non versionné)
platforms/pc/               banc sans écran (tests)
platforms/rp2040/           firmware telestrat.uf2
```

Dépendances reprises de reload-emulator sans copie : `mos6522via.h`,
`ay38910psg.h`, `kbd.h`, `clk.h`, `chips_common.h`, cœurs 65C02, `hid_app.c`,
`audio.c`, `utils.S` (rendu 3x de l'Oric), SDK Pico, PicoDVI, tinyusb.

## Carte mémoire

| Adresses | Contenu | Source |
|---|---|---|
| `$0000-$02FF` | RAM | |
| `$0300-$030F` | VIA 1 : clavier (PB0-2, PB3), AY (PA, CA2, CB2), imprimante | comme l'Atmos |
| `$0310-$0313` | WD1793 | Oricutron `disk.c` |
| `$0314` | écriture : INTENA (b0), face (b4), lecteur (b5-6) ; lecture : /INTRQ (b7) | Oricutron `disk.h` |
| `$0318` | lecture : /DRQ (b7) | Oricutron |
| `$031C-$031F` | ACIA 6551 | Oricutron `machine.c` |
| `$0320-$032F` | VIA 2 : PA0-2 = banque (V2DRA `$0321`), PB = joysticks | notice Extension RAM 64 Ko, IV-2 ; Oricutron `via.c`, `joystick.c` |
| autres `$03xx` | reflet du VIA 1 | Oricutron |
| `$0400-$BFFF` | RAM (écran texte `$BB80`, HIRES `$A000`) | |
| `$C000-$FFFF` | banque 0..7 | |

## Banques (notice « Extension RAM 64 Ko », chap. IV-1)

| Banque | Port | Contenu | Config. `standard` | Config. `ram64k` |
|---|---|---|---|---|
| 7 | gauche | TELEMON (banque au RESET) | TELEMON 2.4 | TELEMON 2.4 |
| 6 | gauche | HYPER-BASIC ou FORTH | HYPER-BASIC | HYPER-BASIC |
| 5 | gauche | inutilisée | vide | vide |
| 4 | les deux | inutilisée / extension RAM | vide | RAM |
| 3 | droit | TELEMATIC ou extension RAM | TELEMATIC | RAM |
| 2 | droit | TELE-ASS ou extension RAM | TELE-ASS | RAM |
| 1 | droit | extension RAM | vide | RAM |
| 0 | interne | RAM interne (STRATSED) | RAM | RAM |

- Sélection : `(banque & ~DDRA) | (ORA & DDRA)` sur les bits 0-2 ; une ligne en
  entrée garde sa valeur (comme `via_tele_w_iora` d'Oricutron). Au RESET : banque 7.
- Écriture en ROM ou en banque vide : ignorée. Lecture d'une banque vide :
  `$FF` (**hypothèse** : bus flottant non modélisé).
- TELEMATIC (8 Ko) : répétée dans les deux moitiés de sa banque (**hypothèse** :
  A13 non décodée) ; TELEMON la compte bien pour 8 Ko (« 56 Ko ROM »).
- TELEMON écrit l'état des banques en `$0200-$0207` (`$0F` = RAM), ce que
  vérifient les tests.

## Choix et écarts

| Sujet | Choix | Raison |
|---|---|---|
| Accès aux banques | pointeurs directs, sans `mem.h` | ROM sans pointeur d'écriture ; plus rapide ; économise la RAM du RP2040 |
| RAM du RP2040 | variante `standard` (1 banque RAM, 4 ROM) : 178 Ko ; `ram64k` (5 RAM, 2 ROM) : 210 Ko | les ROM restent en RAM (`__not_in_flash`) comme dans reload, pour tenir le temps de bus |
| FDC | sprint 1 : aucun disque, « non prêt » et INTRQ en fin de commande | suffisant pour TELEMON ; la lecture `.dsk` est au sprint 2 |
| ACIA | registres et effets de bord d'Oricutron, sans liaison | TELEMON teste l'ACIA au démarrage (avec `$FF` il part dans une routine RAM non installée) |
| FUNCT | touche Windows gauche | `hid_app.c` de reload ne remonte pas Alt en mode ASCII |
| Vidéo | reprise de `oric_screen_update` ; redessin forcé toutes les 32 trames | clignotement même sans écriture en mémoire écran |

Écart constaté avec Oricutron : sans disquette, Oricutron reste sur
« Drive:A-B-C-D » (son WD simule un disque présent, `MICRODISC_FUDGE`), alors que
ce portage affiche « Inserez une disquette ». À confronter au matériel réel.
