# Fiche d'essais sur carte Neo6502 (v0.17.0)

Essais restant à faire sur la carte après le sprint 5 (revue de code).
Outils : sonde SWD et carte, voir [SONDE_SWD.md](SONDE_SWD.md) (une seule
carte pour tous les projets : annoncer la prise et la remise de la sonde).
Constats : [CRITIQUE.md](CRITIQUE.md). Backlog : [BACKLOG.md](BACKLOG.md).

Légende du résultat : ✅ conforme · ❌ écart (créer une US) · ➖ sans effet constaté.

## Préparation

- [ ] Compiler la v0.17.0 (`make uf2`), noter la taille de l'image (attendu : 408 068 o).
- [ ] Flasher, démarrer avec clé USB, lecteur de disquettes et clavier branchés.
- [ ] Noter la charge au repos (référence v0.16.34 : 54 %, 0 ligne rouge).

## A. Démarrage et affichage

| # | Essai | Attendu | Constat | Résultat |
|---|---|---|---|---|
| A1 | Pause depuis le menu, puis reprise | Image figée proprement, reprise sans glitch | F-01 | |
| A2 | Observer l'écran 5 min en BASIC et en menu | Pas de scintillement | F-17, F-18 | |
| A3 | Sortie 480p | Image stable, pas de gel | US-192 | |

## B. Clavier et manette

| # | Essai | Attendu | Constat | Résultat |
|---|---|---|---|---|
| B1 | Vrai clavier : Maj+A, relâcher Maj puis A | Pas de touche collée ni perdue | F-08 | |
| B2 | Touches multimédia : volume +/-, muet | Le volume change, il est mémorisé dans TELESTRA.CFG | v0.16.9, v0.16.10 | |
| B3 | Manette en jeu, puis menu en même temps | Pas d'état de VIA 2 incohérent | F-21 | |

## C. Son

| # | Essai | Attendu | Constat | Résultat |
|---|---|---|---|---|
| C1 | Musique / sons aigus de l'AY | Pas de débordement ni de grésillement | v0.16.8, v0.16.18 | |
| C2 | Relever les lignes rouges pendant le son | 0 ligne rouge | v0.16.18 | |

## D. Disquettes

| # | Essai | Attendu | Constat | Résultat |
|---|---|---|---|---|
| D1 | DIR, LOAD, SAVE sur A à D | Lecture et écriture correctes | v0.16.15 | |
| D2 | Programme qui lit `$0315-$0317` et `$0319-$031B` | Comparer à un Telestrat réel ou à Oricutron | N-16, P-23 | |

## E. Télématique et RS232

| # | Essai | Attendu | Constat | Résultat |
|---|---|---|---|---|
| E1 | Page Vidéotex complète par le modem (PicoWiFiModem) | Page entière, aucun octet perdu | P-02 | |
| E2 | Modem coupé / réactivé dans le menu | Ligne raccrochée, puis rattachée | US-191 | |
| E3 | Appel Minitel : TELEMON envoie-t-il des PRO2/PRO3 ? | Noter si oui (si oui, ajouter la réponse) | P-16 | |
| E4 | `rs232=uext` puis retour à `usb` | UART rendue aux messages, pas de coupure définitive | F-22 | |
| E5 | Modem qui ne lit plus : écrire beaucoup de données | Pas de gel de plus de quelques images | F-20 | |
| E6 | Commande ACIA 00 (émetteur coupé) et BREAK depuis TELEMON | Noter le comportement | P-09 | |

## F. Impression

| # | Essai | Attendu | Constat | Résultat |
|---|---|---|---|---|
| F1 | Impression texte vers la clé | Fichier créé, contenu exact | US-191 | |
| F2 | FX-80 : séquences `ESC e` / `ESC f` | Noter si un programme réel les emploie | P-05 | |
| F3 | Traceur MCP-40 | SVG correct | US-191 | |

## G. Performance

| # | Essai | Attendu | Constat | Résultat |
|---|---|---|---|---|
| G1 | Charge cœur 1 en DIR et pendant le son | ≤ 62 %, 0 ligne rouge | F-35 | |
| G2 | Mesurer le coût des défauts de cache du code en flash | Chiffrer, pas de correction a priori | F-35 | |

## H. Série v0.16.19 à v0.16.21

À reprendre dans le backlog : cœur 1 sans flash, tampon USB 512 en RAM 64 Ko, fichiers par `neo_storage` (8 essais).

- [ ] Relire les 8 essais dans BACKLOG.md et les recopier ici avec leur résultat.

## I. Comportement noyau (aucun changement attendu)

| # | Essai | Constat | Résultat |
|---|---|---|---|
| I1 | `STA $0310` pendant la capture du FDC | N-07 | |
| I2 | `LDA $FFFA` par un programme | N-10 | |
| I3 | PA0-PA2 repassées en entrée : valeur lue | N-14 | |
| I4 | AY au repos (BDIR = BC1 = 0) avec CB2 = 0 | N-15 | |

## Après les essais

1. Passer chaque point vérifié en ✅ dans CRITIQUE.md et BACKLOG.md.
2. Chaque ❌ devient une US du sprint 6.
3. Rendre la sonde et noter le firmware laissé sur la carte.

## Relevé du 2026-11-11 (v0.17.0, par RP3Sonde, Pi 192.168.1.116)

Firmware `build/rp2040/telestrat.uf2` (image 414 208 o) flashé et vérifié en 12,9 s ;
lecture par le tunnel OpenOCD du Pi (`outils/rp3s.py` de `~/Projets/RP3Sonde`).
`tools/carte.py` suppose la Debugprobe locale : non utilisé ici.

| Essai | Résultat |
|---|---|
| Démarrage avec clé (`demarrage=choix`) | ✅ page de choix attendue, 65C02 arrêté tant qu'aucun profil n'est validé (normal) |
| Entrée sur le premier profil | ✅ TELEMON 2.4, STRATSED 2.0c, HYPER-BASIC, TELEMATIC, TELE-ASS, menu « 1- HYPER-BASIC / 2- TELE-ASS » |
| Cadence du 65C02 | ✅ 1,018 à 1,037 MHz |
| Charge cœur 0 au repos | ✅ 49 % (référence v0.16.34 : 54 %) |
| Charge cœur 0 pendant `DIR` | ✅ 62 % (référence : 62 %) |
| Lignes DVI rouges | ✅ 0 (cœur 1 : 31 µs par ligne) |
| Frappe par la file de touches, `DIR` du lecteur A (STRATSED.DSK) | ✅ 92 fichiers, 2229 secteurs libres |

Non faits (besoin d'un humain devant la carte) : A1 à A3 (image, scintillement), B (vrai clavier, manette), C (écoute), E, F.
Le décodage de l'image 4 bits de `tools/carte.py ecran --png` ne correspond plus au tampon d'image actuel (image illisible) : à réparer.

### Modem et Minitel (2026-11-11, PicoWiFiModem, serveur de test sur le PC)

Firmware : la file de touches de diagnostic accepte `0xFE` (FUNCT maintenue pour la touche suivante ; `carte.py taper '\fD'`). Numéro réglé en RAM (`modem+9`), clé non modifiée.

| Essai | Résultat |
|---|---|
| FUNCT+D en APLIC 1 : appel du modem vers le PC (192.168.1.19:8615) | ✅ connexion, état Hayes « en ligne » |
| P-02 : page Vidéotex de 1 167 octets (> file de 512) | ✅ page entière à l'écran, `rx.lost` = 0 (une seule page, rafale réseau local) |
| Octets du Telestrat vers le serveur : `BONJOUR` + ENVOI (`$13 $41`) | ✅ reçus dans l'ordre |
| P-16 (PRO2/PRO3), E4 (`rs232=uext`), E5 (modem muet), E6 (ACIA) | non faits |
