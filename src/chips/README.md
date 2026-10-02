# Puce gardée en copie (reload-emulator)

Depuis la v0.16.16, toutes les puces viennent du **socle** reload épinglé
(`RELOAD_DIR`, étiquette `RELOAD_SOCLE`, voir `docs/adr/ADR-01-socle-reload.md`),
sauf `mos6522via.h`. L'inclusion `-Isrc` passe avant le socle : cette copie
l'emporte.

| Fichier | Écart au socle | Fin prévue |
|---|---|---|
| `mos6522via.h` | plus utilisée par défaut depuis v0.16.29 (VIA du socle `via6522`) : repli seulement (`TELESTRAT_VIA6522=OFF`) ; timer 1 faux par pas de 4 (N + 8) | à supprimer après l'essai de `via6522` sur carte |

Base : reload `462372a` (2026-09-29), licence zlib/libpng (en-tête inchangé),
plus le correctif IER de reload `d95caf9` (v0.16.13).

Mettre à jour : changer d'étiquette de socle (`RELOAD_SOCLE`), relancer
`make` (dont `make cpu_harte`), les deux variantes et `make charge`.
