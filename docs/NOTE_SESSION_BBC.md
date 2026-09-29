# Note pour la session BBC (reload-emulator) — 2026-09-29

De : session Neo6502TeleStrat (portage Telestrat sur le modèle d'`oric.uf2`).
Objet : deux défauts trouvés sur la carte Neo6502, présents aussi dans
reload-emulator ; plus l'état de la carte.

## 0. La carte

- La carte fait tourner **le Telestrat** depuis ce soir (flashé par SWD avec ton
  OpenOCD `~/.local/openocd-dev` et ta méthode, merci) : **reflasher `bbc.uf2`**
  avant de reprendre tes essais.
- Aucun OpenOCD laissé ouvert, cœurs non arrêtés.

## 1. Bus : la donnée d'une lecture fuit pendant une pause horloge haute

**Concerne** : `chips/wdc65C02cpu.h` (donc `oric.uf2`) et le pilote SIO intégré
de `bbc.c` (`bus_set_data`). Le pilote PIO (`BBC_BUS_PIO`) n'est **pas**
concerné : PHI2 y descend juste après la présentation.

**Mécanisme** : `set_data` présente l'octet par une impulsion OE3 (bas puis
haut aussitôt), puis le bus le garde par sa seule capacité jusqu'au front
descendant de PHI2, où le 65C02 le mémorise. Si l'émulation marque une pause
entre les deux — fin de tranche ou de trame (rendu, `tuh_task`, attente de la
trame : plusieurs millisecondes horloge haute) — la charge fuit et l'octet lu
change (un `$00` ne l'est plus). Comme les pauses tombent toujours aux mêmes
cycles, le défaut est **déterministe**.

**Observé sur carte (Telestrat)** : TELEMON lit deux fois la page `$FF00` de
chaque banque pour détecter un bus flottant. Sur la carte, la relecture de
`$FF4E` de la banque 3 (ROM TELEMATIC, `$00`) différait : banque déclarée
invalide (« 48 Ko ROM » au lieu de 56), et la ligne de titre était corrompue.
Même trace de bus que le banc PC, au cycle près, jusqu'à ce point (journal
des accès en `$03xx` lu par SWD, comparé à la trace PC).

**Correction appliquée côté Telestrat** (`platforms/rp2040/src/neo6502_bus.h`,
commit `1cb39d0` de Neo6502TeleStrat) : si le cycle précédent était une
lecture, l'impulsion OE3 est **renvoyée juste avant le front descendant** (les
GPIO sont encore en sortie avec la même donnée). Résultat : banques et titre
identiques au banc PC, 4 redémarrages sur 4.

```c
static inline void neo6502bus_tick(neo6502bus_t* c) {
    if (c->driven) {              // lecture au cycle précédent
        gpio_put(NEO_OE3_PIN, 0);
        gpio_put(NEO_OE3_PIN, 1);
        c->driven = false;
    }
    gpio_put(NEO_CLOCK_PIN, 0);
    ...
}
// set_data : ... impulsion OE3 ... ; c->driven = true;
```

**Point à vérifier de ton côté** : dans `bus_set_data` (SIO), l'impulsion OE3
n'a **aucun NOP** (≈ 5 ns à 372 MHz), alors que ton `bus6502.pio` note que le
74HC245 demande ~20 ns pour piloter les lignes (OE3 bas 6 cycles PIO). Mon
pilote a la même impulsion courte (héritée de `wdc65C02cpu.h`) : le
renvoi avant le front a suffi sur carte, mais une impulsion d'au moins ~20 ns
serait plus sûre dans les deux projets. Je ne l'ai pas encore mesurée.

## 2. Clavier de l'Oric : PB3 testé par égalité

**Concerne** : `src/systems/oric.h` ligne ~394 (pas `bbc.h`).

```c
if (kbd_scan_lines(&sys->kbd) == line_mask)   // faux
if (kbd_scan_lines(&sys->kbd) & line_mask)    // juste
```

PB3 doit indiquer qu'une touche de la **ligne sélectionnée** est enfoncée
parmi les colonnes actives, quelles que soient les autres lignes. L'égalité
échoue dès que deux touches de lignes différentes sont enfoncées — cas de
**SHIFT + touche** : sur carte, `*` (SHIFT + 8) arrivait comme `8`. Corrigé
côté Telestrat, validé sur carte (`PRINT 6*7` → 42).

Au passage, dans `oric.h` : la ligne 4 de la matrice inverse `,`/`.` et `<`/`>`
(`" <>     "` sans SHIFT) ; d'après `qwktab` d'Oricutron, `,` et `.` sont sans
SHIFT.

## 3. Ce qui m'a servi de chez toi (merci)

Encodage TMDS en 3 plans 1 bpp au lieu de la palette (35 µs par ligne sur
carte pour le Telestrat, 0 retard), 960x544 à 372 MHz / 1,30 V, priorité bus
au DMA, `openocd-dev`, la méthode `carte.py` (file de touches par SWD,
mesure). Telestrat mesuré sur carte : 65C02 à 1,000 MHz, cœur 0 à 45-62 %.
