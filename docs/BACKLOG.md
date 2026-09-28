# Backlog produit

Product owner : bmarty. Méthode : sprints courts, une version par sprint,
tests automatiques à chaque modification (`make`).

## Vision

Un Oric Telestrat complet sur le Neo6502 : TELEMON, STRATSED sur disquettes,
HYPER-BASIC, TELE-ASS, TELEMATIC en serveur Minitel par le modem Wi-Fi.

## Sprint 1 — v0.1.0 — « TELEMON démarre » ✅ (2026-09-28)

| US | Récit | État |
|---|---|---|
| US-01 | En tant qu'utilisateur, je vois la bannière TELEMON 2.4 au démarrage | ✅ banc PC |
| US-02 | La banque de `$C000` suit V2DRA (`$0321`), banque 7 au RESET | ✅ tests unitaires |
| US-03 | Les ROM et la RAM des banques suivent la notice Extension RAM 64 Ko | ✅ « 64 Ko RAM, 56 Ko ROM » |
| US-04 | Variante cartouche RAM 64 Ko | ✅ « 128 Ko RAM, 32 Ko ROM », `$0201-$0204` = `$0F` |
| US-05 | Firmware `telestrat.uf2` compilé, multi-boot possible | ✅ compilé, ⏳ essai sur carte |
| US-06 | Clavier Oric (dont ESC, FUNCT, CTRL+lettre) et joysticks par manette | ✅ code, ⏳ essai sur carte |

## Sprint 2 — v0.2.0 — « Disquettes » ✅ (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-10 | WD1793 complet (types I à IV, DRQ, INTRQ) sur images `.dsk` (MFM_DISK) | ✅ 24 tests unitaires FDC |
| US-11 | Images sur clé USB, lues et écrites piste par piste ; F1 = image suivante | ✅ code, ⏳ essai sur carte |
| US-12 | Démarrage STRATSED V2.0c puis HYPER-BASIC ; DIR, SAVE, LOAD | ✅ banc PC |
| US-13 | Même écran qu'Oricutron sur la même disquette | ✅ (« Votre choix: ») |
| US-14 | Imprimante sur le port parallèle (LPRINT) | ✅ banc PC (option `-P`) |

## Sprint 3 — v0.3.0 — « Télématique » ✅ (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-20 | ACIA 6551 : débit, trame, double tampon, IRQ émission/réception | ✅ 15 tests unitaires |
| US-21 | Minitel sur la prise de l'ACIA : PRO1, SEP `$50`/`$59`/`$53` selon la STUM 1B (v0.3.1), sonnerie sur CB1 | ✅ 16 tests unitaires |
| US-22 | TELEMATIC en serveur : appel, décroché, pages Videotex, touches, raccrochage | ✅ banc PC (test de bout en bout) |
| US-23 | Ligne sur PicoWiFiModemUSB (Hayes, USB CDC), réglée par TELESTRA.CFG | ✅ code + 15 tests (faux modem), ⏳ essai sur carte |
| US-24 | Aiguillage Minitel / RS232 par PA4 du VIA 2 | ✅ |
| US-25 | Banques vides = bus flottant (TELEMATIC démarre, banques `$10`) | ✅ |
| US-26 | Émulation Minitel (APLIC 1) en appel sortant | ⏳ ligne prête (`connect:`), scénario non testé |

## Plus tard

- Essai et réglage sur carte (temps de bus, son, DVI, accès USB, modem).
- Émulation Minitel (APLIC 1) : test de bout en bout en appel sortant.
- Modem sur l'UART de l'UEXT ; prise RS232 (PA4 = 1) vers une liaison réelle.
- Vérifier sur matériel la cadence de sonnerie (seule hypothèse Minitel restante).
- Essai croisé sur carte : TELEMATIC (Neo6502TeleStrat) appelé par NeoTel sur un second Neo6502.
- Images disque intégrées en flash (lecture seule), lecteurs B à D depuis la clé.
- Imprimante vers l'UART ou la clé USB sur le Neo6502.
- Instantanés (savestates), sélecteur de ROM au démarrage.
- Test « golden » image contre Oricutron (PPM), ROM ORIX.

## Définition de « terminé »

`make` passe (tests unitaires + démarrage), `make uf2` compile les deux
variantes, documentation et CHANGELOG local à jour, commit signé bmarty.
