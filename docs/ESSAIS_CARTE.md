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

## Relevé du 2026-11-16 (v0.17.0 publiée, tag `v0.17.0`, par RP3Sonde)

Firmware `telestrat.uf2` du paquet Prophet `firmware/telestrat` (sha256 `ba134f75…`, `make uf2` de `be1cf9d`)
flashé et vérifié en 7,9 s à 2000 kHz (`rp3s.py flasher-cible … --ssh` : sans `--ssh`, OpenOCD du Pi ne
voit pas l'image et répond « Programming Failed »). `tools/carte.py` utilisé à travers le port Tcl du Pi
(adaptateur hors dépôt qui remplace sa fonction `openocd`).

| Essai | Résultat |
|---|---|
| Démarrage | ✅ TELEMON 2.4, STRATSED 2.0c, HYPER-BASIC 2.0b, TELEMATIC 2.0b, TELE-ASS 1.0a, « 1- HYPER-BASIC / 2- TELE-ASS », 44 Ko libres |
| Clé USB | ✅ montée, lecteur A : STRATSED.DSK |
| Affichage | ✅ 502 trames en 10 s, cœur 1 : 44 µs par ligne (max 47), 0 ligne rouge |
| 65C02 | ✅ 1,004 MHz, cœur 0 à 57 % (11,3 ms par trame, 11,5 au pire), cœur 1 à 31 µs par ligne (max 35) |
| Menu (F1 par la sonde) | ✅ page principale, « Neo6502 v0.17.0 », modem « PicoWiFiModemUSB : prêt » |
| Page Réseau (socle -24) | ✅ s'ouvre par Réseau + Entrée : Serveur (vide), Wi-Fi (réseau de TELESTRA.CFG), mot de passe masqué |
| Recherche Wi-Fi (socle -26) | ✅ Entrée sur Wi-Fi : liste « Réseaux Wi-Fi » (2 réseaux, WPA, niveau en dBm) en moins de 1 s ; pendant la recherche, 26 à 29 trames par 0,53 s (≈ 50/s, pas d'arrêt de l'affichage), 0 ligne rouge |
| Retour | ✅ Échap ×3 (liste, page sans enregistrer, menu) : HYPER-BASIC reprend, `PRINT 6*7` → 42 |

À voir : sous la liste des réseaux, la ligne d'aide « Wi-Fi : Entrée = … » de la page Réseau paraît recouverte
d'une barre de couleur (capture vidéo, non analysé). Recherche lente (réseau absent, modem lent) non essayée ;
TELESTRA.CFG non modifié (rien enregistré).

Écran vu par une capture HDMI (MS2109, `/dev/video32`, ffmpeg) : `ecran.py` / `carte.py ecran --png`
restent illisibles pour le Telestrat ; `carte.py ecran` (texte) fonctionne.

### Ligne Réseau du menu (v0.18.0 en développement, US-196, 2026-11-16)

Firmware `make uf2` de l'arbre de travail (US-196), flashé par RP3Sonde en 7,8 s ; écran par la capture HDMI.

| Essai | Résultat |
|---|---|
| F1 pendant l'essai de montage (attente d'adresse IP) | ✅ « Réseau .Wi-Fi .TNFS connexion en cours » |
| F1 après l'échec (34 s) | ✅ « Réseau ✗Wi-Fi ✗TNFS Wi-Fi absent (STATUS:5) », signes et libellés en rouge |
| Wi-Fi connecté, volume monté | ✅ vu ensuite (v0.18.0, ci-dessous) |

Incident au premier démarrage après le flash : écran noir, cœurs 0 et 1 arrêtés ; cœur 0 dans `panic("Data Seq
Error")` de TinyUSB (`hcd_rp2040_irq`, pendant `usb_scan_entry` → `f_readdir` → `disk_read`), puis `_exit`
(`bkpt`). Défaut connu du socle (STRATEGIE-NEO6502, 1 bis) : `third_party/tinyusb` n'a pas
`patches/tinyusb-data-seq.sh`. Redémarrage par la sonde : plus de panique en 40 s. US-197 au backlog.

### TNFS sur carte (v0.18.0, 2026-11-16, soir)

Serveur : `tnfsd` (Spectranet) recompilé dans le scratchpad sur le port **16385** (16384 pris par une autre
session), racine avec `NETTEST.DSK` (copie de `~/oriclib/games/dsk/STRATSED.DSK`) ; `TELESTRA.CFG` :
`reseau=172.22.195.246:16385` (page Réseau, Enregistrer). Modem : `172.22.195.149`.

| Essai | Résultat |
|---|---|
| Wi-Fi du modem | ✅ `STATUS:2` (le modem a rejoint le réseau entre-temps) ; port TNFS du modem utilisé (`relay` = 1) |
| Premier montage | ❌ 5 MOUNT sans réponse ; F1 : « ●Wi-Fi ✗TNFS 172.22.195.246:16385 sans réponse ». Cause : **pare-feu du PC** (`ufw`, entrée `DROP`) : la même requête MOUNT envoyée depuis le Pi (même Wi-Fi) restait sans réponse, depuis le PC elle avait sa réponse. Règle ajoutée par bmarty : `ufw allow from 172.22.195.0/24 to any port 16385 proto udp` |
| Montage après la règle | ✅ `tnfsd` : « Session started at: / » (172.22.195.149) ; F1 : « ●Wi-Fi ●TNFS 172.22.195.246:16385 », panneau de la clé « ● Réseau » |
| Sélecteur du lecteur B | ✅ source Clé USB / Réseau, liste du serveur « NETTEST.DSK 1001 Ko », « ✓ Lecteur B : net:/NETTEST.DSK » ; `tnfsd` : « File mounted: /NETTEST.DSK » |
| Même image en A et en B | ✅ refusée : « image refusée (MFM_DISK, déjà en place ?) » |
| `DIR "A"` (clé) | 92 fichiers, 2229 secteurs libres ; **0** datagramme TNFS |
| `DIR "B"` (réseau) | ✅ **88 fichiers, 2237 secteurs libres** (sans CARTE, RETOUR, SOCLE, BUSOK .COM de la clé) ; **14** datagrammes TNFS (`net.dgram.sent`) |

Syntaxe (index HYPER-BASIC, oric.free.fr) : `DIR "B"` ; `DIR B` et `DIR B:` donnent « Attention: expression
alphanumérique ». Non essayés : écriture sur la disquette du réseau, cartouche et cassette du réseau,
`a=net:/…` au démarrage, retrait du modem.

### Menu en anglais (v0.19.0, US-198, 2026-11-16)

| Essai | Résultat |
|---|---|
| F1, touche L | ✅ « Menu in English (Save the configuration: keep it) » ; L de nouveau : « Menu en français… » |
| Page principale en anglais | ✅ Floppy disks, Cartridges, Bank 7…, USB key / Key mounted, Devices, Snapshots, Printer, Tape fast (BASIC 1.1 CLOAD), Motor relay (PB6), Modem on / ready, Restart (RESET), Save the configuration, Resume ; pied « Arrows select … Esc resume », « F1: open this menu », « L: français » |
| Ligne Réseau | ✅ « Network ●Wi-Fi ✗TNFS 172.22.195.246:16385 no answer » (premier essai après le flash, voir US-199) ; en français, détail entier « …16385 sans réponse » (barre élargie d'une colonne) |
| Message d'action | ✅ « Drive A: STRATSED.DSK » |
| Page Réseau | en français (socle), pied en anglais « Arrows field, Enter next, Backspace erase, Esc cancel » |
| Second essai de montage (Enregistrer de la page Réseau) | ✅ session du modem sur `tnfsd` (22:34:45) |

Non essayés sur carte : `langue=en` lu au démarrage, écrit par « Save the configuration » (couverts par
`test_menu_lang`) ; bandeaux de la cassette et du volume en anglais.
