# Charge du RP2040 (mesure sans carte)

## Pourquoi

Sur le Neo6502, le RP2040 **fournit l'horloge du vrai 65C02** : à chaque
`telestrat_tick`, il fait un front d'horloge, lit l'adresse et R/W, répond ou
capte la donnée, puis fait avancer les périphériques. La boucle principale
exécute 19 968 cycles 6502 par trame, puis attend la fin des 20 ms. Si le
travail d'une trame dépasse 20 ms, rien ne casse : **le Telestrat tourne
simplement moins vite que 1 MHz** (son plus grave, horloge, liaison série et
Minitel ralentis). Budget : 295,2 MHz (horloge du DVI 800 x 480) / 1 MHz =
**295 cycles M0+ par cycle 6502**, soit 5,90 Mcycles par trame, rendu de l'écran,
clavier et USB compris.

## Méthode (`make charge`)

1. Le banc PC enregistre la trace du bus (adresse, R/W, donnée de chaque cycle)
   et le clavier : `telestrat_headless -B préfixe` (scénario : démarrage sur
   `STRATSED.DSK`, HYPER-BASIC, `DIR` — forte activité disque ; 30 s, 30 M cycles).
2. La cible `telestrat_bench` (platforms/rp2040/bench) est **le même système
   compilé comme le firmware** (options, code chaud en RAM), le 65C02 étant
   remplacé par la relecture de la trace. Chaque octet que l'ARM place sur le
   bus est comparé à la trace : **0 différence sur 30 M cycles** — le code ARM se
   comporte exactement comme la version PC.
3. `tools/rp2040_load.py` exécute cet ELF dans un émulateur Cortex-M0+
   (unicorn) et compte les cycles (modèle : « Cortex-M0+ Technical Reference
   Manual », ARM DDI 0484C, chap. 3 ; accès SIO à 1 cycle ; multiplieur
   1 cycle ; diviseur matériel émulé). Le coût de la relecture est mesuré et
   retranché ; celui du **pilote de bus réel** de reload (`wdc65C02cpu_tick`,
   `get_data`, `set_data`, `set_irq`) est calculé sur le firmware et ajouté.
   Profil par fonction et par ligne de source avec `--profile`.

## Mesure sur carte (v0.4.1, 2026-09-29, 960x544 à 372 MHz)

| Situation | 65C02 | Cœur 0 (moyenne / pire) | Cœur 1 |
|---|---|---|---|
| TELEMON, écran fixe | 1,000 MHz | 45 % / 57 % | 35 µs par ligne, 0 retard |
| DIR (défilement, disquette en flash) | 1,000 MHz | 62 % / 76 % | 35 µs par ligne, 0 retard |

Le modèle sans carte annonçait 54 % en moyenne à 372 MHz : l'ordre de grandeur
est confirmé.

## Sprint 4 (v0.4.0) : le Telestrat tient 1 MHz (mesure sans carte)

| Scénario (trace) | Système | Pilote de bus | Rendu écran / trame | Charge moyenne | Charge au pire |
|---|---|---|---|---|---|
| Disque : démarrage, HYPER-BASIC, DIR (30 M cycles) | 102 cycles | 68 cycles | 0,60 Mcycle (0,92 au pire) | **68 %** | **76 %** |
| Serveur TELEMATIC appelé (60 M cycles) | 105 cycles | 68 cycles | 0,05 Mcycle (0,92 au pire) | **59 %** | **76 %** |

Aucune trame au-delà du budget ; il reste ~24 % pour l'USB (`tuh_task`,
pistes lues sur la clé, modem) et la contention mémoire, non modélisés.
Point de départ (v0.3.2) : 314 + 84 cycles, 150 % en moyenne.

Ce qui a changé, **sans rien changer au comportement** (vérifié à chaque étape
par `tests/test_replay.sh` : 0 différence sur le bus, la ligne IRQ, l'audio,
la liaison série et l'image, contre le modèle de référence) :

| Étape | Système (cycles / cycle 6502) |
|---|---|
| référence (tous les périphériques tous les 4 cycles) | 314 |
| pas de 4 cycles « au repos » sautés, rattrapés d'un coup | 198 |
| `telestrat_tick` court (pas complet et E/S hors ligne), banque en cache, AY par échéance | 118 |
| FDC/ACIA : accès sans pas complet si l'IRQ ne change pas | 119 |
| VIA stable aussi avec une IRQ en attente (65C02 sous SEI) ; entrées extérieures qui coupent le repos | 103 |
| lectures de VIA sans effet de bord (XRING qui scrute l'IFR) | 102 |

Autres gains : pilote de bus intégré (`platforms/rp2040/src/neo6502_bus.h`,
même séquence GPIO et mêmes NOP que reload, sans appels : 84 → 68 cycles) ;
rendu de l'écran par table (deux pixels par consultation : 1,35 → 0,92 Mcycle
au pire, image identique au rendu d'origine sur 600 écrans aléatoires).

Dépendances de mesure : puces figées dans `src/chips` (reload 462372a) ; SDK
Pico de `~/reload-emulator` au commit 2fd6e23 (mis à jour par ailleurs entre
deux mesures : à figer aussi).

## Résultats (2026-09-29, puces de reload figées au commit 462372a)

| Poste | Cycles M0+ par cycle 6502 |
|---|---|
| Système (`telestrat_tick`) | 283 en moyenne, 298 au pire (tranche de 1 ms) |
| Pilote de bus du vrai 65C02 | 84 |
| **Total** | **~367 pour un budget de 295** |
| Fin de trame (clavier + rendu écran) | 0,18 Mcycle en moyenne, **1,35 au pire** (écran modifié) |

Avec le reload de travail du 2026-09-29 01:40 (modifications non commitées du
VIA en cours, autre session) : 314 cycles, 150 % en moyenne — d'où l'option
`BENCH_CHIPS_DIR` pour mesurer contre des puces figées.

**Charge du cœur 0 : 127 % en moyenne, 150 % au pire** (hors USB : `tuh_task`,
lecture des pistes sur la clé, modem). Le Telestrat tournerait vers
**0,65 à 0,8 MHz**. À comparer : l'Oric de reload fait le même travail sans le
VIA 2, le FDC, l'ACIA, les joysticks ni la sonnerie.

Profil (reload de travail, même ordre de grandeur) :

| Poste | Part |
|---|---|
| `mos6522via_tick` (VIA 1 + VIA 2, tous les 4 cycles) | 21 % (+ ~3,5 % de division dans son chemin lent) |
| `telestrat_tick` lui-même (prologue, compteurs) | 21 % |
| décodage mémoire `_telestrat_mem_rw` | 15 % |
| joysticks (`get_pb`/`set_pb`/`set_pa` du VIA 2 à chaque pas) | 5 % |
| ACIA (tick + rappel de réception à vide) | 5 % |
| sonnerie (`set_cb1`), clavier, FDC, AY | 1 à 2 % chacun |

## Menu (sprint 6) : coût d'une ligne sur le cœur 1 (`make charge-menu`)

`tools/osd_cost.py` exécute `telestrat_osd_bench` dans le même émulateur
Cortex-M0+ (mêmes hypothèses) : une ligne du menu contre une ligne de l'image.

| Ligne de tampon | Cycles (moyenne / pire) | à 372 MHz |
|---|---|---|
| image du Telestrat (`telestrat_video_line`) | 4 769 / 4 769 | 12,8 µs |
| menu (`osd_render_line`) | 8 637 / 9 623 | 23,2 / 25,9 µs |

Budget : une ligne de tampon toutes les 59,35 µs (1104 pixels à 37,2 MHz =
29,68 µs par ligne de sortie, deux lignes par tampon). Sur carte, la ligne de
l'image coûte 35 µs, encodage TMDS compris (v0.4.1) ; avec le menu,
**estimation** 35 − 12,8 + 25,9 ≈ 48 µs, soit 81 % du budget. Première
version (masques par attribut en branchements) : 2,4 fois l'image sur PC ;
tables de masques + multiplication : 1,6 fois. À mesurer sur carte.

## Limites du modèle

- Optimiste : aucune contention SRAM avec le cœur 1 (DVI) et le DMA ; code
  en flash toujours dans le cache XIP ; USB non compté.
- Pessimiste : flottants logiciels de libgcc au lieu de ceux de la ROM
  (échantillons de l'AY, ~2 %).
- Une seule trace (disque) ; la télématique ajoute l'ACIA actif.

## Pistes proposées avant le sprint 4 (pour mémoire)

| Piste | Gain estimé |
|---|---|
| Rendu de l'écran sur le cœur 1 (comme le BBC de reload) | jusqu'à 23 % de trame au pire |
| Joysticks et sonnerie : mise à jour du VIA 2 seulement sur changement | ~6 % |
| ACIA : rappel de réception tous les 64 cycles, pas tous les 4 | ~3 % |
| Chemin rapide RAM/ROM en tête de `telestrat_tick` (comme `bbc_tick`) | part des 15 % |
| VIA : saut des pas inactifs (travail en cours dans reload, `idle`) | part des 21 % |
| Pilote de bus intégré au tick (commit « bus 65C02 intégré » du BBC) | part des 84 cycles |

Objectif : total ≤ ~260 cycles par cycle 6502 pour garder de la marge (USB,
contention), vérifié par `make charge` avant tout essai sur carte.
