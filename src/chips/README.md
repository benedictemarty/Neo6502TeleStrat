# Puces gardées en copie (reload-emulator)

Depuis la v0.16.14, les puces viennent du **socle** reload épinglé
(`RELOAD_DIR`, étiquette `RELOAD_SOCLE`, voir `docs/adr/ADR-01-socle-reload.md`) :
`chips_common.h`, `kbd.h`, `clk.h`, `w65c02cpu.h` (et `mem.h`, `wdc65C02cpu.h`).
Il ne reste ici que les deux fichiers qui diffèrent encore du socle.
L'inclusion `-Isrc` passe avant le socle : ces copies l'emportent.

| Fichier | Écart au socle | Fin prévue |
|---|---|---|
| `mos6522via.h` | pas de mode paresseux (`idle` / `pending`) : `_telestrat_via_skip` et `_telestrat_quiet_steps` (`telestrat.h`) avancent les compteurs eux-mêmes | quand le socle offre « avancer de n pas sans événement » |
| `ay38910psg.h` | pas de marqueurs `CHIPS_HOT` | à la reprise du socle (sans effet attendu, à mesurer par `make charge`) |

Base : copie figée de reload `462372a` (2026-09-29), licence zlib/libpng
(en-tête de chaque fichier, inchangé), plus les correctifs repris : `882d18f`
(`w65c02cpu.h`, SBC décimal et BBR/BBS, v0.16.7), `a0314e4`
(`ay38910psg_sample_u8`, v0.16.8), `d95caf9` (`mos6522via.h`, IER, v0.16.13).

Mettre à jour : changer d'étiquette de socle (`RELOAD_SOCLE`), relancer
`make` (dont `make cpu_harte`), les deux variantes et `make charge`.
