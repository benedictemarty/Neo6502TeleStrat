# Puces reprises de reload-emulator

Copie figée de `src/chips/` de [reload-emulator](https://github.com/benedictemarty/reload-emulator)
au commit `462372a` (2026-09-29), licence zlib/libpng (en-tête de chaque
fichier, inchangé). Le projet ne dépend ainsi plus des modifications en cours
dans `~/reload-emulator` (autre développement actif sur ces fichiers).

| Fichier | Rôle |
|---|---|
| `chips_common.h` | types communs |
| `mos6522via.h` | VIA 6522 (chemin rapide exact) |
| `ay38910psg.h` | AY-3-8912 |
| `kbd.h` | matrice clavier |
| `clk.h` | conversions de temps |
| `w65c02cpu.h` | cœur W65C02S cycle à cycle (banc PC et tests) |

Correctif repris depuis : `w65c02cpu.h` de reload `882d18f` (2026-09-30,
v0.16.7) : SBC décimal avec opérandes BCD invalides, cycle de BBRx/BBSx
quand le branchement est pris. Les autres fichiers restent à `462372a`.

Mettre à jour : recopier depuis un commit de reload, relancer `make` (dont
`make cpu_harte`) et `make charge`, noter le commit ici.
