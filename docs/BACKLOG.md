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
| US-26 | Émulation Minitel (APLIC 1) en appel sortant | ✅ v0.4.3 au banc (`tests/test_minitel_emul.sh`) ; carte à faire |

## Sprint 4 — v0.4.0 — « Tenir 1 MHz sur le RP2040 » ✅ (2026-09-29, mesure sans carte)

Départ : ~398 cycles M0+ par cycle 6502 pour un budget de 295 (150 %).
Arrivée : 170 cycles, charge 59-68 % en moyenne, 76 % au pire, 0 trame hors budget.

| US | Récit | État |
|---|---|---|
| US-30 | Puces de reload figées dans le projet (`src/chips`, 462372a) | ✅ |
| US-31 | Modèle de référence figé + rejeu exact (bus, IRQ, audio, série, image) | ✅ 2 traces, 0 différence |
| US-32 | Pas de 4 cycles au repos sautés et rattrapés, chemin court RAM/ROM | ✅ 314 → 102 cycles |
| US-33 | Pilote de bus intégré (même séquence GPIO que reload) | ✅ 84 → 68 cycles, ⏳ carte |
| US-34 | Rendu de l'écran par table | ✅ 1,35 → 0,92 Mcycle au pire |
| US-35 | Rendu de l'écran sur le cœur 1 | ⏳ pas nécessaire (cœur 0 à 45-62 % sur carte) |

## v0.4.1 — recette sur carte ✅ (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-40 | Outillage SWD : flasher, écran, clavier, mesure (`tools/carte.py`) | ✅ |
| US-41 | Affichage 3 plans 1 bpp, 960x544 à 372 MHz, priorité DMA (leçons du BBC) | ✅ 35 µs/ligne, 0 retard |
| US-42 | Disquette intégrée en flash (`TELESTRAT_FLASH_DISK`) | ✅ |
| US-43 | Maintien de la donnée sur le bus après une pause | ✅ corrigé (banque 3, titre) |
| US-44 | Clavier : SHIFT et CTRL combinés, RETURN | ✅ corrigé |
| US-45 | Clé USB, modem PicoWiFiModemUSB, son, manette | ⏳ matériel non branché lors de la recette |
| US-46 | TELEMATIC serveur sur carte (ligne de recette SWD à la place du modem) | ✅ v0.4.2 : appel, pages, ENVOI, MENU, raccrochage, second appel |
| US-47 | Bus : OE3 maintenu bas jusqu'après la descente de PHI2 (solution de la session BBC, reload dfc1584) au lieu du renvoi d'impulsion | ⏳ à évaluer sur carte |
| US-48 | Raccrochage du correspondant : TELEMATIC ne revient en attente qu'à son délai d'inactivité (banc et carte) ; vérifier sur la documentation si un autre signal (DCD de l'ACIA ?) est attendu | ✅ v0.4.3 : comportement d'origine — TELEMATIC termine sur `SEP $49` (Connexion/Fin), pas sur la perte de porteuse ; testé |

## Sprint 5 — v0.5.0 — « Prise RS232 » ✅ (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-50 | Prise RS232 du banc sur TCP (`-S`), essayée avec HYPER-BASIC | ✅ `SOUT`, `SSAVE`, `SLOAD`, `CONSOLE` (`tests/test_rs232.sh`) |
| US-51 | Prise RS232 du Neo6502 sur l'UART0 de l'UEXT, au format programmé dans l'ACIA (`rs232=uext`) | ✅ code + tests du format, ⏳ essai sur carte |
| US-52 | Prise RS232 vers le PicoWiFiModemUSB (défaut), partagé avec la prise Minitel selon PA4 (`modem_mux.h`) | ✅ v0.5.1 code + 13 tests, ⏳ essai sur carte |

## Sprint 6 — v0.6.0 — « Menu : disquettes et cartouches » ✅ au banc (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-60 | Menu à l'écran (F1), moderne : police originale, panneaux tramés, sélecteur de fichiers | ✅ banc PC (captures), ⏳ carte |
| US-61 | Disquettes des lecteurs A à D depuis la clé, noms longs, une image par lecteur | ✅ banc, ⏳ carte |
| US-62 | Cartouches `.rom` de la clé dans les banques (deux emplacements en RAM), contenu d'origine | ✅ banc, ⏳ carte |
| US-63 | `TELESTRA.CFG` : `a=` … `d=`, `bank1=` … `bank7=` appliqués au montage, écrits par le menu | ✅ banc, ⏳ carte |
| US-64 | RESET à froid (TELEMON n'inventorie les cartouches qu'à froid) | ✅ |
| US-65 | Banc : `-U` (répertoire = clé), `-M` (touches du menu), `-O` (image du menu) ; `tests/test_menu.sh` | ✅ 9/9 |
| US-66 | Coût du menu sur le cœur 1 mesuré sans carte (`make charge-menu`) | ✅ ≈ 48 µs sur 59,35 (estimation) |
| US-67 | Police libre pour le menu : unscii-8 (domaine public) | ✅ v0.6.1 |
| US-68 | RAM : ROM intégrées en flash copiées dans des emplacements ; une cartouche de la clé prend l'emplacement de la ROM qu'elle remplace, plus une banque supplémentaire | ✅ v0.6.1 : 16,8 Ko libres (0,5 Ko avant) |

## Sprint 7 — v0.7.0 — « Cassettes et mode Atmos » ✅ au banc (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-70 | Cartouche Atmos (BASIC 1.1) intégrée au firmware, proposée par le menu (`bank7=@atmos`) | ✅ banc, ⏳ carte |
| US-71 | Lecteur de cassette `.tap` en temps réel (CB1 / PB6 du VIA 1), implémentation propre | ✅ banc : CLOAD, « L'Aigle d'Or » |
| US-72 | Menu : ligne Cassette, sélecteur des `.tap`, éjection, position, moteur | ✅ banc, ⏳ carte |
| US-73 | Bandeau de la cassette sous l'image pendant la lecture | ✅ banc (`-D`), ⏳ carte |
| US-74 | Banc : `-c atmos`, `-K`, `-D` ; `tools/mktap.py` ; `tests/test_tape.sh` | ✅ 9/9 |

## Sprint 8 — v0.8.0 — « Enregistrement cassette » ✅ au banc (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-80 | `CSAVE` vers `NOM.TAP` sur la clé (PB7 décodé), bandeau « Écriture » | ✅ banc, ⏳ carte |
| US-81 | `.tap` à plusieurs parties | ✅ « L'Aigle d'Or » (2 parties) au banc |

## Plus tard

- Essai et réglage sur carte (temps de bus, son, DVI, accès USB, modem).
- Essai de la prise RS232 sur carte : PicoWiFiModemUSB (`ATDT` depuis
  `CONSOLE`), puis retour à TELEMATIC ; en option l'UEXT.
- Vérifier sur matériel la cadence de sonnerie (seule hypothèse Minitel restante).
- Essai croisé sur carte : TELEMATIC (Neo6502TeleStrat) appelé par NeoTel sur un second Neo6502.
- Menu sur carte : rendu (temps du cœur 1), clavier, clé, cartouches,
  cassette et bandeau.
- Cassette : chargement accéléré (BASIC 1.1, au niveau du bus sur le
  Neo6502) ; `STORE`/`RECALL` (tableaux) non essayés.
- TELEMON + HYPER-BASIC seuls : le démarrage sur STRATSED s'arrête après la
  liste des ROM (observé au banc, non expliqué ; comparer à Oricutron).
- Clé retirée puis rebranchée : non gérée (montage au premier branchement).
- Variante RAM 64 Ko : ≈ 0,5 Ko de RAM libre.
- Imprimante vers l'UART ou la clé USB sur le Neo6502.
- Instantanés (savestates), sélecteur de ROM au démarrage.
- Test « golden » image contre Oricutron (PPM), ROM ORIX.

## Définition de « terminé »

`make` passe (tests unitaires + démarrage), `make uf2` compile les deux
variantes, documentation et CHANGELOG local à jour, commit signé bmarty.
