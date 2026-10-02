# Architecture

## Principe

Même schéma que `oric.uf2` de reload-emulator : sur le Neo6502, le **W65C02S
réel** fait tourner le logiciel ; à chaque cycle, le RP2040 lit l'adresse et
R/W sur le bus (`chips/wdc65C02cpu.h` de reload), répond ou capte la donnée,
puis fait avancer les périphériques émulés. Sur PC, le même système tourne
avec le cœur W65C02S cycle à cycle de reload (`chips/w65c02cpu.h`, validé par
les tests de Klaus Dormann) : c'est le banc de test.

```
src/systems/telestrat.h     système (format header-only de reload)
src/devices/telestrat_fdc.h Microdisc intégré (contrôle $0314/$0318 autour du WD1793 du socle)
src/devices/mos6551acia.h   ACIA 6551 (débit, trame, double tampon, interruptions)
src/devices/minitel_port.h  Minitel sur la prise de l'ACIA + sonnerie de la ligne
src/devices/hayes_line.h    ligne sur modem Hayes (PicoWiFiModemUSB, USB CDC)
platforms/pc/line_tcp.h     ligne du banc sur TCP
src/roms/telestrat_roms.h   généré par tools/fetch_roms.py (non versionné)
platforms/pc/               banc sans écran (tests)
platforms/rp2040/           firmware telestrat.uf2
```

Dépendances de reload-emulator, depuis le socle épinglé (`RELOAD_DIR`,
étiquette `RELOAD_SOCLE`, `docs/adr/ADR-01-socle-reload.md`) : puces
(`chips_common.h`, `kbd.h`, `clk.h`, `w65c02cpu.h`, `mem.h`,
`wdc65C02cpu.h`), cassette (`oric_tape.h`, `oric_tape_rec.h`,
`oric_tape_turbo.h`, v0.16.14), rendu du menu (`osd/osd.h`, grille 120 x 34
par `-DOSD_COLS=120 -DOSD_ROWS=34`, v0.16.14), contrôleur de disquettes
(`devices/wd1793.h`, images MFM_DISK par `devices/oric_dsk.h` ; implémentation
compilée avec `CHIPS_IMPL`, une fois par programme ; v0.16.15, socle
`socle-2026-09-30-5` : DRQ exactement `WD1793_BYTE_US` après l'accès),
`audio.c` (volume), `utils.S`,
`tools/cputest/harte.c`, SDK Pico, PicoDVI, tinyusb. Depuis v0.16.16 (`socle-2026-10-01`) : `ay38910psg.h` et `hid_app.c` aussi
(l'AY reste en flash : `AY38910_HOT` vide, socle `-2` ; +1 Ko de RAM
sinon, sans gain de charge) ; l'AY produit à la cadence réelle du PWM
(`audio_pwm_rate_q8(22050)`, 22 017 Hz à 372 MHz : diviseur au 1/16) ; sans
disque, un type II/III rend `$80` seul (fiche FD179X). Seule copie gardée :
`src/chips/mos6522via.h` (`src/chips/README.md`). Firmware : le rendu du menu du socle est compilé hors ligne
(`OSD_NOINLINE`, `OSD_HOT` = section en RAM) ; en ligne, il était recopié dans
`core1_main` (+1,4 Ko de RAM). Le cœur `w65c02cpu.h` du socle `-5` est commun
avec la NES : IRQ et NMI scrutées à l'avant-dernier cycle, avec le drapeau I
d'avant `CLI` / `SEI` / `PLP`, et plus de détournement du vecteur NMI pendant
`BRK` / IRQ (comportements mesurés sur carte par la NES) ; la version PC
seulement (la carte a le vrai 65C02). Depuis v0.16.21 (`socle-2026-10-01-8`) :
montage de la clé par `msc_app.c` du socle (section « Clé USB ») et
fichiers par `neo_storage` du socle (`src/devices/neo_storage.h`, pilotes
`neo_storage_fatfs.c` sur la carte et `platforms/pc/neo_storage_posix.h` au
banc PC ; section « Menu à l'écran et clé USB »).

## Référence et système optimisé (sprint 4)

- `src/systems/telestrat_ref.h` : **modèle de référence**, simple et figé —
  tous les périphériques avancent tous les 4 cycles. Il n'est plus optimisé ;
  c'est la spécification exécutable.
- `src/systems/telestrat.h` : même comportement, cycle pour cycle, mais un pas
  de 4 cycles « au repos » (VIA stables sans échéance, WD1793 sans événement
  annoncé — `wd1793_next_event_us`, n µs sans changement de DRQ, d'INTRQ ni
  du statut, soit n / 4 pas —, ACIA sans compte à rebours échu, bus de l'AY inactif, pas d'ACK imprimante, aucune entrée
  extérieure changée) est sauté ; sa durée est cumulée puis appliquée d'un
  coup avant le prochain accès en `$03xx` ou le prochain pas complet. RAM et
  banques sont servies par un chemin court ; l'AY avance par échéances.
- Preuve d'équivalence : le banc de référence (`telestrat_headless_ref -B`)
  enregistre des traces ; `tests/replay.c` les fait rejouer au système
  optimisé et compare à chaque cycle l'octet placé sur le bus et la ligne IRQ,
  ainsi que les échantillons audio, les octets série (avec leur cycle) et
  l'empreinte de l'image de chaque trame.
- Seul changement de la référence par rapport au sprint 3 : l'ACIA interroge
  la ligne tous les 64 cycles au lieu de 4 (64 µs contre 8,3 ms par caractère
  à 1200 bauds), sans quoi le système n'est jamais au repos en mode Minitel.
- Le clavier passe par `telestrat_key_down/up` et `telestrat_kbd_update` (et
  non `kbd_*`) pour que le système voie le changement.

## Carte mémoire

| Adresses | Contenu | Source |
|---|---|---|
| `$0000-$02FF` | RAM | |
| `$0300-$030F` | VIA 1 : clavier (PB0-2, PB3), AY (PA, CA2, CB2), imprimante (ORA, STROBE PB4, ACK CA1) | comme l'Atmos ; imprimante : Oricutron `via.c` |
| `$0310-$0313` | WD1793 | Oricutron `disk.c` |
| `$0314` | écriture : INTENA (b0), face (b4), lecteur (b5-6) ; lecture : /INTRQ (b7) | Oricutron `disk.h` |
| `$0318` | lecture : /DRQ (b7) | Oricutron |
| `$031C-$031F` | ACIA 6551 | Oricutron `machine.c` ; fiche 6551 |
| `$0320-$032F` | VIA 2 : PA0-2 = banque (V2DRA `$0321`), PA4 = prise série (0 Minitel, 1 RS232), PB = joysticks, CB1 = sonnerie de la ligne | notice Extension RAM 64 Ko, IV-2 ; TELEMON `$DB3A`, `$DB5D`, `$EEA5` ; Oricutron `joystick.c` |
| autres `$03xx` | reflet du VIA 1 | Oricutron |
| `$0400-$BFFF` | RAM (écran texte `$BB80`, HIRES `$A000`) | |
| `$C000-$FFFF` | banque 0..7 | |

## Banques (notice « Extension RAM 64 Ko », chap. IV-1)

| Banque | Port | Contenu | Config. `standard` | Config. `ram64k` |
|---|---|---|---|---|
| 7 | gauche | TELEMON (banque au RESET) | TELEMON 2.4 | TELEMON 2.4 |
| 6 | gauche | HYPER-BASIC ou FORTH | HYPER-BASIC | HYPER-BASIC |
| 5 | gauche | inutilisée | vide | vide |
| 4 | les deux | inutilisée / extension RAM | vide | RAM |
| 3 | droit | TELEMATIC ou extension RAM | TELEMATIC | RAM |
| 2 | droit | TELE-ASS ou extension RAM | TELE-ASS | RAM |
| 1 | droit | extension RAM | vide | RAM |
| 0 | interne | RAM interne (STRATSED) | RAM | RAM |

- Sélection : `(banque & ~DDRA) | (ORA & DDRA)` sur les bits 0-2 ; une ligne en
  entrée garde sa valeur (comme `via_tele_w_iora` d'Oricutron). Au RESET : banque 7.
- Écriture en ROM ou en banque vide : ignorée. Lecture d'une banque vide :
  **bus flottant**, valeur changeante (pseudo-aléatoire, déterministe). TELEMON
  (`$C2F4`) lit deux fois la page `$FF00-$FFFF` de chaque banque : instable =
  « invalide » (`$10`, valeur que la notice donne pour une banque invalide) ;
  une valeur fixe (`$FF`) la faisait passer pour une ROM, avec un nom parasite
  affiché et un démarrage bloqué dès que TELEMATIC est présent.
- TELEMATIC (8 Ko) : répétée dans les deux moitiés de sa banque (**hypothèse** :
  A13 non décodée) ; TELEMON la compte bien pour 8 Ko (« 56 Ko ROM »).
- TELEMON écrit l'état des banques en `$0200-$0207` (`$0F` = RAM), ce que
  vérifient les tests.

## Choix et écarts

| Sujet | Choix | Raison |
|---|---|---|
| Accès aux banques | pointeurs directs, sans `mem.h` | ROM sans pointeur d'écriture ; plus rapide ; économise la RAM du RP2040 |
| RAM du RP2040 | variante `standard` (1 banque RAM, 4 ROM) : 204 Ko ; `ram64k` (5 RAM, 2 ROM) : 236 Ko (sur 256) | les ROM restent en RAM (`__not_in_flash`) comme dans reload, pour tenir le temps de bus |
| FDC | WD1793 du socle reload (`devices/wd1793.h`, écrit d'après la fiche technique, cas non précisés alignés sur Oricutron) ; délais du socle, par défaut (60 µs avant le premier octet, 32 µs par octet, compté à partir de l'accès du processeur, 180 µs entre deux secteurs, 20 µs pour un type I) ; avancé par pas de 4 cycles (`wd1793_tick_n`) dans la référence comme dans le système optimisé ; fin de multi-secteurs sans erreur ; FORCE INTERRUPT lève toujours INTRQ ; NOT READY levé pendant les commandes de types II et III ; sans disque, type I = NOT READY + SEEK ERROR (sans TRACK 0) ; RESET sans effet sur les disques ni la position des têtes | STRATSED démarre, lit et écrit comme sous Oricutron ; un seul WD1793 pour les projets du socle |
| Images disque | `devices/oric_dsk.h` du socle. PC : image entière en mémoire ; Neo6502 : clé USB, piste courante (6400 o) dans le cache de piste du WD1793, réécrite à la fin de chaque commande d'écriture (`WD1793_FLUSH_AT_END`, défaut du socle) | 1 Mo ne tient pas dans les 264 Ko du RP2040 ; le 65C02 attend pendant l'accès USB (le RP2040 fournit son horloge) |
| Imprimante | option : octet sur front descendant de STROBE, ACK de 40 cycles sur CA1 (retenu si l'imprimante est occupée) ; niveau de CA1 redonné à chaque pas | le VIA de reload ne détecte un front qu'entre deux appels de `set_ca1` ; TELEMON n'affiche « Imprimante » que si l'ACK répond (toujours branchée sur le Neo6502 depuis v0.9.0) |
| Code chaud | `telestrat_tick`, VIA et cœur 65C02 en RAM (`.time_critical`) | comme le BBC de reload : depuis la flash, le cache XIP de 16 Ko déborde |
| Bus du 65C02 (`neo6502_bus.h`) | séquence GPIO de reload, intégrée ; **impulsion OE3 renvoyée juste avant le front descendant** après une lecture | sur carte, la donnée d'une lecture suivie d'une pause (fin de tranche ou de trame, plusieurs ms horloge haute) fuyait : TELEMON lisait `$FF4E` de la banque 3 (`$00`) autrement à la relecture, banque déclarée invalide (« 48 Ko ROM »), titre corrompu ; défaut latent aussi dans reload |
| Clavier : PB3 | touche dans la ligne sélectionnée parmi les colonnes actives (`scan & ligne`) | `oric.h` testait l'égalité (`scan == ligne`), fausse dès que deux touches de lignes différentes sont enfoncées : SHIFT + 8 (« * ») donnait « 8 » sur carte ; aussi dans reload |
| Clavier : CTRL | pas de CTRL+H ni CTRL+M déclarés | ils écrasaient DEL et RETURN (RETURN arrivait comme « M » sur carte) |
| Affichage (cœur 1) | image -> 3 plans 1 bpp (`telestrat_video.h`) -> 3 encodages TMDS 1 bpp ; 960x544 à 372 MHz (1,30 V) ; priorité bus au cœur 1 et au DMA | l'encodage à palette prend 72 µs par ligne pour 59-63 (mesure du BBC) ; mesuré sur carte : 35 µs par ligne, 0 retard |
| Son (AY) | trois voies mélangées en entiers (`ay38910psg_sample_u8`, 85 par voie au plus, somme ≤ 255) ; exactement `sample_rate` échantillons par seconde émulée (horloge fractionnaire, 22 050 Hz par défaut), le système optimisé calcule d'avance le cycle du suivant | reload `a0314e4` : `(uint8_t)(sample * 255)` débordait avec plusieurs voies fortes ; un échantillon tous les 46 cycles donnait 21 739 Hz pour une sortie PWM à 22 050 Hz. Une voie seule sonne 3 fois moins fort qu'avant |
| ACIA | registres et effets de bord d'Oricutron, sans liaison | TELEMON teste l'ACIA au démarrage (avec `$FF` il part dans une routine RAM non installée) |
| FUNCT | touche Windows gauche | `hid_app.c` de reload ne remonte pas Alt en mode ASCII |
| Ligne 4 du clavier | `,` et `.` sans SHIFT, `<` et `>` avec (table `qwktab` d'Oricutron) | `oric.h` de reload les inverse (à corriger aussi dans reload) |
| Vidéo | reprise de `oric_screen_update` ; redessin forcé toutes les 32 trames | clignotement même sans écriture en mémoire écran |

Écart constaté avec Oricutron : sans disquette, Oricutron reste sur
« Drive:A-B-C-D » (son WD simule un disque présent, `MICRODISC_FUDGE`), alors que
ce portage affiche « Inserez une disquette ». À confronter au matériel réel.

## Télématique (sprint 3)

Sur le Telestrat, l'ACIA ne parle pas directement à la ligne : il est relié à
un **Minitel** dont le modem donne la ligne, et la sonnerie arrive sur CB1 du
VIA 2. Relevé dans TELEMON 2.4 :

| Routine | Adresse | Effet |
|---|---|---|
| XRING | `$EEA5` | attend des rafales d'impulsions sur CB1 dont la période (timer 2) est de 19 à 21 ms (50 Hz), une rafale, un silence, une seconde rafale |
| XLIGNE | `$EF20` | envoie au Minitel `ESC 9 $6F` puis `ESC 9 $68` (PRO1 : opposition, connexion) |
| attente de connexion | `$EF47` | vide le tampon de réception, patiente 0,1 s, guette `$13 $53` pendant ~25 s |
| XDECON | `$EF3F` | envoie `ESC 9 $67` (déconnexion) |
| routine série | `$C8C0` | appelée sur IRQ (ACIA ou timer) si le bit 7 de l'état est levé : lit l'octet reçu, émet le suivant si /DCD est bas |
| sélection de prise | `$DB3A`, `$DB5D` | PA4 = 0 : Minitel, commande `$65`, contrôle `$38` (1200 bauds 7E1) ; PA4 = 1 : RS232, contrôle `$1E` (9600 8N1) |

**Modèle de l'ACIA** (fiche 6551 + comportement exigé par TELEMON) : double
tampon d'émission ; IRQ d'émission par **événement** (registre vidé, ou écriture
de la commande avec l'IRQ d'émission autorisée et le registre vide), effacée
par la lecture de l'état — une IRQ de niveau étouffe TELEMON (14 s par octet) ;
bit 7 de l'état levé à l'arrivée d'un octet **même IRQ de réception interdite**
(hypothèse : pendant le service, TELEMON met la commande à `$63`/`$67` et ne lit
les touches du correspondant que par sa routine série appelée par le timer).

**Prise Minitel** (`minitel_port.h`) : filtre les séquences PRO1/PRO2/PRO3 et
envoie vers la prise les séquences de la **STUM 1B** (partie 1, modem ;
récapitulatif des SEP) :

| Événement | Vers la prise |
|---|---|
| PRO1 OPPO | `SEP $50` |
| PRO1 CONNEXION (bascule de ligne) | `SEP $59` |
| porteuse établie (mode opposé : ≥ 3 s de 390 Hz ; standard : 1,7 s de 1300 Hz ; + 80 ms) | `SEP $53` (aussi vers le modem) |
| pas de porteuse en 40 s | second `SEP $59` |
| PRO1 DECONNEXION, perte de porteuse | `SEP $59`, `SEP $53` |

**Fin de session de TELEMATIC** (observé au banc, lu dans le source) : le
serveur termine une communication quand il reçoit `SEP $49` (touche
Connexion/Fin, code `MNTL_CNXFI` = `$49 + $5F`) ; la perte de porteuse seule ne
l'avertit pas (`SEP $59 $53` ignorés) : il revient alors en attente par son
délai d'inactivité (120 s, puis « deconnexion dans 30 sec »). Comportement
d'origine conservé.

**Émulation Minitel** (APLIC 1) : FUNCT+D = XLIGNE (appel sortant), FUNCT+F =
XDECON, RETURN = ENVOI ; les pages s'affichent en HIRES (40 x 25 cellules de
6 x 8, jeu de caractères en `$9800`).

Le délai de porteuse sert aussi TELEMON (attente `$EF47` : vide le tampon puis
patiente 0,1 s). Reste une **hypothèse** : la cadence de sonnerie française
1,5 s / 3,5 s (absente de la STUM, qui ne traite pas l'appel entrant).

**Lignes** : banc PC sur TCP (`listen:` = appels entrants, `connect:` = appels
sortants) ; Neo6502 : modem Hayes en USB CDC (`hayes_line.h` : `ATE0V1`,
`ATS0=0`, `AT$SP=port`, `RING`, `ATA`, `ATD`, `+++`/`ATH` avec gardes de 1,1 s,
`NO CARRIER`). La trame de 20 ms est découpée en tranches de 1 ms pour la
sonnerie.

## Prise RS232 (sprint 5)

PA4 = 1 aiguille l'ACIA vers la prise RS232 (TELEMON `$DB5D` : contrôle `$1E`,
9600 bauds 8N1). Pas de signaux de contrôle (/DCD et /DSR restent actifs).
Banc PC : `-S` sur une liaison TCP directe.

Neo6502, par défaut : **le PicoWiFiModemUSB**, seul modem, partagé avec la
prise Minitel par `src/devices/modem_mux.h`. TELEMON laisse PA4 sur la
dernière prise utilisée (trace `-T` du banc : RS232 au RESET, Minitel dès
l'initialisation, RS232 dès un `SOUT`, sans retour) ; le modem suit PA4, vu à
chaque milliseconde :

| PA4 | Modem |
|---|---|
| 0 (Minitel) | piloté par `hayes_line.h` (ATE0V1, ATS0=0, AT$SP, ATA, ATD) pour TELEMATIC |
| 1 (RS232) | octets bruts dans les deux sens : le logiciel du Telestrat parle Hayes ; ligne Minitel au repos |

Au retour sur la prise Minitel, `hayes_line` est réinitialisé ; une
communication laissée ouverte par la RS232 recevrait ces commandes comme
données. Un modem branché pendant que la RS232 le tient n'est initialisé
qu'au retour sur la prise Minitel.

`TELESTRA.CFG rs232=uext` : UART0 de l'UEXT (GPIO 28/29, carte
`olimex_neo6502` du pico-sdk et `serial.cpp` du firmware officiel), réglé
d'après les registres de l'ACIA (`mos6551acia_format`) à chaque changement ;
l'ACIA cadençant émission et réception au débit programmé, l'UART ne bloque
ni ne déborde (FIFO de 32 octets). Marque et espace, absents de l'UART du
RP2040, sont émis sans parité. Sans ce réglage, l'UART porte les messages du
firmware (stdio).

Protocole de `SSAVE`/`SLOAD` (TELEMON `$EE0A`/`$EE56`, observé au banc) : 50 x
`$16`, `$24`, nom sur 12 octets, `$00`, 7 octets `$052C`-`$0532` (début
en `$052D`, fin en `$052F` ; `$052C` vaut `$40` pour un bloc mémoire, rôle
non vérifié), somme (OU exclusif), données (fin − début octets), somme. `CONSOLE` (XCONSO) :
la liaison devient le terminal (octets reçus affichés, touches émises)
jusqu'à CTRL+C.

## Menu à l'écran et clé USB (sprint 6)

**Clé USB** : stockage de masse monté par FatFs (`msc_app.c` de reload, LUN
0 ; FAT12/16/32, exFAT, noms longs). Le Telestrat ne la voit pas : il voit
les images `.dsk` dans les lecteurs du Microdisc (un `neo_file_t` par
lecteur, `wd1793_insert_streamed_file` avec `neo_file_read_cb` /
`neo_file_write_cb` et ce fichier pour contexte, piste tamponnée partagée)
et les `.rom` dans les banques.

**Volumes de fichiers** (v0.16.21, `neo_storage.h` du socle) : le firmware
n'appelle plus FatFs directement. Volume 0 (`NEO_VOL_USB`) = la clé, pilote
`neo_storage_fatfs.c` du socle (réserve de `NEO_FATFS_FILES` FIL de 88
octets ; chaque écriture est synchronisée). Passent par lui : disquettes en
flux, cassette lue (`neo_file_read_cb`) et enregistrée, imprimante texte
(ajout en fin de fichier), pages FX-80 et tracés MCP-40, instantanés,
`TELESTRA.CFG` (lu ligne à ligne comme `f_gets`, réécrit par
`neo_file_save`), liste du menu (`neo_storage_list`), cartouches `.rom`,
dépôt et relecture par la sonde (`diag_upload_poll` ; `diag_up_status` donne
une cause, 1 à 4, au lieu d'un code FatFs). Plus aucun appel FatFs direct :
un instantané manqué est effacé par `neo_file_remove` (socle `-8`). Les sorties
écrites par petits morceaux (cassette enregistrée octet par octet, PNG et
SVG) passent par `src/devices/neo_writer.h`, un tampon fourni par
l'appelant (64 octets pour la cassette, 512 pour les rendus) : une écriture
synchronisée par tampon plein, au retour en arrière et à la fermeture.
Fichiers ouverts en même temps, comptés pour `NEO_FATFS_FILES` : 4 lecteurs,
cassette lue, cassette enregistrée, imprimante texte, plus un pour les
actions du menu, `TELESTRA.CFG` ou la sonde (les profils et la reprise d'un
instantané ferment leur fichier avant de charger des cartouches), plus la
page ou le tracé en cours hors RAM 64 Ko : 9 (standard), 8 (RAM 64 Ko).
Clé retirée : fichiers fermés (le volume est déjà démonté : aucune
écriture), lecteurs vidés, image en flash dans A. Banc PC : volume 0 = le
répertoire de `-U`, volume 1 = le réseau (`-N`, v0.16.26), volume 2 = les
chemins de la ligne de commande (`-0` … `-3`, `-K` lue en flux, `-C`, `-P`,
`-G`, `-W`, `-X`, `-J`), pilote POSIX ;
traces, images PPM et RAM restent en stdio. Une image n'est jamais dans deux
lecteurs (fichier ouvert en écriture). Au montage : `TELESTRA.CFG` (`a=` …
`d=`, `bank1=` … `bank7=`, `src/osd/osd_config.h`), sinon la première image
dans A (`src/devices/drive_set.h`) ; cartouches chargées, puis RESET à froid.

**Banques de ROM et cartouches** (`src/osd/rom_pool.h`, v0.6.1) : les ROM
intégrées restent en flash (`tools/fetch_roms.py` les génère `const`) ;
chacune est copiée au démarrage dans un emplacement de 16 Ko en RAM, d'où le
65C02 lit sans attente. Une `.rom` de la clé (16 Ko, ou 8/4/2/1 Ko répétées
comme TELEMATIC) **réutilise l'emplacement de la ROM qu'elle remplace** ; un
emplacement supplémentaire (la « banque USB supplémentaire ») sert à une
cartouche dans une banque vide ou de RAM (aucun avec `TELESTRAT_RAM64K`).
Contenu d'origine : la ROM est recopiée depuis la flash, sinon l'emplacement
est libéré. `telestrat_set_bank_rom` / `telestrat_restore_bank` changent la
banque à chaud.

**RESET du menu = à froid** (`telestrat_cold_reset` : RAM effacée puis RESET).
Observé au banc : après un RESET à chaud, TELEMON n'inventorie pas les banques
et n'affiche que « Logiciel ecrit par Fabrice BROCHE ». Ce message vient de
TELEMON (`$C392` : affichage puis `jmp $C398`, boucle sans fin), application
par défaut (vecteur `$C386`) quand il n'y a rien à lancer.

**STRATSED sans TELE-ASS** (v0.8.1, comparé à Oricutron par
`tools/oracle/oracle.sh`) :

| Banques 1-5 | 6 | 7 | Notre banc | Oricutron |
|---|---|---|---|---|
| RAM (1-4), TELE-ASS (5) | HYPER-BASIC | TELEMON | menu « 1- HYPER-BASIC 2- TELE-ASS » | identique |
| RAM (1-5) | HYPER-BASIC | TELEMON | arrêt après « HYPER BASIC V2.0b » | **identique** |
| vides (config. `telemon` + cartouche) | HYPER-BASIC | TELEMON | arrêt après la liste | — |
| RAM (1-4), banque 5 vide (`ram64k`) | HYPER-BASIC | TELEMON | « Logiciel ecrit par Fabrice BROCHE » | non simulable (Oricutron n'a pas de banque vide) |

L'arrêt sans TELE-ASS est donc reproduit par l'oracle : ce n'est pas un
défaut propre à notre émulation. Comportement d'un vrai Telestrat : **non
connu**. Le cas `ram64k` (celui du firmware RAM 64 Ko) reste ouvert.

**Menu (OSD)**, `src/osd` en C pur, testé sur PC :

| Fichier | Rôle |
|---|---|
| `osd_font.h` | police 8 x 8 générée par `tools/gen_osd_font.py` : texte en **unscii-8** de Viznut (domaine public, `tools/fonts`), ASCII et Latin-1 ; filets arrondis et icônes dessinés pour le projet. Pixels deux fois plus hauts que larges (lignes répétées par PicoDVI), comme le texte CGA |
| `osd.h` | surface de 120 x 34 cellules (caractère, encre, fond, fond tramé, grandes lettres) ; `osd_render_line` : une ligne de tampon dans les 3 plans 1 bpp |
| `osd_menu.h` | état, navigation (touches -> actions) et dessin : lecteurs, banques, clé, boutons, sélecteur de fichiers défilant |
| `osd_config.h` | `.rom` dans une banque, fusion de `TELESTRA.CFG` (lignes du menu remplacées, autres gardées) |

Rendu : plein écran, 960 x 272 lignes de tampon (960 x 544 affichées), à la
place de l'image du Telestrat, sur le cœur 1. Par cellule, l'octet du glyphe
est recopié dans les trois octets d'un mot par une multiplication (un cycle
sur le RP2040) puis masqué par l'encre et, complémenté, par le fond (tables de
40 mots, trame comprise). La surface (12 Ko) occupe l'image du Telestrat,
inutile pendant la pause ; la police et les tables sont en RAM. Pas de menu en
800x480 (`TELESTRAT_VIDEO_480`) : la surface ne tient pas.

Mémoire : cinq emplacements de 16 Ko (quatre ROM + un supplémentaire) au
lieu de quatre ROM en RAM et deux emplacements (v0.6.0). v0.7.0 : les
tampons de `TELESTRA.CFG` (4 Ko) sont pris dans l'image du Telestrat, menu
ouvert ; la variante RAM 64 Ko liste 32 fichiers de la clé (64 sinon). RAM
libre : environ 16,7 Ko (standard), 2,2 Ko (RAM 64 Ko), plus le tas de 2 Ko.
v0.9.0 (enregistreur, imprimante) : 14 Ko (standard), **208 octets** (RAM
64 Ko) — la variante RAM 64 Ko n'a plus de marge.

## Volume Réseau TNFS (v0.16.26)

Second volume de `neo_storage` (`NEO_VOL_NET` = 1), servi par le client TNFS
du socle (`devices/neo_tnfs.h`, `neo_storage_tnfs_ops`) ; la clé reste le
volume 0, les deux s'utilisent en même temps.

| | Banc PC | Carte, variante standard |
|---|---|---|
| Transport | UDP (`platforms/pc/neo_tnfs_udp.h` du socle) | second port série USB du modem Neo6502picowifi, une trame par datagramme : longueur sur 2 octets petit-boutiste puis le datagramme (`neo_dgram_serial.h`) |
| Serveur | `-N hôte[:port]` | `TELESTRA.CFG reseau=hôte[:port]` |
| Montage | au lancement, avant `TELESTRA.CFG` | `net_poll`, une tentative par branchement du modem |

**Ports série du modem** : `neo_cdc_serial.c` du socle (avec
`CFG_TUH_CDC=2`) remplace l'accès CDC direct de `telestrat.c` : port 0 =
le modem AT (ligne Minitel et RS232, `hayes_line.h`, `modem_mux.h`), port 1
= le port TNFS (interface 2). Équivalences pour la ligne : écriture
`neo_cdc_write((void *)0, …)` (attend, en faisant tourner `tuh_task`, si la
file d'émission de 128 octets est pleine, au plus 1 s ; l'ancien accès
perdait l'excédent), lecture `neo_cdc_read_byte((void *)0, 0)` (sans attente,
octet par octet au lieu de 64), montage et retrait vus par `modem_watch` à
chaque trame (`neo_cdc_ready`) au lieu des rappels `tuh_cdc_mount_cb` /
`tuh_cdc_umount_cb`, que `neo_cdc_serial.c` définit ; seuls les ports du
premier appareil série sont pris. DTR + RTS et 9600 8N1 sont posés à
l'énumération pour chaque port (`CFG_TUH_CDC_LINE_CONTROL_ON_ENUM`) : le port
TNFS ne répond qu'avec DTR levé. La variante RAM 64 Ko utilise aussi
`neo_cdc_serial.c`, avec `CFG_TUH_CDC=1` et sans TNFS. Depuis v0.16.27, les
octets vers le modem passent par une file (`modem_txq`, `byte_fifo.h`) vidée
par `modem_flush` entre deux tranches de 1 ms d'émulation et en fin de
trame : `neo_cdc_write`, qui peut attendre en faisant tourner `tuh_task`
(rappels du clavier : RESET, instantané…), n'est plus appelé depuis l'ACIA au
milieu d'un cycle du 65C02 (signalé par reload). File pleine (1 Ko ; 64 o en
RAM 64 Ko) : octets perdus, comme avant v0.16.26.

**Activation** (`net_poll`, comme `net_poll` de l'Oric de reload) : modem
branché, `reseau=` lu, ligne au repos (ni appel, ni sonnerie, ni prise RS232,
menu fermé) : dialogue AT par `neo_esp_at.h` sur le port 0 (`AT`, `ATE0`,
attente d'une adresse IP par `AT+CIPSTATUS`, 30 s au plus) ; port TNFS
absent mais `AT$TNFSUSB?` répond 0 : `AT$TNFSUSB=1` puis `AT+RST` (une fois ;
le modem redémarre, la tentative suivante vient à sa nouvelle énumération) ;
port présent : `AT$TNFS="hôte",port`, `MOUNT` TNFS. La ligne Minitel est
ensuite réinitialisée (`modem_mux_attach` : `ATE0V1`, `ATS0=0`, `AT$SP`).
Différence avec l'Oric : pas de repli sur l'UDP du port AT (`neo_esp_xfer`)
pour un modem sans second port, qui prendrait la ligne Minitel. Modem
retiré : volume plus prêt, lecteurs et cassette du réseau éjectés (ceux de
la clé restent ; et l'inverse au retrait de la clé, `files_dropped`).

**Noms** : un fichier du réseau est `net:/NOM` (`NEO_NET_PREFIX` + `/` de
`neo_storage.h`, `OSD_NET_PREFIX` du menu) partout où un nom est gardé :
liste du menu, `drive_name`, `tape_name`, `pool.name` (cartouches),
`TELESTRA.CFG` (`a=` … `d=`, `bank1=` … `bank7=`), texte des instantanés.
`neo_file_open_path` choisit le volume d'après le préfixe ; un nom sans
préfixe reste la clé (rétrocompatible : les deux points n'existent pas dans
un nom FAT). Disquettes du réseau en flux (`wd1793_insert_streamed_file` et
`neo_file_*_cb`, au banc comme sur la carte), cassette lue en flux,
cartouches et instantanés lus entiers. Les écritures (disquette,
`TELESTRA.CFG` du menu, nouvel instantané, cassette enregistrée,
imprimante) vont sur la clé, sauf la piste réécrite d'une disquette du
réseau. Au premier montage du réseau, les lecteurs `net:/` de
`TELESTRA.CFG` restés vides sont remplis, les cartouches `net:/` chargées,
puis démarrage à froid si l'un ou l'autre a eu lieu ; aux montages suivants,
les lecteurs seulement. Un lecteur réglé sur le réseau n'est pas rempli par
la première image de la clé au montage de celle-ci.

**Menu** (`osd_menu.h`) : `net_present` (volume prêt) fait passer Entrée sur
un lecteur, la cassette, une banque ou « Instantanés » par la page
`OSD_PAGE_SOURCE` (Clé USB / Réseau, curseur sur la source du fichier en
place). Le choix rend `OSD_ACT_SOURCE` ; pour le réseau la plate-forme
relit la liste (`neo_storage_list` du volume 1, à la suite des fichiers de
la clé, `osd_file_t.vol` = 1) puis rouvre le sélecteur
(`osd_menu_browse`). Le sélecteur ne montre que les fichiers de la source,
sans préfixe ; les ROM intégrées seulement avec la clé. Sans réseau, la
page de la source n'existe pas : mêmes écrans qu'avant (comparés pixel par
pixel au banc). La liste est commune (`OSD_MENU_FILES` : 40 sur la carte) :
le réseau prend les places laissées par la clé.

**Mémoire** (v0.16.26, `--print-memory-usage`) : standard 234 260 octets de
RAM (+3 348 : `neo_tnfs_t`, `neo_esp_t`, trames, second port CDC de
TinyUSB) ; RAM 64 Ko 238 908 (+16).

## STRATORIC et banques vides (sprint 10)

**STRATORIC** (dépôt jedeoric/stratoric, `B7STRA40.ROM`) : cartouche du mode
Atmos du Telestrat d'après le manuel du développeur (F. Broche, 1987, page 3) :
banque 7 SEDORIC + démarrage, 6 ORIC BASIC V1.1, 5 ORIC BASIC V1.0. Intégrée
(`rom_builtin.h`) comme « cartouche complète » : choisie en banque 7, elle
charge aussi les banques 6 et 5 (emplacement supplémentaire). Au banc :
« STRATORIC V4.0 », BASIC, cassette, disquette SEDORIC (3D Munch).

**Banque vide et démarrage à froid d'HYPER-BASIC** (enquête v0.9.1) : sans
`BONJOUR.COM` sur la disquette (`FLGTEL` `$020D` bit 2), HYPER-BASIC lancé
seul appelle `$C000` de la banque 5 (`$FFAC` : EXBNK) — là où la cartouche
« TELE-ASS gauche » met TELE-ASS. Banque 5 vide (cartouche HYPER-BASIC seule
à gauche, cartouche RAM 64 Ko à droite : configuration de la notice), notre
bus flottant renvoie des octets pseudo-aléatoires, l'exécution s'égare et
TELEMON est relancé, sans langage : « Logiciel ecrit par Fabrice BROCHE ».
Avec deux langages, TELEMON affiche son menu et entre autrement dans
HYPER-BASIC (pas d'appel à la banque 5) ; avec un `BONJOUR.COM`, la variante
RAM 64 Ko démarre (« 44 Ko libres »). Ce que renvoie une banque vide sur un
vrai Telestrat n'est pas établi par une mesure. Indices : « Telestrat, le
système m'était conté » (pages 1-7 à 1-9, lues par OCR) commente la détection
des banques (`$C2F4`) : un octet est lu, relu après une attente (« le code
a-t-il été rafraîchi ou gardé ? »), et une banque dont les octets changent est
« à ignorer » ; le même livre décrit `07,92,C3` comme l'adresse par défaut du
RESET à chaud, exécutée « avant qu'une application ait été lancée ». Les
concepteurs attendaient donc des valeurs instables d'une banque vide, ce que
notre bus flottant imite ; l'appel d'HYPER-BASIC à la banque 5 vide y
exécuterait aussi des octets instables. Déduction, pas vérification.

## Confrontation au livre « Telestrat à cœur ouvert » (v0.10.2)

Annexes II (matrice du clavier), V (carte mémoire), VI (structure) et VII
(brochage des E/S) de G. Meister, comparées à l'émulation :

| Point | Livre | Émulation | Arbitre |
|---|---|---|---|
| VIA 1 : CA1 ACK, CB1 entrée K7, CA2/CB2 AY, PB0-3 clavier, PB4 STROBE, PB6 relais K7, PB7 émission K7 | idem | idem | concordant |
| VIA 2 : PA0-2 banque (0 = RAM), CB1 appel Minitel | idem | idem | concordant |
| E/S : VIA 1 `$0300`, FDC `$0310`, ACIA `$031C`, VIA 2 `$0320` | idem | idem | concordant |
| **VIA 2 PA4** | « 0 = RS 232 ; 1 = MINITEL » | 0 = Minitel | **TELEMON** : `$DB3A` met PA4 à 0 avec 1200 bauds 7E1 (Minitel), `$DB5D` à 1 avec 9600 8N1 (RS232) — le livre inverse |
| **Joysticks** | « PB6 port droit, PB7 port gauche » | PB6 gauche, PB7 droit | **TELEMON** : `JCGVAL` (joystick gauche) lu par `Ldf90` (PB6), la souris (boutons PA5/PA7) par `Ldf99` (PB7) ; Oricutron idem — le livre inverse |
| Matrice du clavier | 8 x 8 | identique | concordant ; l'émulation déclare en plus `\`, `]`, `[` (touches de l'Atmos, absentes du Telestrat selon le livre) : sans effet |
| ACIA : DCD « arrêt d'émission », DTR « ACIA active » | idem | idem (émission si /DCD bas) | concordant |
| MIDI (VIA 2 CA1, CB2, PA3, PA6), souris | décrits | non émulés | — |

## Périphériques dans le menu (sprint 11)

Panneau « Périphériques » : imprimante et modem, activés ou coupés par
Entrée, enregistrés par « Enregistrer » (`impression=oui|non`,
`modem=oui|non`, `osd_config_merge_ex`). Imprimante coupée : octets jetés
(TELEMON ne voit pas la différence). Modem coupé : une communication en
cours est raccrochée (`hayes_line_hangup` : `+++`, `ATH`), puis la ligne ne
sonne plus, ne compose plus, n'émet ni ne reçoit (prise Minitel, et prise
RS232 quand elle passe par le modem) ; réactivé : modem réinitialisé. État
affiché : absent, prêt, sonnerie, en ligne, prise RS232. Au banc : `-P`
(imprimante) et `-L` (ligne TCP) suivent les mêmes options.

RAM : 13,6 Ko libres (standard), **88 octets** (RAM 64 Ko).

## Touches multimédia du clavier (v0.16.9)

Volume +, Volume − et Muet (page HID « Consumer », usages `0xE9`, `0xEA`,
`0xE2`) ne passent pas par le rapport « boot » du clavier : le clavier les
envoie sur une autre interface HID, décrite par son propre descripteur de
rapport. `hid_app.c` de reload ne les lit pas ; le projet en a une copie
(`platforms/rp2040/src/hid_app.c`, reload `33f1623`) qui lit le descripteur
de chaque interface au branchement (`src/devices/hid_media.h`, formes
tableau et variable, identifiants de rapport), décode ses rapports et
remet les touches nouvellement enfoncées à `hid_media_key_down()`. Une telle
interface n'est plus prise pour une manette. Corrigé au passage : un rapport
d'une interface sans manette enregistrée déréférençait un pointeur nul
(signalé à reload, corrigé là-bas au commit `a833b45`).

Le volume (`hid_volume_t`, 9 niveaux de 0 à 8 : gain 0, 23, 32, 45, 64, 91,
128, 181, 256 / 256, et une coupure à part) s'applique à chaque échantillon
avant la sortie PWM. Depuis v0.16.10, c'est celui de `audio.c` de reload
(`2c35d91`, commun à tous ses systèmes : `audio_set_volume`,
`audio_volume_level`, `audio_volume_serial`, et `hid_media_key_down`), qui
définissait le même rappel que `telestrat.c` : édition des liens en échec
tant que les deux existaient. Le bandeau (`osd_volume_banner`) s'affiche 2 s
par-dessus celui de la cassette, à chaque touche (même aux butées). Le
niveau est enregistré dans `TELESTRA.CFG` (`volume=`, v0.16.10) ; le PC, sans
son, garde la ligne telle quelle. Rien ne change dans
l'émulation (rejeu identique) ni dans la charge (`make charge` : 58 %).

Mémoire : tampon d'énumération USB porté à 512 octets (standard) pour les
descripteurs de plus de 256 octets, que TinyUSB ignorerait (v0.16.20 : 512
aussi en RAM 64 Ko, marge 976 octets) ; 256 en RAM 64 Ko jusque-là,
où il reste 480 octets au-delà du tas réservé (608 avant) ; 376 en v0.16.10
(`audio_push_sample` de reload placé en RAM). Deux interfaces
multimédia au plus, quatre champs chacune.

Limites : une touche multimédia envoyée dans le rapport principal du clavier
(clavier en protocole « report ») n'est pas vue, le clavier étant mis en
protocole « boot ».

**Sur carte** (2026-09-30, v0.16.12) : le clavier de bmarty présente ses
touches multimédia sur l'interface 1, rapport 1, tableau d'un usage de 16 bits
(forme du descripteur Consumer de TinyUSB) ; appuis reçus, jauge et volume
vérifiés par bmarty ; `volume=5` enregistré par le menu puis relu après un
reset (lu par la sonde : niveau 5, gain 91). Même séance : 65C02 à
1,000 MHz, cœur 0 à 60-62 % en moyenne, 73 % au pire pendant `DIR` sur la
clé, 0 ligne DVI en retard.

## Premiers essais sur carte depuis la v0.4.2 (sprint 16)

**RESET du 65C02 cadencé** (v0.16.31, `neo6502_bus.h`) : le W65C02S ne prend
RESB en compte que s'il reste bas 2 cycles d'horloge au moins ; l'horloge
n'avançant que dans `neo6502bus_tick`, le RESET lui donne 8 cycles, bus de
données non piloté (OE3 haut). Avant : RESB bas 1 ms sans horloge ; un RESET
après le démarrage (F12) pouvait être ignoré (constaté sur l'Apple IIe de
reload ; ici non mesuré).

**Touches de commande hors de l'émulation** (v0.16.28) : la lecture en flux
d'une piste ou d'une cassette (clé : `wait_for_disk_io` ; réseau :
`neo_cdc_read_byte`) fait tourner `tuh_task` pendant l'émulation, donc les
rappels du clavier. F1-F3, F11, F12 et les touches du menu sont mis en file
(`key_cmd_q`, 8) et exécutés par `keys_service` dans la boucle principale ;
derrière une commande en attente, les touches suivantes (appuis et relâchés)
attendent aussi, pour garder l'ordre. Les touches du Telestrat restent
immédiates, comme une frappe réelle. Même défaut trouvé par reload sur l'Oric.

**VIA du socle** (v0.16.29, `via6522`, étape 9 de la fusion avec reload) :
les deux VIA, dans la référence comme dans le système optimisé, sont celles
du socle (`chips/via6522.c`), par une couche d'accès `_tv_*`
(`telestrat.h`, `telestrat_ref.h`) écrite par la session reload sur une copie
de ce dépôt. Clavier (VIA 1) et manettes (VIA 2) sont lus par les rappels du
port B ; CA1 (ACK de l'imprimante) est au repos haut ; CA1 et CB1 ne sont
donnés qu'au changement (mode paresseux gardé). Les pas au repos utilisent
`via_quiet_cycles` / `via_advance` (même résultat que des pas de 4 cycles).
Raison : avec `mos6522via` avancée par pas de 4, le timer 1 en roue libre
avait une demi-période de N + 8 cycles au lieu de N + 2 (N = 25 : 32 ; 100 :
108 ; 1 000 : 1 008), sons du timer trop graves, et la référence avait le même
défaut (le rejeu ne pouvait pas le voir) ; trouvé par reload, vérifié ici.
Charge (`make charge`) : 56 % en moyenne, 63 % au pire, contre 59 / 67 %.
Firmware (`TELESTRAT_VIA6522_PLACE`) : VIA en RAM dans la variante
standard (sur carte : 59 %, contre 64 % en flash ; le modèle de `make charge`
suppose la flash dans le cache XIP et sous-estime son coût) ; dans la variante
RAM 64 Ko, où tout en RAM déborde et tout en flash ne tient pas le temps réel
(113 %, 0,875 MHz sur carte), placement partiel depuis v0.16.30 : chemin de
chaque pas en RAM (`VIA6522_HOT` : `via_advance`, `via_update`…), accès aux
registres en flash (`VIA6522_HOT_ACCESS` vide), tampon audio de 1 Ko
(`SAMPLES_BUFFER_SIZE`) et `DIAG_TX_SIZE` de 128 (364 octets de marge) ; les
deux réglages ont été ajoutés au socle à notre demande. `TELESTRAT_VIA6522=OFF` garde l'ancienne
`mos6522via` en repli. Instantanés (format 4) : les rappels et pointeurs de
la VIA (`porta_read`… `irq_userdata`) ne sont pas repris de l'instantané.
Banc : à 4 trames par touche, le second S d'« ESSAI » peut se perdre (la ROM
ne voit pas le relâchement ; phase changée par le minutage juste des IRQ) :
`test_boot` tape ces lignes à 6 trames par touche.

**VIA : IER** (v0.16.13, correctif de reload `d95caf9`) : écrire IER pour
interdire une source dont le drapeau est levé relâche l'IRQ (fiche 6522 :
IRQ = OU de IFR & IER) ; avant, l'IRQ restait levée jusqu'à l'acquittement.
STRATSED masque les deux VIA (`$7F` en IER) avant une lecture de secteur,
autorise l'INTRQ du WD1793 puis fait `CLI` (`$D55A`) : avec l'ancien défaut,
l'IRQ restée levée le faisait entrer dans son gestionnaire (`$D58A`) avant la
fin de la commande ; il suit maintenant son chemin de lecture par DRQ
(`$D55C`). Ce changement de timing a révélé que `test_menu` dépendait du
hasard : TELEMON + HYPER-BASIC seul, sans `BONJOUR.COM`, fait exécuter la
banque 5 vide (bus flottant, voir « Banque vide et démarrage à froid »), dont
l'issue dépend du cycle exact (« 195 Ko RAM, 118 Ko ROM » après un RESET à
froid). Le test pose maintenant TELE-ASS en banque 5. Enquête : option `-Q`
(démarrage à froid à une trame, référence comprise) et comparaison des
traces des deux versions ; le modèle de référence donne le même résultat que
le système optimisé (test_replay C).

**Cœur 1 sans flash** (v0.16.19) : `tools/core1_flash.py` du socle (reload)
liste ce que le cœur 1 atteint en flash ; il trouvait `memset` (bordures
noires de `telestrat_frame_line`), remplacé par des écritures de mots
volatiles (0 octet de RAM en plus, au lieu d'environ 1,2 Ko avec
`PICO_MEM_IN_RAM`). Les deux points restants de l'outil, dans PicoDVI, sont sans effet :
le littéral en flash est le message de `panic()` (« TMDS free queue full in
IRQ! »), lu seulement si le firmware s'arrête ; l'appel indirect est
`dvi0.scanline_callback`, que le Telestrat ne définit pas.

**Verrous des files DVI** (v0.16.11) : `dvi_init` reçoit deux verrous
dédiés (`spin_lock_claim_unused`) au lieu de `next_striped_spin_lock_num()`,
dont les verrous 16-23 sont partagés avec FatFs et TinyUSB : le cœur 1
pouvait attendre le cœur 0 interruptions masquées (lignes en retard, traits
rouges pendant les rafales disque, vus par Neo6502Trinity). Signalé par la
session reload-emulator (règle 2 de `docs/STRATEGIE-NEO6502.md` de reload) ;
aucun symptôme mesuré ici.

**Mémoire au démarrage** : `dvi_init` alloue ses tampons TMDS par `malloc`
(3 x 3 x 480 mots = 17 280 octets) et `malloc` (newlib) demande la mémoire au
système par pages : mesuré sur carte, il faut ~21,8 Ko de tas (21 204 octets
échouent, « Out of memory » dans `dvi_init` ; 21 800 passent). Les chiffres de
« RAM libre » des versions 0.9 à 0.15 n'en tenaient pas compte : le firmware
ne démarrait plus. `PICO_HEAP_SIZE` (0x5800 ; 0x3400 avec 2 tampons pour la
RAM 64 Ko) réserve maintenant le tas à l'édition des liens : un manque de RAM
la fait échouer (« region RAM overflowed ») au lieu de planter la carte.
Regagné pour cela : journal des accès en `$03xx` (8 Ko) compilé seulement avec
`-DTELESTRAT_DIAG_IO`, `diag_tx` de 4 à 1 Ko, liste des fichiers de la clé à
40 (28 en RAM 64 Ko), bande de la FX-80 à 28 lignes, un seul volume FatFs
pour la clé (lecteur « 0: », 588 octets au lieu de 3,5 Ko), FatFs du projet
(`third_party/fatfs`, R0.15 de ChaN) réglé par son `ffconf.h` : `FF_FS_TINY`
(les fichiers partagent le tampon du volume : 512 octets de moins par
fichier ouvert ; 8 ou 9 FIL de 88 octets dans la réserve de
`neo_storage_fatfs.c` depuis v0.16.21) et noms longs de 64 caractères ; tampons du
modem USB (CDC) de 128 octets ; en RAM 64 Ko, 16 fichiers dans le menu et
`diag_tx` de 256 octets. Tas disponible : 31,6 Ko (standard) et 23,1 Ko (RAM
64 Ko), 22,5 Ko réservés dans les deux. La RAM 64 Ko avait d'abord été
essayée avec 2 tampons TMDS : image défectueuse (plus de marge pour la
sortie DVI, comme l'avait signalé la session BBC) ; avec 3, image propre.

**Clé USB** : avec le TinyUSB de reload-emulator (2023), la clé répondait à
l'INQUIRY (36 octets, un paquet) mais son premier READ10 (512 octets, 8
paquets) n'aboutissait jamais, avec ou sans clavier. TinyUSB 0.21.0
(sous-module `third_party/tinyusb`, `PICO_TINYUSB_PATH`), dont le pilote hôte
RP2040 a été refondu (hathach/tinyusb#3561), lit la clé, seule ou avec un
clavier. Le montage est celui du socle (`msc_app.c`, depuis v0.16.21 ; notre
`usb_msc.c`, qui l'avait précédé, est retiré) : `f_mount` hors du rappel
d'INQUIRY (`msc_poll`, appelé par la boucle principale), un seul volume
(`MSC_VOLUMES=1`, lecteur « 0: »), délai de 1 s par transfert, volume
démonté au retrait (le rebranchement remonte la clé). La sonde lit l'adresse
USB de la clé dans `msc_slot_addr[0]` (`carte.py cle`). Écarts avec
`usb_msc.c` : le statut (CSW) d'un transfert n'est plus vérifié (une réponse
en erreur passe pour réussie), un montage en attente n'est pas annulé si la
clé part avant lui, compteurs `msc_diag` et `msc_mount_result` supprimés.

**Démarrage** : la clé se monte 2-3 s après la mise sous tension, quand
TELEMON a déjà demandé sa disquette : une disquette insérée au premier
montage provoque un démarrage à froid dessus.

**Sonde** (`tools/carte.py`) : `deposer` copie des fichiers sur la clé de la
carte (morceaux de 16 Ko chargés dans l'image du Telestrat par
`load_image`, écrits par le firmware ; 1 Mo en 8 s) ; `relire` les relit ;
`flasher` refuse un ELF absent ou plus ancien que les sources (un flash sur
une édition des liens en échec avait effacé la carte). Pendant un transfert,
l'émulation est en pause et l'écran montre les données ; image ou menu sont
redessinés ensuite.

**Essayé sur carte** (clé + clavier + écran HDMI, 960x544) : 65C02 à 0,998 MHz,
cœur 0 à 43 %, cœur 1 à 30 µs par ligne, aucune ligne DVI en retard ;
`TELESTRA.CFG` appliqué ; page « Démarrer sur… » au clavier ; STRATSED depuis
la clé, `DIR`, `PRINT 6*7`, `SAVE` (fichier écrit sur la disquette de la clé),
`LPRINT` vers une page PNG de la FX-80 relue et valide ; cassette rapide
(« L'Aigle d'Or » chargé dès l'insertion, BASIC 1.1) ; instantané enregistré
en plein jeu (70 Ko, registres cohérents) puis repris : le jeu revient à
l'écran enregistré et continue — le NMI détourné marche sur le vrai
W65C02S (ordre des cycles conforme à l'émulation). Non expliqué : une
pression d'Entrée perdue une fois sur la page de démarrage (pas reproduite).

## Choix des ROM au démarrage (sprint 15)

**Profils** (`rom_builtin.h`, `rom_profiles`) : banques 1-7 remises à
l'origine, puis une cartouche complète en banque 7 (`@stratoric`,
`@atmos` ; rien pour le Telestrat d'origine) ; démarrage à froid ensuite.
`TELESTRA.CFG` `demarrage=choix` : au premier montage de la clé, le menu
s'ouvre sur la page « Démarrer sur… » (le sélecteur de fichiers,
pseudo-élément `OSD_ITEM_BOOT`, entrées `-100 - profil`) ; Échap ou la
première ligne gardent la configuration de la clé. `demarrage=ID` : profil
appliqué sans page ; identifiant inconnu : ignoré.

**Profils de la clé** (v0.15.2) : `profil=Libellé;bank7=X;…` (trois au
plus), proposés après les profils intégrés ; `rom_user_profile_apply`
remet les banques 1-7 d'origine puis charge chaque `bankN=` (ROM intégrée,
ou fichier par le chargeur de la plate-forme). Seuls les libellés sont gardés
en mémoire (120 octets) : la ligne est relue dans `TELESTRA.CFG` quand on
choisit le profil. `read_config` lit des lignes de 160 caractères et saute
la fin d'une ligne plus longue (elle n'est plus lue comme une autre ligne).
RAM libre : 8,1 Ko (standard), 672 octets (RAM 64 Ko).

**ORIX, écarté (v0.15.1)** : les ROM ORIX 1.0 d'Oricutron démarrent jusqu'au
shell, mais tous leurs fichiers passent par un CH376 en `$0340`/`$0341`
(contrôleur USB/SD de la carte Twilighte), absent d'un Telestrat d'origine.
Plutôt qu'émuler une extension que la vraie machine n'a pas, ORIX a été
retiré (décision du PO).

## Instantanés (sprint 14)

**Touches rapides** (v0.16.22) : F2 enregistre, F3 reprend le dernier
(`osd_state_latest` : celui enregistré ou repris en dernier s'il est encore sur
la clé, sinon le plus grand `ETATnnnn.STA`). Les deux passent par le menu,
dont l'image du Telestrat est la zone de travail : F2 laisse le menu ouvert
sur le résultat, F3 le referme si la reprise réussit.

**Registres du processeur** (`telestrat.h`, `telestrat_cpu_capture` /
`telestrat_cpu_restore`) : le 65C02 du Neo6502 est une vraie puce, ses
registres ne se lisent pas. On les lui fait écrire, ou charger, par un NMI
détourné : pendant quelques dizaines de cycles, une variante du pas
(`_telestrat_tick_snoop`, hors de la RAM : aucun coût pour `telestrat_tick`)
sert elle-même le bus.

- Capture : NMI ; le processeur finit son instruction, empile PCH, PCL, P
  (trois écritures en `$0100+S`, `S-1`, `S-2`, relevées : PC, P et S), lit le
  vecteur en `$FFFA` : on répond `$FF00`, où l'on sert `STA $FF00`,
  `STX $FF00`, `STY $FF00` (écritures relevées : A, X, Y ; pas faites en
  mémoire) puis `RTI`, qui dépile P et PC de la vraie pile. Le programme
  reprend où il était, avec les mêmes registres, 25 à 40 cycles plus tard.
- Restitution (machine déjà chargée) : NMI ; lectures servies sans effet de
  bord, écritures jetées (empilements compris) ; au vecteur, `LDX #S-3`,
  `TXS`, `LDA #A`, `LDX #X`, `LDY #Y`, `RTI` ; les trois lectures de pile
  du `RTI` sont servies (P, PCL, PCH de l'instantané). La RAM chargée n'est
  pas touchée.

Le même code tourne au banc sur le 65C02 émulé cycle à cycle, où les
registres internes sont lisibles : `tests/test_telestrat.c` vérifie A, X, Y,
S, P et l'adresse de reprise, et qu'une machine relue repart exactement
comme l'originale (même RAM, mêmes registres, même timer après 7000 cycles).
Sur la carte, le NMI est tenu bas par `MOS6502CPU_SET_NMI` (GPIO 27)
pendant la prise en compte. Reste à vérifier sur le vrai 65C02 : l'ordre des
cycles du NMI et du `RTI` (empilements puis `$FFFA`, pile dépilée en S+1…S+3),
supposé identique à l'émulation.

**Fichier** (`src/systems/telestrat_state.h`) : en-tête (« TELESTRAT ETAT »,
version, signature des tailles de structures), texte de la plate-forme
(`bank1=` … `bank7=` : cartouches, remises à la reprise ; `a=` … `d=`,
`cassette=` : pour information), registres, RAM, banques de RAM, VIA 1 et
2, AY et ACIA (rappels de la plate-forme gardés), clavier, registres du
WD1793 (piste, secteur, données, lecteur, face, têtes, `$0314` ; tampon de
piste vidé sur la disquette), cadence (compteur de cycles, échéances de
l'AY, horloge audio fractionnaire depuis la version 2 du format, v0.16.8 : les
instantanés antérieurs sont refusés ; version 3 : registres du WD1793 du
socle, têtes `head[]`), STROBE et ACK. Pas enregistrés : disquettes et cassette (supports),
enregistreur, joystick, ligne. Refusé pendant une commande du WD1793
(`state != WD1793_IDLE`). Sur
le Neo6502, le fichier (`neo_file_t`) et le texte sont pris dans l'image du Telestrat
(menu ouvert), vérifié à la compilation (`_Static_assert`).

RAM libre : 8,3 Ko (standard), 860 octets (RAM 64 Ko).

## Cassette rapide et moteur sans relais (sprint 13)

**Cassette rapide** (`src/devices/oric_tape_turbo.h`) : dans la ROM ORIC
EXTENDED BASIC V1.1, `$E6C9` lit un octet de la bande (résultat dans A et
`$2F`, X et Y préservés, C = 0 sans erreur de parité) et `$E735` cherche la
synchro (bits jusqu'à un `$16`, puis trois `$16` ; X = 0 au retour, rangé par
l'appelant en `$02B1`, drapeau d'erreur). Relevé sur la ROM désassemblée
(da65). L'option remplace ces routines dans la copie en RAM de la banque :

    $E6C9  AD FE 03   LDA $03FE      octet suivant
           85 2F      STA $2F
           18         CLC
           60         RTS
    $E735  A2 00      LDX #0
           AD FF 03   LDA $03FF      0 : bande après une synchro ; $80 : aucune
           30 FB      BMI (attente, comme la ROM qui cherche)
           60         RTS

Le système répond en `$03FE`/`$03FF` seulement l'option active (sinon ces
adresses restent des reflets du VIA 1) ; les octets d'origine servent de
signature (autre ROM : rien n'est touché) et reviennent quand l'option est
coupée. Le lecteur (`oric_tape.h`) : `oric_tape_turbo_sync` place la bande
après la prochaine suite d'au moins trois `$16`, `oric_tape_turbo_byte`
rend les octets ; le signal sur CB1 est suspendu (`turbo_hold`) jusqu'à
l'arrêt du moteur, pour que la lecture directe et le signal ne se disputent
pas la bande. Sur le Neo6502, aucun coût dans la boucle du bus : les ROM
intégrées sont déjà copiées en RAM (`rom_pool`), le patch est appliqué à
chaque fermeture du menu et au montage de la clé. Les deux traitements rares
(registres de la cassette rapide, démarrage et arrêt du moteur) sont hors de
la RAM (`TELESTRAT_COLD`) : le code chaud a perdu 880 octets.

**Moteur toujours en marche** : `telestrat_tape_options(sys, turbo,
motor_always)` ; le moteur vaut alors 1 quel que soit PB6. L'enregistreur
(`CSAVE`) ferme son fichier après le dernier octet, comme avant.

RAM libre : 8,4 Ko (standard), 956 octets (RAM 64 Ko).

## Imprimantes émulées : Epson FX-80 et MCP-40 (sprint 12)

Trois modèles derrière le port Centronics (`imprimante_type=`, menu) : Texte
(octets bruts dans `IMPRIM.TXT`), Epson FX-80 (`src/devices/printer_fx80.h`)
et table traçante MCP-40 (`src/devices/plotter_mcp40.h`), en C pur, communs
au firmware et au banc (`platforms/pc/printer_files.h`,
`platforms/pc/printer_render.c`). Sortie par `printer_out_t`
(`printer_out.h` : ouvrir un fichier d'extension donnée, écrire, revenir en
arrière, fermer) ; noms `IMPRnnnn.PNG` / `.SVG`, une seule suite.

**FX-80** : unités 1/1440 pouce en largeur (toutes les densités : 60, 72,
80, 90, 120, 240 points par pouce) et 1/216 pouce en hauteur (`ESC 3`,
`ESC J`) ; page rendue à 144 points par pouce (1224 x 1584, 1 bit). Seule
une bande de 32 lignes de pixels (4,9 Ko) est en mémoire : quand la tête
descend, les lignes qu'elle ne peut plus atteindre (9ᵉ aiguille et double
frappe comprises) sont écrites. Le PNG est écrit au fil de l'eau sans
compresser : hauteur fixe (longueur de page, `ESC C`), donc taille du bloc
`IDAT` connue d'avance, blocs deflate « stockés » (65535 octets au plus),
CRC-32 et Adler-32 calculés en continu (table de 16 mots, en flash) ; une
page fait 244 Ko. Page ouverte au premier point (pas de fichier pour une
page blanche ; les lignes blanches déjà sautées sont écrites à l'ouverture).
`fx80_busy()` : un saut de ligne attend l'écriture des lignes quittées ;
`fx80_service(n)` en écrit au plus n. Retour à la ligne automatique : le
caractère attend (`pending`) que les lignes soient écrites.

**MCP-40** : aucun tracé en mémoire. Un `<path>` par suite de traits de même
plume et même pointillé, un `<text>` (police « monospace », largeur imposée
par `textLength`) par suite de caractères ; coordonnées en pas (0,2 mm),
Y vers le haut (inversé dans le SVG). L'en-tête, de longueur fixe, est
réécrit à la fermeture avec l'étendue verticale du tracé (retour en
arrière : `f_lseek`).

**Firmware** : les octets passent par la file (`byte_fifo.h`) ; à chaque
trame, `printer_render()` les interprète et écrit les lignes tant que la
trame a du temps (au moins un pas par trame, puis jusqu'à 18 ms). File
presque pleine : le système retient l'ACK (`printer.busy`, `printer_wait`)
jusqu'à `telestrat_printer_resume()` ; TELEMON n'envoie l'octet suivant
qu'à l'interruption CA1 (routine `$CA2F`), l'Oric attend donc sans perte —
la file de la variante RAM 64 Ko passe de 256 à 64 octets. Fin de travail
(page, fichier SVG terminés) : saut de page, ouverture du menu, 10 s sans
octet. Changement de modèle : travail en cours terminé ; clé retirée :
travail abandonné. Le rendu (FX-80 et MCP-40 en `union`, 5,3 Ko, plus un
fichier et son tampon de 512 octets depuis v0.16.21) n'existe pas dans la
variante RAM 64 Ko (Texte seul).

RAM libre : 7,9 Ko (standard), 92 octets (RAM 64 Ko), plus le tas de 2 Ko.

## Imprimante et clé retirée (sprint 9)

**Imprimante** : le système appelle `printer_out` à chaque octet (STROBE,
ACK sur le VIA 1) ; le firmware le met dans une file (`byte_fifo.h`, 1 Ko ;
256 octets avec la RAM 64 Ko) vidée à chaque trame dans le fichier
`imprimante=` de la clé (`IMPRIM.TXT`, ouvert en ajout, `f_sync`) : aucune
écriture de fichier pendant un cycle du 65C02. File pleine : octets perdus,
comptés (depuis v0.12.0 : ACK retenu, plus de perte). L'imprimante est toujours branchée (TELEMON l'annonce au démarrage).

**Clé retirée** : `msc_app.c` de reload ne redescend pas
`msc_inquiry_complete` ; la présence est suivie par `tuh_msc_mounted`. Au
retrait : lecteurs de la clé vidés (l'image en flash revient dans A),
cassette éjectée, enregistrement et impression arrêtés, drapeau remis à
zéro. Au rebranchement (drapeau relevé par `msc_app.c`) : `TELESTRA.CFG` et
lecteurs relus, cartouches et machine inchangées. Non testable au banc.

## Cassette et mode Atmos (sprint 7)

Le Telestrat a une prise cassette DIN, comme l'Atmos, gérée par le VIA ; le
chargement se fait avec la cartouche Atmos (TELEMON et HYPER-BASIC n'ont pas
de chargeur de cassette) — information du PO (Wikipédia, Defence-Force,
documentation cc65), non vérifiée par nous sur matériel.

**Mode Atmos** : ORIC EXTENDED BASIC V1.1 (ROM d'`oric.uf2`, en flash) en
banque 7, puis RESET à froid : la banque 7 est celle du RESET, et le matériel
que la ROM attend (VIA en `$0300`, clavier, ULA) est celui du Telestrat.
Observé au banc : « ORIC EXTENDED BASIC V1.1 … 37631 BYTES FREE ».

**Lecteur** (`src/devices/oric_tape.h`, implémentation du projet) : faits du
format relevés chez Oricutron (`tape.h`, GPL : aucune ligne de code reprise)
— un bit commence sur un front montant, deux alternances de 208 cycles (1) ou
416 (0) ; un octet = 1, 0, 8 bits (bit 0 d'abord), parité, 1, 1, 1 ; moteur
démarré sur une synchro : 80 octets de synchro de plus ; environ 1281 cycles
d'alternances courtes après l'en-tête ; fin : deux alternances. La ROM
(`$E71C`, désassemblée) attend le drapeau CB1 du VIA 1 (front montant, PCR =
`$10`), lit T2H (≥ `$FE` : bit 1) et relance T2.

Dans le système : moteur = `ORB & DDRB & $40` (au RESET, PB6 en entrée se lit
à 1 et ne doit pas lancer le moteur) ; chaque bascule est un événement à son
cycle (multiple de 4), qui borne la fenêtre de repos ; le niveau est redonné
à CB1 à chaque pas, comme la sonnerie (le VIA de reload garde le front
jusqu'à l'appel suivant : sans cela, le drapeau se relève aussitôt effacé).
Sans cassette, rien ne change (rejeu identique). En-tête `.tap` : adresse de
fin **incluse** (sinon la ROM attend un octet de plus, observé).

**Enregistreur** (`src/devices/oric_tape_rec.h`, v0.8.0) : `CSAVE` écrit sur
PB7 du VIA 1 (routine `$E65E` de la ROM, sortie du timer 1). Mesuré au banc :
une période (entre fronts montants) de 432 cycles pour 1, 640 pour 0 — pas les
416 / 832 de la lecture ; trame de **13 bits** : 0, 8 bits (bit 0 d'abord),
parité (1 si le nombre de 1 est pair), 1, 1, 1 (désassemblage de `$E65E`).
Décodage : seuil à 536 cycles ; calage sur la trame exacte d'une synchro
`$16` (sans quoi une suite de `$16` se découpe de façon cohérente mais
décalée) ; trame fausse : calage perdu avant l'en-tête, octet perdu après.
L'en-tête reconnu, le fichier `NOM.TAP` (lettres, chiffres, `-`, `_`) est
ouvert sur la clé et reçoit 3 synchros, `$24`, l'en-tête, le nom et les
données ; fermé après le dernier octet ou à l'arrêt du moteur. PB7 est lu à
chaque pas tant que le moteur tourne : ses fronts tombent au passage à zéro
du timer 1, qui borne déjà la fenêtre de repos.

Plusieurs parties : « L'Aigle d'Or » (`AIGLEDOR.1`, 14 903 octets, puis
`AIGLEDOR.2`, 39 679) se charge en entier au banc (la partie 2 après les
écrans d'introduction), jusqu'à « Aventurier,ton nom? ».

**Bandeau** : pendant que le moteur tourne, une rangée de texte (icône, nom,
barre, pour cent) dans la marge sous l'image, dessinée par le cœur 0 à chaque
trame, rendue par le cœur 1 comme une ligne du menu (8 lignes de tampon, même
coût). La composition d'une ligne de sortie (menu, bandeau, image centrée)
est commune au firmware et au banc (`telestrat_frame.h`, option `-D`).

## Démarrage sur disquette (observé au banc)

TELEMON fait un RESTORE sur les lecteurs 3 à 0 (`$0314` = `$E4`, `$C4`, `$A4`,
`$84`), copie un chargeur en `$B800`, puis lit la piste 0 secteur 1 (en boucle
tant qu'il n'y a pas de disquette : « Inserez une disquette »). Ce secteur
charge STRATSED en banque 0, qui charge ensuite le menu des langages.
