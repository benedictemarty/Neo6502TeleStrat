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

## Limites du modèle

- Optimiste : aucune contention SRAM avec le cœur 1 (DVI) et le DMA ; code
  en flash toujours dans le cache XIP ; USB non compté.
- Pessimiste : flottants logiciels de libgcc au lieu de ceux de la ROM
  (échantillons de l'AY, ~2 %).
- Une seule trace (disque) ; la télématique ajoute l'ACIA actif.

## Pistes (sprint 4 proposé)

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
