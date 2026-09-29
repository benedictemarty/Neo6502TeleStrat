# Références

| Source | Usage |
|---|---|
| [Oricutron](https://github.com/pete-gordon/oricutron) (rev. 002279f) — `machine.c`, `via.c`, `disk.c`, `6551.c`, `joystick.c`, `8912.c` | décodage `$03xx`, banques, Microdisc, ACIA, joysticks, matrice clavier ; oracle de démarrage |
| [reload-emulator](https://github.com/benedictemarty/reload-emulator) (fork de vsladkov) | base du portage : puces, `oric.h`, `oric.c`, plate-forme RP2040 |
| Notice « Extension RAM 64 Ko pour Oric Telestrat », F. Broche, ORIC International, 1987 ([PDF, ceo.oric.org](https://ceo.oric.org/wp-content/uploads/wpforo/default_attachments/1773852840-Extensiontelestrat64K.pdf)) | carte des banques, V2DRA `$0321`, état des banques `$0200-$0207`, EXBNK |
| [jedeoric/telemon](https://github.com/jedeoric/telemon) | ROM TELEMON 2.4 (`original/telemon.rom`) |
| [assinie/Hyper-Basic](https://github.com/assinie/Hyper-Basic) | ROM HYPER-BASIC 2.0b, source commentée |
| [jedeoric/tele-ass](https://github.com/jedeoric/tele-ass) | ROM TELE-ASS |
| [assinie/Telematic](https://github.com/assinie/Telematic) | ROM TELEMATIC 2.0b (8 Ko, `$E000`), source commentée |
| [assinie/STRATSED](https://github.com/assinie/STRATSED) | STRATSED V2.0E (image de banque 0), source commentée |
| `STRATSED.DSK` (archive locale `~/oriclib/games/dsk`) | disquette système STRATSED V2.0c, tests de démarrage |
| Fiche technique WD1793 (Western Digital) | commandes, registre d'état, formatage `$F5`-`$F7` |
| STUM 1B, « Spécifications Techniques d'Utilisation du Minitel 1B », PTT/Télétel 1986 — transcription relue de jbellue ([github.com/jbellue/stum1b](https://github.com/jbellue/stum1b)), copie locale `~/Neo6502NeoTel/docs/ref/STUM1B.txt` | séquences PRO1 (OPPO, CONNEXION, DECONNEXION), SEP `$50`/`$53`/`$59`, délais de connexion |
| [OricTel](https://github.com/benedictemarty/orictel), `~/Neo6502NeoTel` (NeoTel v1.0.0) | protocole Videotex côté terminal ; NeoTel pourra servir de correspondant Minitel réel pour TELEMATIC |
| Fiche technique 6551 (ACIA) | registres, débits, trames, interruptions |
| TELEMON 2.4 désassemblé (da65 + `roms/telmon24.sym` d'Oricutron) | XRING `$EEA5`, XLIGNE `$EF20`, XDECON `$EF3F`, routine série `$C8C0`, détection des banques `$C2F4`, prises `$DB3A`/`$DB5D` |
| `~/picowifi/PicoWiFiModemUSB/README.md` | commandes AT du modem (`ATA`, `ATS0`, `AT$SP`, RING) |
| `~/Neo6502NeoTel` | branchement du PicoWiFiModemUSB (USB CDC) sur le Neo6502 |
| `~/Téléchargements/telestrat_*.pdf` (schémas de la carte mère, « Telestrat à cœur ouvert », « Système ») | à exploiter aux sprints 2-3 (FDC, ACIA) |
| `olimex_neo6502.h` du pico-sdk ; `serial.cpp` du firmware officiel du Neo6502 (Paul Robson) | UART0 de l'UEXT : TX GPIO 28, RX GPIO 29 |
| `hid_app.c` et `msc_app.c` de reload-emulator | codes des touches (ASCII, sinon code HID \| 0x100) ; montage de la clé (FatFs, LUN 0) |
| `dvi_timing.c` de PicoDVI | 960x544 : 1104 pixels par ligne à 37,2 MHz (budget d'une ligne de tampon : 59,35 µs) |
| unscii-8 de Viznut, <http://viznut.fi/unscii/> (domaine public), copie `tools/fonts/unscii-8.hex` | texte du menu (ASCII, Latin-1) ; comparée à font8x8 de Daniel Hepper (domaine public) et à une police dessinée pour le projet |
| Oricutron `tape.h`, `tape.c`, `roms/basic11b.pch` (GPL v2, lus seulement) | durées du format cassette (208 / 416 cycles), trame d'octet, synchro et silence d'en-tête ; adresses des routines cassette de BASIC 1.1 (`$E6C9`, `$E71C`, `$E735`) — aucune ligne de code reprise |
| Wikipédia, Defence-Force (site matériel Oric), documentation cc65 — transmis par le PO | prise cassette DIN du Telestrat identique à l'Atmos ; chargement par la cartouche Atmos |
| `oric_roms.h` de reload-emulator (local) | ROM ORIC EXTENDED BASIC V1.1 (md5 `a330779c42ad7d0c4ac6ef9e92788ec6`) |
| `AIGLE.TAP` (« L'Aigle d'Or », Loriciels 1984), `~/Téléchargements` | essai manuel de chargement |
| ROM BASIC 1.1 désassemblée (da65) | écriture d'un octet `$E65E` (trame de 13 bits, parité), lecture d'un bit `$E71C` |
| Oricutron 1.2.0 (rev. 002279f, `~/oricutron`) avec `tools/oracle/oricutron-dump.patch` | oracle sans fenêtre (écran texte après N trames) : STRATSED sans TELE-ASS |
| [jedeoric/stratoric](https://github.com/jedeoric/stratoric) (`B7STRA40.ROM`, md5 `19c56dfcab72a082f449d2bc6ec15032`) | cartouche STRATORIC 4.0 (banque 7) |
| `~/Oric1/roms/basic10.rom` (local, md5 `ebe418ec8a6c85d5ac32956c9a96c179`) | ORIC BASIC V1.0 (banque 5 de STRATORIC) |
| Manuel du développeur Telestrat (F. Broche, ORIC International 1987), `~/Téléchargements/manuel_developpeur_telestrat.pdf` — fourni par le PO | répartition des banques par cartouche (page 3), entête de banque `$FFF8`-`$FFFF` (bit 4 : ignorer) |
| « Telestrat à cœur ouvert », « Telestrat, le système m'était conté » (`~/Téléchargements`) — fournis par le PO | matériel et système (lus par OCR, tesseract fra) ; pages 1-7 à 1-9 du second : détection des banques, RESET à chaud par défaut |
| Source commentée d'HYPER-BASIC (`FLGTEL` bit 2 = BONJOUR.COM, `$FFAC`) | appel de la banque 5 au démarrage à froid |
| « FX Series Printer User's Manual » (Epson, 1983), [files.support.epson.com/pdf/fx80__/fx80__u1.pdf](https://files.support.epson.com/pdf/fx80__/fx80__u1.pdf) | codes de la FX-80 (annexes B, C, D : Master Select, densités graphiques `ESC *` 0-6, `ESC ^`) |
| Manuel de la table traçante Oric MCP-40, [manualslib.com/manual/1202312/Oric-Mcp-40.html](https://www.manualslib.com/manual/1202312/Oric-Mcp-40.html) (pages lues par OCR) | 480 pas de 0,2 mm, codes du mode texte, commandes A C D H I J L M P Q R S X, table des plumes ; contradictions et manques notés dans `plotter_mcp40.h` |
| Table `ESC R` usuelle de l'ESC/P (jeu France : à ° ç § é ù è ¨) | absente de l'OCR du manuel FX-80 : reprise de la table ESC/P connue, à vérifier sur une FX-80 |
| FatFs R0.15 w/patch1 (ChaN, http://elm-chan.org/fsw/ff/), copié de reload-emulator dans `third_party/fatfs` | système de fichiers de la clé ; seul `ffconf.h` modifié |
| TinyUSB 0.21.0 ([hathach/tinyusb](https://github.com/hathach/tinyusb), sous-module `third_party/tinyusb`) | hôte USB du RP2040 : clé et clavier ensemble (#3533, #3561) |
| TELEMON 2.4, source (`~/telemon`, `src/telemon.asm` `$CA2F`, `xtstlp.asm`) | impression par interruption CA1 : l'octet suivant n'est envoyé qu'à l'ACK |
| « Oric MCP40 imprimante couleur, manuel d'utilisation » (Oric, en français), `~/Téléchargements` — fourni par le PO | origine en marge gauche sous la plume en entrant en mode graphique ; même contradiction sur les couleurs que le manuel anglais |
| « TRS-80 Color Graphic Plotter CGP-115 Operation Manual » (Radio Shack 26-1192), [colorcomputerarchive.com](https://colorcomputerarchive.com/repo/Documents/Manuals/Hardware/CGP-115%20(Tandy).pdf), aussi [archive.org](https://archive.org/details/cgp-115_operation_manual) — même mécanisme que la MCP-40 (indiqué par le PO) ; lu par OCR | couleurs C0-C3 noir, bleu, vert, rouge ; taille S gardée en mode texte ; commutateurs DIP (40/80 colonnes, CR seul ou CR + LF) ; autotest (figure 12) : interligne ≈ 2 largeurs de caractère |
