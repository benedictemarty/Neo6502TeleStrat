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
