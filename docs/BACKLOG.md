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

## Sprint 9 — v0.9.0 — « Imprimante et clé » ✅ code (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-90 | Imprimante vers un fichier de la clé (`imprimante=`, `IMPRIM.TXT`) | ✅ code + tests de la file, ⏳ carte |
| US-91 | Clé retirée puis rebranchée : lecteurs vidés puis remis, sans redémarrer | ✅ code ; ✅ carte (2026-09-30, v0.16.12, docs/TESTS.md) ; `DIR` sans clé : catalogue gardé en mémoire par STRATSED (WD1793 « non prêt », vérifié au banc) |
| US-92 | Oracle Oricutron sans fenêtre (v0.8.1) | ✅ |

## Sprint 10 — v0.10.0 — « STRATORIC » ✅ au banc (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-100 | Cartouche STRATORIC intégrée (banques 7, 6, 5), `bank7=@stratoric` | ✅ banc : STRATORIC V4.0, BASIC, cassette |
| US-101 | Disquettes SEDORIC (jeux Oric/Atmos) | ✅ 3D Munch, démo 1337 au banc |
| US-102 | Démarrage de la variante RAM 64 Ko sans `BONJOUR.COM` expliqué (appel d'HYPER-BASIC à la banque 5 vide) | ✅ expliqué ; ⏳ lecture d'une banque vide sur le vrai matériel |

## Sprint 11 — v0.11.0 — « Périphériques dans le menu » ✅ au banc (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-110 | Menu : imprimante activée / coupée, enregistrée (`impression=`) | ✅ banc (`test_menu`), ⏳ carte |
| US-111 | Menu : modem activé / coupé (ligne raccrochée), état affiché, enregistré (`modem=`) | ✅ code + tests unitaires, ⏳ carte |

## Sprint 12 — v0.12.0 — « Impression matricielle et traceur 4 couleurs » ✅ au banc (2026-09-29)

Objectif du PO : imprimer en mode Centronics / matricielle et en mode tracé
4 couleurs ; sortie en images sur la clé d'abord, vraie imprimante USB plus
tard ; modèles : Epson FX-80 (ESC/P) et Oric MCP-40.

| US | Récit | État |
|---|---|---|
| US-120 | Epson FX-80 : pages PNG écrites au fil de l'eau (bande de 32 lignes), modes de caractères, interlignes, marges, tabulations, graphiques | ✅ banc (`test_telestrat`, `test_printer`), ⏳ carte |
| US-121 | MCP-40 : tracés SVG 4 couleurs (texte, D J M R H I C L P Q S X A) | ✅ banc, ⏳ carte, ⏳ vraie MCP-40 (pointillés) |
| US-125 | MCP-40 revue avec le manuel français et celui du Tandy CGP-115 (même mécanisme) : ordre des couleurs tranché, origine en entrant en mode graphique, taille S gardée en mode texte, interligne mesuré (v0.12.1) | ✅ banc |
| US-122 | Menu et `TELESTRA.CFG` : Texte → FX-80 → MCP-40 → coupée (`imprimante_type=`) | ✅ banc |
| US-123 | Pas de perte : ACK retenu tant que la file est presque pleine | ✅ code, rejeu identique |
| US-124 | Banc : `-G fx80:RÉP` / `mcp40:RÉP`, outil `printer_render` (rendu après coup d'un `IMPRIM.TXT`) | ✅ |

## Sprint 13 — v0.13.0 — « Cassette rapide, moteur sans relais » ✅ au banc (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-130 | Cassette rapide : `CLOAD` du BASIC 1.1 immédiat (ROM patchée en RAM, registres `$03FE`/`$03FF`) | ✅ banc (`test_tape`, « L'Aigle d'Or »), ✅ carte |
| US-131 | Moteur toujours en marche (câble DIN sans relais) | ✅ banc, ⏳ carte |
| US-132 | Menu et `TELESTRA.CFG` : `cassette_rapide=`, `cassette_moteur=` | ✅ banc |
| US-133 | Banc : `-Z`, `-Y`, texte du menu (`-O menu.txt`) | ✅ |

## Sprint 14 — v0.14.0 — « Instantanés » ✅ au banc (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-140 | Registres du vrai 65C02 lus et remis par un NMI détourné (programme servi sur le bus) | ✅ banc, ✅ carte (v0.16.1) |
| US-141 | Instantané de la machine dans `ETATnnnn.STA` ; reprise, cartouches remises | ✅ banc (`test_state`), ✅ carte |
| US-142 | Menu : ligne Instantanés (enregistrer, reprendre) | ✅ banc |
| US-143 | Banc : `-X T:FICHIER`, `-J FICHIER` | ✅ |

## Sprint 15 — v0.15.0 — « Choix des ROM au démarrage » ✅ au banc (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-150 | Profils de démarrage (Telestrat, STRATORIC, Atmos), page « Démarrer sur… » (`demarrage=choix`) ou profil direct | ✅ banc (`test_profiles`), ⏳ carte |
| US-151 | ROM ORIX 1.0 | ❌ abandonné (v0.15.1) : fichiers par un CH376, extension absente d'un Telestrat d'origine |
| US-152 | Profils définis sur la clé (`profil=`), le choix des ROM revient à l'utilisateur (v0.15.2) | ✅ banc, ⏳ carte |

## Sprint 16 — v0.16.0 — « Retour sur carte » ✅ sur carte (2026-09-29)

| US | Récit | État |
|---|---|---|
| US-160 | Démarrage sur carte : tas de `dvi_init` réservé à l'édition des liens, RAM regagnée | ✅ carte |
| US-161 | Clé USB lue sur carte, seule ou avec un clavier (TinyUSB 0.21.0, montage hors du rappel) | ✅ carte |
| US-162 | Dépôt et relecture de fichiers sur la clé par la sonde (`carte.py deposer`, `relire`) | ✅ carte |
| US-163 | Disquette de la clé : démarrage à froid au premier montage | ✅ carte (STRATSED, DIR, SAVE) |
| US-164 | Impression FX-80 sur la clé | ✅ carte (page PNG relue) |
| US-165 | Cassette rapide sur carte (« L'Aigle d'Or ») | ✅ carte |
| US-166 | Instantanés sur carte (NMI détourné sur le vrai 65C02) : enregistrer, reprendre en plein jeu | ✅ carte |
| US-167 | MCP-40 sur carte | ✅ carte : tracé en couleurs, SVG valide relu |
| US-169 | Modem sur carte | ⏳ (la Pico du PicoWiFiModemUSB sert de sonde SWD pour l'instant) |
| US-168 | Variante RAM 64 Ko sur carte (3 tampons DVI : FatFs du projet en FF_FS_TINY, CDC 128 octets) | ✅ carte : image propre, clé montée ; STRATSED sans BONJOUR.COM s'arrête comme au banc |

## v0.16.7 — anomalie du cœur 65C02 du PC ✅ (2026-09-30)

| Id | Anomalie | État |
|---|---|---|
| BUG-1 | `w65c02cpu.h` : SBC décimal faux sur opérande BCD invalide ; BBRx/BBSx un cycle trop court quand le branchement est pris (trouvé par les SingleStepTests, session reload-emulator) | ✅ correctif reload `882d18f` repris, `make cpu_harte` ; le Makefile recompile désormais sur changement de `src/chips/` |

## v0.16.8 — anomalie du son de l'AY ✅ au banc (2026-09-30)

| Id | Anomalie | État |
|---|---|---|
| BUG-2 | Son : `(uint8_t)(sample * 255)` débordait avec plusieurs voies fortes ; 21 739 échantillons/s pour une sortie à 22 050 Hz (signalé par la session reload-emulator, correctif `a0314e4`) | ✅ banc, 12 tests, rejeu identique ; ✅ carte (2026-09-30) : écouté par bmarty, « fonctionne de manière correcte » |

## v0.16.9 — touches multimédia ✅ au banc (2026-09-30)

| Id | Récit | État |
|---|---|---|
| US-171 | Le volume est gardé d'un démarrage à l'autre (`volume=` dans `TELESTRA.CFG`, v0.16.10) | ✅ code + 3 tests ; ✅ carte (2026-09-30) : `volume=5` écrit par le menu, relu après reset |
| US-170 | Volume +, Volume − et Muet d'un clavier multimédia règlent le son, avec une jauge à l'écran | ✅ code + 25 tests ; ✅ carte (2026-09-30) : clavier de bmarty reconnu (rapport 1, tableau 16 bits), 5 appuis reçus, bmarty : « fonctionne » |

## v0.16.11 — verrous DVI dédiés ✅ compilé (2026-09-30)

| Id | Anomalie | État |
|---|---|---|
| BUG-3 | `dvi_init` avec `next_striped_spin_lock_num()` : verrous partagés avec FatFs et TinyUSB, lignes en retard possibles (signalé par reload, vu par Trinity) | ✅ verrous dédiés ; ✅ carte (2026-09-30, v0.16.12) : 0 ligne en retard, `DIR` sur la clé compris |

## v0.16.12 — socle reload épinglé ✅ (2026-09-30)

| Id | Récit | État |
|---|---|---|
| US-172 | Le projet compile contre une étiquette vérifiée de reload, pas contre sa tête (`tools/reload_socle.sh`, ADR-01) | ✅ `make` entier et deux variantes sur `socle-2026-09-30` |

## v0.16.13 — VIA : IER relâche l'IRQ ✅ (2026-09-30)

| Id | Anomalie | État |
|---|---|---|
| BUG-4 | VIA : interdire par IER une source active ne relâchait pas l'IRQ (signalé par reload) ; `test_menu` dépendait du bus flottant d'une banque vide | ✅ correctif + test ; `test_menu` rendu déterministe ; démarrage à froid comparé à la référence (`-Q`, test_replay C) |

## v0.16.14 — fusion avec reload : cassette, puces, rendu du menu ✅ (2026-09-30)

| Id | Récit | État |
|---|---|---|
| US-173 | Cassette, puces identiques et rendu du menu pris dans le socle reload (`socle-2026-09-30-4`), copies supprimées | ✅ `make` entier, images du menu identiques au bit près, deux variantes (RAM : −864 / −880 o), `make charge` 58 % |
| US-174 | WD1793 du socle (`wd1793_tick_n`, `wd1793_next_event_us`, `oric_dsk.h`) à la place du nôtre | ✅ v0.16.15 (`socle-2026-09-30-5`) : 466 unitaires, rejeu 3/3, charge 58 / 66 % ; ✅ carte (2026-10-01, v0.16.17) : DIR et SAVE en flux sur la clé (relu), image en flash au retrait de la clé |

## v0.16.16 — fusion : AY, clavier USB, cadence du son ✅ (2026-10-01)

| Id | Récit | État |
|---|---|---|
| US-175 | `ay38910psg.h` et `hid_app.c` du socle ; AY à la cadence réelle du PWM ; `$80` sans disque (fiche) | ✅ `make` entier, deux variantes, charge 58 / 66 % ; ✅ carte (2026-10-01) : PING, ZAP, SHOOT, EXPLODE ; bmarty : « très bien le son » |

## Plus tard

- Essai et réglage sur carte (temps de bus, son, DVI, accès USB, modem).
- Essai de la prise RS232 sur carte : PicoWiFiModemUSB (`ATDT` depuis
  `CONSOLE`), puis retour à TELEMATIC ; en option l'UEXT.
- Vérifier sur matériel la cadence de sonnerie (seule hypothèse Minitel restante).
- Essai croisé sur carte : TELEMATIC (Neo6502TeleStrat) appelé par NeoTel sur un second Neo6502.
- Menu sur carte : rendu (temps du cœur 1), clavier, clé, cartouches,
  cassette et bandeau.
- Cassette : `STORE`/`RECALL` (tableaux) non essayés ; chargement accéléré
  du BASIC 1.0 (banque 5 de STRATORIC) ; démonstration « prise K7 » de
  « Telestrat à cœur ouvert » (p. 101) à essayer avec le moteur toujours en
  marche.
- STRATSED avec la variante RAM 64 Ko (banque 5 vide) : « Logiciel ecrit par
  Fabrice BROCHE » au lieu du menu (banc) ; Oricutron ne simule pas de banque
  vide. À vérifier : notice de la cartouche, vrai Telestrat. (Sans TELE-ASS,
  l'arrêt après la liste des ROM est reproduit par Oricutron : v0.8.1.)
- Variante RAM 64 Ko : 860 octets de RAM libres (v0.14.0). Imprimante :
  Texte seulement.
- Vraie imprimante USB (classe imprimante USB, ESC/P ou PCL) à la place des
  images : envoi des octets bruts, ou de la page rendue.
- FX-80 : police 9 x 11 de la FX-80, mode proportionnel, caractères de
  l'utilisateur (`ESC &`), autres jeux internationaux ; confronter une page
  au rendu d'une vraie FX-80.
- MCP-40 : confronter à une vraie machine (MCP-40 ou Tandy CGP-115) les
  pointillés, les graduations de `X`, l'interligne mesuré sur une figure ;
  police vectorielle de la machine ; commutateurs DIP (80 colonnes,
  CR + LF).
- Instantanés : touches rapides (F2 enregistrer, F3 reprendre le dernier).
- Test « golden » image contre Oricutron (PPM).

## Définition de « terminé »

`make` passe (tests unitaires + démarrage), `make uf2` compile les deux
variantes, documentation et CHANGELOG local à jour, commit signé bmarty.
