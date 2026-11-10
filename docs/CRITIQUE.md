# Revue de code exhaustive — 2026-11-11

Relecture complète du dépôt (≈ 16 000 lignes hors ROM générées), en quatre
zones, en lecture seule. Chaque constat porte un identifiant (`N-` noyau,
`P-` périphériques, `F-` firmware RP2040 et OSD, `O-` outils, banc PC, tests
et build). Gravité : **C** critique, **M** majeure, **m** mineure, **s** style.
« à vérifier » = non confirmé par une lecture du code seule : à trancher par un
test ou une mesure, jamais par supposition (voir le suivi dans
`docs/BACKLOG.md`, sprint 5).

État de départ : `make test` passe (514 + 34 + 7 + 4 + 7 + 20 + 5 + 12 + 7 + 10
vérifications, 3 rejeux identiques, image identique à Oricutron, 12 vérifications
carte) ; `test_menu` et `test_tnfs` sont ignorés faute de ressources.

## Zone N — noyau d'émulation (`src/systems/`)

| Id | Gr. | Où | Constat |
|---|---|---|---|
| N-01 | C | telestrat_state.h:195-219 | Instantané tronqué entre `via` et `via2` (ou après la copie de l'AY / de l'ACIA) : les rappels et `userdata` restent ceux du fichier ; `cold_reset` ne les remet pas. Lire dans un tampon, appliquer à la fin, ou remettre les rappels dans tous les cas. |
| N-02 | M | telestrat_state.h:95-150, 246 | `deferred` (temps différé des périphériques) non rattrapé avant l'écriture ni enregistré : exécution divergente après rechargement. Appeler `_telestrat_catch_up` après la capture. |
| N-03 | M | telestrat.h:812 | Masque `0xFEEC` : la lecture de SR (bit 10) est dite sans effet de bord alors que `via_read(VIA_SR)` efface IFR.SR et lance un décalage. Masque `0xFAEC`. |
| N-04 | M | telestrat_state.h:164-257 | Validation superficielle (magic, version, signature) : ni CRC ni bornes sur `psg_next`, `psg_next_sample`, `sample_acc`, `head[]`, `printer_ack`, champs internes de la VIA et de `kbd`. |
| N-05 | M | telestrat.h:633-638 | `telestrat_printer_resume` ne remet pas `quiet_until` : ACK retardé jusqu'à 16 ms par octet retenu. Appeler `_telestrat_input_changed`. Fonction `static inline` hors API alors que le commentaire (l.124) la dit publique. |
| N-06 | m | telestrat_state.h:238 | `printer_wait` non enregistré : `printer_ack` = 0 au rechargement pendant une attente, l'ACK ne vient jamais. |
| N-07 | m | telestrat_state.h:98-106 | `telestrat_fdc_busy` testé avant la capture : un `STA $0310` dans les cycles de capture n'est pas vu (rare, à vérifier). |
| N-08 | m | telestrat_state.h:76-83 | Signature trop faible (seulement des `sizeof`) ; pointeurs de l'hôte écrits dans le fichier (non déterministe, fuite d'adresses). Ajouter `offsetof` de champs clés, marqueur d'endianness, pointeurs mis à zéro. |
| N-09 | m | telestrat.h:940-941 | Capture impossible si la pile reboucle (S = $00/$01) : comparer les octets bas en `uint8_t`. |
| N-10 | m | telestrat.h:937 | Lecture de `$FFFA/$FFFB` détournée en phase 0 même pour un `LDA $FFFA` du programme (à vérifier). |
| N-11 | m | telestrat.h:540-541 | `telestrat_reset` jette `deferred` sans rattraper ; `printer_ack`, `strobe`, `printer_wait` non remis à zéro (dans les deux en-têtes). |
| N-12 | m | telestrat.h:275-304, ref:252,256 ; telestrat_roms.h | Fonctions et tableaux non `static` définis hors `CHIPS_IMPL` : définitions multiples dès deux unités de compilation. |
| N-13 | m | telestrat_state.h / telestrat.h:905-1026 | `telestrat_regs_t`, `cpu_capture`, `cpu_restore` sans prototype public, sous `CHIPS_IMPL` : commentaire d'inclusion (state.h:16) incomplet. |
| N-14 | m | telestrat.h:549-555 | Lignes PA0-PA2 repassées en entrée : « garde la valeur » (Oricutron) vs commentaire du RESET « tirées à 1 » (à vérifier sur matériel). |
| N-15 | m | telestrat.h:768-770 | AY toujours posé sur IRA quand CB2 = 0, même au repos (BDIR = BC1 = 0) où le bus devrait flotter. |
| N-16 | m | telestrat.h:588-609 | Reflets du Microdisc : `$0315-$0317`, `$0319-$031B` tombent sur le VIA 1 (à vérifier contre le décodage réel). |
| N-17 | m | telestrat.h:714, 439 | `(uint32_t)(d + 3)` : débordement signé théorique ; `sample_rate` > 1 MHz non géré (n = 0). |
| N-18 | m | telestrat.h:352-354, 459 | `__attribute__((noinline))` propre à GCC/Clang ; littéraux composés incompatibles C++ malgré `extern "C"`. |
| N-19 | s | ref:5-12 | « Copie figée au sprint 3 (v0.3.2) » faux : le modèle de référence utilise via6522 du socle (mode paresseux) ; l'oracle partage donc les défauts éventuels de `via_advance`. À dire dans l'en-tête. |
| N-20 | s | telestrat.h:656-660 | « Identique cycle pour cycle à la référence » : cassette, turbo, imprimante occupée et capture par NMI ne sont pas couverts par le rejeu. À dire. |
| N-21 | s | telestrat.h:274, 664-666, 773, 783-786 ; ref:251 | Commentaires mal placés ou en double ; prototypes en double (`set_ring` 286/309, `key_*`, `insert_disk` 275/330). |
| N-22 | s | telestrat_state.h:247 | `sys->bank = 0xFF;` inutile (`select_bank` inconditionnelle). |
| N-23 | s | telestrat.h:628, ref:511 ; `_telestrat_via_skip` | `_telestrat_update_joysticks` vide ; enveloppe triviale. Code mort. |
| N-24 | s | telestrat.h / ref | ≈ 60 lignes (`_tv_*`, clavier, palette) copiées à l'identique entre les deux en-têtes : garde-fou de test pour qu'une table ne dérive pas seule. |
| N-25 | s | telestrat.h:1047 | `telestrat_pair_lut` initialisée paresseusement avec un test à chaque trame. |
| N-26 | s | telestrat_state.h:113 | Texte d'info limité à 1024 octets : peut couper un caractère UTF-8. |
| N-27 | m | telestrat_roms.h | Aucune mention de licence/ayant droit des ROM embarquées ; chemins absolus `/home/bmarty/…` dans les commentaires de provenance ; TELEMATIC 8 Ko dupliqué dans les deux moitiés sans commentaire clair. |

## Zone P — périphériques (`src/devices/`)

| Id | Gr. | Où | Constat |
|---|---|---|---|
| P-01 | M | drive_set.h:38, 50, 163, 175 | Noms limités à 12 caractères (`names[..][13]`, `strncpy 12`) alors que FatFs a `FF_USE_LFN 1` et `OSD_NAME_LEN` = 48 : `JEUX_ORIC_V2.DSK` introuvable, `a=` sans correspondance. Dimensionner à `OSD_NAME_LEN`, refuser au lieu de tronquer. |
| P-02 | M | hayes_line.h:98-101, 132 ; modem_mux.h:111 | Files de 512 octets : octets reçus jetés sans signal ni contrôle de flux (page Vidéotex tronquée ; à vérifier selon le cadencement du PicoWiFiModem). Compter les pertes, ne pas lire le CDC quand la file est pleine. |
| P-03 | M | modem_mux.h:93-99 | Bascule vers la RS232 en pleine communication Minitel : `memset(hayes)` sans raccrocher ; SEP $59 SEP $53 périmés en file ; `hayes_line_hangup` sans effet ; ATE0V1/ATS0=0 envoyés au correspondant au retour ; `+++`/`ATH` annulés en garde. |
| P-04 | m | printer_fx80.h:514 | `ESC b` avec canal ≠ 0 ne remet pas `tab_last` à 0. |
| P-05 | m | printer_fx80.h:459-471 | `ESC e` / `ESC f` absents (à vérifier sur la FX-80) : paramètres imprimés comme du texte. |
| P-06 | m | printer_fx80.h:155 | `ESC @` : `page_rows` < `band_top` possible (garde `!band_top` absente). |
| P-07 | m | printer_fx80.h:535 | Marge droite `ESC Q` jusqu'à 9 pouces : points au-delà de 8,5 jetés sans signal. |
| P-08 | m | mos6551acia.h:158-160 | IRQ d'émission indépendante de DTR (DTR = 0 doit couper récepteur et interruptions). |
| P-09 | m | mos6551acia.h:212-221, 237-245 | Commande b2-3 = 00 (émetteur coupé) : octet quand même émis ; b2-3 = 11 (BREAK) non modélisé (à vérifier selon TELEMON). |
| P-10 | m | mos6551acia.h:110-113 / 133 | `char_cycles` compte 2 bits de stop, `mos6551acia_format` 1 : incohérents. |
| P-11 | m | mos6551acia.h:264-268 | L'écho écrase `tdr` si un octet y attend. |
| P-12 | m | mos6551acia.h:204 / 170 | Lecture de l'état n'efface pas l'IRQ tant que RDRF actif : choix documenté, non vérifié sur matériel. |
| P-13 | m | mos6551acia.h:244 | Reste négatif de `tx_timer` perdu au rechargement : dérive du débit. |
| P-14 | m | hayes_line.h:133-141 | « NO CARRIER » dans le flux coupe la ligne et passe au Telestrat comme données : n'accepter que précédé de `\r\n` et retirer ces octets. |
| P-15 | m | hayes_line.h:183-199 ; minitel_port.h:237 | BUSY / NO ANSWER / ERROR : le Minitel reste en CONNECTING jusqu'à 40 s ; pas de délai pour DIALING/ANSWERING ; commentaire « > 440 ms » (minitel_port.h:27) faux vs code (l.246). |
| P-16 | m | minitel_port.h:183-187 | PRO2/PRO3 avalées sans réponse (à vérifier : TELEMON en émet-il ?). |
| P-17 | m | plotter_mcp40.h:290, 384-386 | `cur_neg` non remis à zéro sans chiffre : `D-,5` donne -5, un « - » isolé contamine la commande suivante. |
| P-18 | m | plotter_mcp40.h:301/303 ; printer_fx80.h:446 ; mcp40:133 | Positions sans borne (débordement int32 ; en-tête SVG `%09d` de longueur variable au-delà de 10^9). |
| P-19 | m | plotter_mcp40.h:333 | Graduations de X limitées à 255 sans signal. |
| P-20 | m | hid_media.h:69-73, 109-111 | Compteur `uint16_t i` peut boucler (descripteur ≈ 64 Ko) ; `bits`/`bit_off` en 16 bits. |
| P-21 | m | neo_writer.h:47 | `neo_writer_open` sur un écrivain déjà ouvert : handle FatFs perdu. |
| P-22 | m | drive_set.h:174, 211 | Au-delà de 32 images `a=` impossible, échec silencieux ; `a=` introuvable → règle « première image libre » appliquée malgré le commentaire. |
| P-23 | m | telestrat_fdc.h:105-123 | Reflets `$0315-$0317`/`$0319-$031B` (voir N-16). |
| P-24 | m | snapshots | Copie brute de `mos6551acia_t` avec pointeurs : non portable PC/RP2040 (détecté seulement par `sizeof(void*)`). |
| P-25 | s | telestrat_fdc.h | N'inclut pas `devices/wd1793.h` (dépend de l'ordre) ; `telestrat_fdc_tick` (l.80-83) duplique `wd1793_tick_n`. |
| P-26 | s | hayes.rxq, modem_mux.rxq, minitel.queue | Quatre files circulaires : factoriser sur `byte_fifo`. |
| P-27 | s | mos6551acia.h, fx80, minitel_port.h, hid_media.h | Code mort : `MOS6551_ST_PARITY/FRAMING/OVERRUN` jamais levés, `MOS6551_CTL_INTCLK`, `FX80_DPI`, `MINITEL_PRO2/3` (hors comptage), `hid_volume_apply` (tests seuls), `FX80_PROP` sans effet (documenté). |
| P-28 | s | hayes_line.h:183-229 | Fonctions `static` non `inline` dans un en-tête (avertissements RP2040). |
| P-29 | s | modem_mux.h:76 | `snprintf` sans `<stdio.h>`. |
| P-30 | s | printer_fx80.h:322 | Initialiseur `[0 ... N]` : extension GNU. |
| P-31 | s | plotter_mcp40.h, divers | Identifiants réservés (`_MCP40_PRINTF`, `_xxx` à portée de fichier). |

## Zone F — firmware RP2040 et OSD (`platforms/rp2040/`, `src/osd/`)

| Id | Gr. | Où | Constat |
|---|---|---|---|
| F-01 | M | telestrat.c:1726 ; neo_multiboot.h:15-18 | Pause : `neo_multiboot_return()` redémarre immédiatement depuis `tuh_task`, sans `wd1793_flush`, `neo_writer_close`, `render_job_end`, `neo_file_close` : piste, CSAVE, page FX-80 perdus, FAT possiblement corrompue. Passer par la file de commandes (`keys_service`). |
| F-02 | M | telestrat.c (aucun appel), oric_dsk.h:219-237 | `wd1793_flush` jamais appelé en fonctionnement : piste modifiée perdue tant que la tête reste dessus. Flush au repos (moteur arrêté ou N trames sans accès) et à l'ouverture du menu. |
| F-03 | M | telestrat.c:765 ; osd_config.h:161, 176 | « Enregistrer » : 2047 octets lus seulement, lignes coupées à 127 caractères (`read_config` en accepte 159) : profils longs détruits. Refuser si tronqué, copier par longueur. |
| F-04 | M | rom_builtin.h:441-445, 473, 506 ; telestrat.c:1006-1013, 1366-1370 | Profil/cartouche échoué à moitié : banques changées sans `cold_reset` (plantage du 65C02) ; `@stratoric` avec emplacement pris ; RAM 64 Ko sans emplacement banque 5. Vérifier place et fichiers avant, ou démarrage à froid dès qu'une banque est touchée. |
| F-05 | M | telestrat.c:1375-1378, 778-789, 1551-1561, 1748-1761 | 800x480 (`TELESTRAT_VIDEO_480`) sans `TELESTRAT_OSD` : `demarrage=choix` appelle `menu_open()` → pause sans menu, F1 inerte. Protéger par `#ifdef`. |
| F-06 | M | telestrat.c:173, 1689 ; telestrat_video.h:46 | `__not_in_flash` fait passer `state` (≈ 102 Ko), `planes`, `telestrat_video_lut` de `.bss` à `.data` : zéros stockés en flash (.bin 456 Ko). Retirer l'attribut ou `__uninitialized_ram` + mise à zéro. |
| F-07 | m | telestrat.c:1745, 1799 | Touche tenue pendant F1/F2/F3, relâchée menu ouvert : relâchement jeté, touche bloquée. |
| F-08 | m | hid_app.c:112 (socle) | Relâchement calculé avec les modificateurs du rapport précédent (Maj+A puis Maj relâché) (à vérifier ; code du socle). |
| F-09 | m | telestrat.c:1180-1186 | Commentaire « fermés sans écriture » faux : `wd1793_eject` → `oric_dsk_flush` écrit sur un volume parti (attente TNFS possible). |
| F-10 | m | telestrat.c:588-596 | Échec d'insertion : `drive_eject` appelé avant l'ouverture, lecteur vide (image flash de A non remise). |
| F-11 | m | telestrat.c:538-551 ; drive_set.h:86-92 | Fichiers AppleDouble `._NOM.DSK` listés (`hidden` ignoré), triés en premier, choisis pour A. |
| F-12 | m | telestrat.c:1354-1355, 873-877 | Message d'erreur écrasé (`@inconnu` → « fichier illisible »). |
| F-13 | m | rom_builtin.h:409-413 vs 381 | Profils `stratoric`/`atmos` proposés avec `TELESTRAT_ROMS_CLE` alors que `rom_builtins` vide ; `*err` vide → « Démarrage : » sans motif. |
| F-14 | m | rom_builtin.h:479-480 | Élément de profil > 63 caractères sauté sans erreur, profil déclaré réussi. |
| F-15 | m | telestrat.c:924, 928 | `user_profile_apply` lit 159 caractères sans sauter la suite (contrairement à `read_config`, 1399-1403). |
| F-16 | m | telestrat.c:1210, 1264, 1642 | Menu affiché avec état périmé (clé retirée, relecture sonde) : `menu.files`/`nfiles` changent sans redessin, Entrée agit sur un autre fichier. |
| F-17 | m | osd_menu_draw ; banner_update ; telestrat.c:787-788, 896-897 | Scintillement cœur 0/cœur 1 : `osd_clear` puis redessin sous la lecture du cœur 1, bandeau refait à chaque trame, `osd_open` avant `menu_draw`, surface du menu affichée jusqu'à la trame suivante. Dessiner dans un tampon, `telestrat_screen_update` avant `osd_open = false`. |
| F-18 | m | telestrat.c (banner_row, surface) | Pas de barrière mémoire avant l'écriture des drapeaux volatiles (`__compiler_memory_barrier`). |
| F-19 | m | telestrat.c:1587-1653 | Zone de dépôt de la sonde partagée avec la surface du menu, `config_save`, `state_work` ; commande 4 avec dépôt actif rouvre `diag_up_file` par-dessus un fichier ouvert. |
| F-20 | m | telestrat.c:1946 | `neo_cdc_write` jusqu'à 1 s et `modem_flush` 20 fois par trame (à vérifier : modem qui ne lit plus). |
| F-21 | m | telestrat.c:1713 | Rappels HID pendant `telestrat_tick` : `gamepad_state_update` → `telestrat_set_joystick` modifie le VIA 2 en plein tick (à vérifier). |
| F-22 | m | telestrat.c:1410 ; read_config | `listen=` par `atoi` sans plage ; `dial=`, `rs232=` hors `osd_config_value` ; `cfg_dial`, `cfg_listen`, `cfg_net`, `cfg_rs232_uext`, `modem_enabled` non remis à zéro au changement de clé ; `rs232=uext` coupe stdio définitivement. |
| F-23 | m | telestrat.c:1121 | Retour de `neo_file_append` ignoré (imprimante, clé pleine). |
| F-24 | m | telestrat.c:840-847 | Nom d'instantané faux après échec (`menu.state_last`). |
| F-25 | m | telestrat.c ; osd_menu | Liste de fichiers tronquée à 40 (16 en RAM 64 Ko) sans l'indiquer. |
| F-26 | m | osd_menu.h:721-729 | « Aucune image .rom sur la clé » affiché aussi pour les instantanés. |
| F-27 | s | osd_menu.h:39 ; telestrat.c:554, 24 | Commentaires périmés (64/32 vs 40/16 ; aide des touches sans F2/F3) ; osd_menu.h:11 deux phrases fusionnées, :173 commentaire orphelin en double. |
| F-28 | s | telestrat_bench.c:3, 179 | « Mêmes options de compilation » faux (`TELESTRAT_MAX_RAM_BANKS=1` absent) ; « comme la boucle principale » faux (ordre, 20000 vs 19968). |
| F-29 | s | CMakeLists.txt:158-160 | `PICO_HEAP_SIZE=0x5800` défini pour les deux variantes, ligne 160 mal indentée. |
| F-30 | s | divers | Code mort : `TELESTRAT_FRAME_LINES`, `osd_config_merge` (sans `_ex`), `osd_rom_fill`, `rom_pool_free`, `osd_menu_init` (tests seuls). |
| F-31 | s | osd_font.h | Identique à celui du socle (seul le commentaire diffère). |
| F-32 | s | telestrat.c | `files_dropped` recopie `drive_eject`/`tape_eject` ; nettoyage de fin de ligne en double (929-930, 1404-1405) ; copie de `diag_up_name` en double ; trois versions de « @intégrée ou fichier » (`usb_poll`, `state_load`, `rom_user_profile_apply`). |
| F-33 | s | osd_menu_draw (~280 l.), menu_action (~140), osd_menu_key (~125), usb_poll, read_config (~70) | Fonctions trop longues. |
| F-34 | s | telestrat.c:983, 49 ; tusb_config.h:219 | `char msg[48]` masque `msg[96]` ; `RGBA8` sans parenthèses autour des arguments ; alias obsolète `CDC_LINE_CONDING_STOP_BITS_1` ; garde des sondes `time_us_32()==0xFFFFFFFF` (préférer `__attribute__((used))`). |
| F-35 | s | boot2 GENERIC_03H CLKDIV 8 | Code en flash (AY, accès VIA partiels) paie cher chaque défaut de cache : à mesurer, pas un bogue. |

## Zone O — banc PC, outils, tests, build

| Id | Gr. | Où | Constat |
|---|---|---|---|
| O-01 | C | Makefile:13-20 ; tools/reload_socle.sh:14,20-31 ; CMakeLists.txt:10 | Build non reproductible : le socle n'existe que dans `~/.cache/reload-socle` (dépôt local sans remote) ; `.socle-ok` ne vérifie pas le commit ; étiquette écrite en 3 endroits ; `$HOME/reload-emulator` supposé (déplacé dans `~/Projets/`). |
| O-02 | C | tools/fetch_roms.py:73-101 ; Makefile:41-42 | `telestrat_roms.h` non régénérable : ROM `telestrat_atmos` et `telestrat_basic10` sans URL ni source locale ; `FileNotFoundError` non intercepté ; liste OPTIONNELLES (l.88) sans effet ; écriture non atomique ; MD5 au lieu de SHA-256. |
| O-03 | M | menu_pc.h:438-443 | « Enregistrer » écrit 0 octet quand `osd_config_merge_ex` renvoie 0 (tampon 4096 insuffisant) et affiche quand même un succès. |
| O-04 | M | line_tcp.h:82-96, 144-155 ; headless.c:292-305 | `-S listen` : `recv` = 0 ou erreur traité comme « rien », jamais de raccrochage, `fd` reste ouvert, client suivant bloqué ; `send` perd l'octet sur EAGAIN ; non testé. |
| O-05 | M | line_tcp.h:106-115, 55 | `connect:` : seule la première adresse de `getaddrinfo`, `connect` bloquant sans délai, `[::1]:P` non géré, port non validé (`listen:abc` → port 0). |
| O-06 | M | test_menu, test_golden, test_replay, test_telematic, test_minitel_emul, test_rs232, test_tnfs ; Makefile cpu_harte | Un test ignoré (`exit 0`) compte comme réussi. Compteur SKIP + mode `STRICT=1`. |
| O-07 | M | tests/*.sh | Code de retour du banc jamais vérifié, pas de `set -e`, pas de `trap` de nettoyage ; pas de cible ASan/UBSan. |
| O-08 | M | test_telematic.sh:19-24, test_minitel_emul.sh:18-24, test_rs232.sh:22-30, test_boot.sh:79-83 | Tests réseau dépendants du temps réel de l'hôte ; port choisi par ouverture/fermeture d'un socket (TOCTOU) ; perte de touche « non examinée » à `-k 8`. |
| O-09 | M | tools/oracle/oracle.sh:264-273, 289 | `/tmp/neo6502telestrat-oracle` prévisible, binaire exécuté s'il existe, `rm -rf` sur chemin contrôlable par un tiers. |
| O-10 | M | test_tnfs.sh:65-69, 88, 142-143 | `$TMP` jamais supprimé ; compile et exécute des sources tierces depuis l'environnement ; tnfsd peut écouter sur toutes les interfaces (à vérifier). |
| O-11 | m | telestrat_headless.c:452-484, 477 | `atoi` sans contrôle ; `-X 1290` sans `:` silencieux ; `-w` > `-f` sans avertissement. |
| O-12 | m | telestrat_headless.c:577-583 ; replay.c:169-178 | `fopen` non vérifiés ; replay saute la comparaison image/série sans le dire. |
| O-13 | m | telestrat_headless.c:234, 258-262 | Au-delà de 4096 événements clavier la suite est perdue en silence. |
| O-14 | m | telestrat_headless.c:612 | `-M` sans `-U` : le répertoire courant devient clé USB et reçoit `TELESTRA.CFG` et les disquettes. |
| O-15 | m | telestrat_headless.c:854 | `-W` écrit `images[0]` même si le lecteur A a changé ; ignoré sans `-0`. |
| O-16 | m | telestrat_headless.c:337-340, 405-412, 858, 876-883 | `fwrite` / `neo_writer_close` non vérifiés, sortie 0 malgré une perte. |
| O-17 | m | telestrat_headless.c:494, 6-7 | Usage : 8 options listées sur ≈ 35. |
| O-18 | m | telestrat_headless.c:253 ; replay.c:135 | Cycle stocké sur 32 bits : divergence au-delà de 2³² cycles (≈ 72 min). |
| O-19 | m | telestrat_headless.c:701, 776 | Tailles de `info` incohérentes (MAX+1 / MAX). |
| O-20 | m | menu_pc.h:159, 336, 506-510, 630-633, 562 ; headless.c:697 | `neo_file_save` ignoré (modifications de disquette perdues), `realloc` non vérifié, reprise d'instantané ratée sans retour arrière, éjection cassette sans libérer `p->tape`/`tape_file`, `msg` masquant une variable, `-J chemin/X.STA` ne garde que le nom avec `-U`. |
| O-21 | m | line_tcp.h:56-66, 134-141 | `setsockopt` avant test `fd < 0` ; fuite si `bind` échoue ; porteuse restée vraie sur ECONNRESET. |
| O-22 | m | printer_files.h:219-225 ; printer_render.c:301-331 | Répertoire manquant → pages perdues, « 0 page(s) », sortie 0 ; nom tronqué au-delà de 512 octets. |
| O-23 | m | replay.c:204, 188-192, 200 | `ev[0]` lu sans contrôle de taille, `ev_n` non borné, configuration inconnue → « oricutron » en silence, insertion non vérifiée ; cycles au-delà d'un multiple de 20000 ignorés ; tables de configuration dupliquées avec headless.c:150-162. |
| O-24 | m | tools/carte.py:96, 159, 244, 334-347, 139 | Fraîcheur de l'ELF ne regarde pas `src/` ; `mkdtemp` jamais supprimé ; MHz sans masque 32 bits ; `envoyer` sans contrôle de place (file de 256) et `TypeError` sans argument ; dépendance implicite à ImageMagick. |
| O-25 | m | tools/rp2040_load.py:389-390, 196-202, 343, 348 | `addr2line` appelé deux fois ; `blob` peut rester `None` ; division par zéro / `max()` vide si trace < 20000 cycles. |
| O-26 | m | tools/gen_osd_font.py:249 ; tools/bin2h.py, mktap.py | Sortie relative au répertoire courant ; glyphes « maison » quasi morts ; rien ne vérifie que `osd_font.h` correspond au générateur ; `bin2h.py` mort ; `mktap.py` sans contrôle fin > début. |
| O-27 | m | Makefile:33, 16, 113 ; compile_flags.txt | `HEADERS` sans `hid_media.h` ; `reload_socle.sh` exécuté à chaque invocation (même `clean`) ; `CHARGE_DSK` en dur sur `~/oriclib` ; `all: test` lance des serveurs ; ni `-Werror` ni sanitizers ; `compile_flags.txt` pointe vers un chemin absent. |
| O-28 | m | test_printer.sh:41-58 ; test_replay.sh:67 ; test_menu/profiles/tnfs ; png_info.py | Chemin relatif de `png_info.py` ; motif `*.out` non développé ; `sed -i` GNU ; `png_info.py` ignore les filtres PNG et lance `main()` à l'import. |
| O-29 | m | test_telestrat.c:281-297 | `test_bank_ddr_keeps_inputs` tautologique (recalcule la banque lui-même) ; coordonnées d'écran en dur ; état global partagé ; options `-1` à `-3`, `-R`, `-S listen` non testées ; couverture à ajouter pour `rp2040/telestrat_frame.h`, `hayes_line`/`modem_mux`/`drive_set`/`hid_media` hors tests unitaires. |

## Points forts à conserver

Rejeu croisé référence/optimisé cycle par cycle ; image comparée au pixel
à Oricutron ; WD1793 testé avec CRC MFM ; PNG/SVG décodés et vérifiés ; sockets
de test liés à 127.0.0.1 ; ROM vérifiées par somme de contrôle et non
versionnées ; aucune écriture en flash dans le firmware (multi-boot sûr) ;
partage cœur 0 / cœur 1 simple ; chemin du cœur 1 entièrement en RAM ; tampons
presque partout bornés ; commentaires sourcés.
