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

## Sprint 3 — « Télématique »

| US | Récit |
|---|---|
| US-20 | ACIA 6551 : horloge interne, IRQ émission/réception |
| US-21 | Liaison vers l'UART de l'UEXT ou le PicoWiFiModemUSB (commandes AT) |
| US-22 | TELEMATIC en serveur Minitel ; sortie Videotex |

## Plus tard

- Essai et réglage sur carte (temps de bus, son, DVI, accès USB).
- Images disque intégrées en flash (lecture seule), lecteurs B à D depuis la clé.
- Imprimante vers l'UART ou la clé USB sur le Neo6502.
- Instantanés (savestates), sélecteur de ROM au démarrage.
- Test « golden » image contre Oricutron (PPM), ROM ORIX.

## Définition de « terminé »

`make` passe (tests unitaires + démarrage), `make uf2` compile les deux
variantes, documentation et CHANGELOG local à jour, commit signé bmarty.
