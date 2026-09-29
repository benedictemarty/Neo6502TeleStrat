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
src/devices/wd1793.h        WD1793 sur images MFM_DISK (mémoire ou flux)
src/devices/telestrat_fdc.h Microdisc intégré (contrôle $0314/$0318 autour du WD1793)
src/devices/mos6551acia.h   ACIA 6551 (débit, trame, double tampon, interruptions)
src/devices/minitel_port.h  Minitel sur la prise de l'ACIA + sonnerie de la ligne
src/devices/hayes_line.h    ligne sur modem Hayes (PicoWiFiModemUSB, USB CDC)
platforms/pc/line_tcp.h     ligne du banc sur TCP
src/roms/telestrat_roms.h   généré par tools/fetch_roms.py (non versionné)
platforms/pc/               banc sans écran (tests)
platforms/rp2040/           firmware telestrat.uf2
```

Dépendances reprises de reload-emulator sans copie : `mos6522via.h`,
`ay38910psg.h`, `kbd.h`, `clk.h`, `chips_common.h`, cœurs 65C02, `hid_app.c`,
`audio.c`, `utils.S` (rendu 3x de l'Oric), SDK Pico, PicoDVI, tinyusb.

## Référence et système optimisé (sprint 4)

- `src/systems/telestrat_ref.h` : **modèle de référence**, simple et figé —
  tous les périphériques avancent tous les 4 cycles. Il n'est plus optimisé ;
  c'est la spécification exécutable.
- `src/systems/telestrat.h` : même comportement, cycle pour cycle, mais un pas
  de 4 cycles « au repos » (VIA stables sans échéance, FDC et ACIA sans compte
  à rebours échu, bus de l'AY inactif, pas d'ACK imprimante, aucune entrée
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
| FDC | WD1793 écrit d'après la fiche technique (Oricutron, GPL, n'est qu'un oracle de comportement) ; délais d'Oricutron (32 cycles/octet) ; fin de multi-secteurs sans erreur comme Oricutron ; le registre de piste doit correspondre à l'ID (fiche) | STRATSED démarre, lit et écrit comme sous Oricutron |
| Images disque | PC : image entière en mémoire ; Neo6502 : clé USB, piste courante (6400 o) en tampon, réécrite à la fin de chaque commande d'écriture | 1 Mo ne tient pas dans les 264 Ko du RP2040 ; le 65C02 attend pendant l'accès USB (le RP2040 fournit son horloge) |
| Imprimante | option : octet sur front descendant de STROBE, ACK de 40 cycles sur CA1 ; niveau de CA1 redonné à chaque pas | le VIA de reload ne détecte un front qu'entre deux appels de `set_ca1` ; TELEMON n'affiche « Imprimante » que si l'ACK répond (désactivée sur le Neo6502) |
| Code chaud | `telestrat_tick`, VIA et cœur 65C02 en RAM (`.time_critical`) | comme le BBC de reload : depuis la flash, le cache XIP de 16 Ko déborde |
| Bus du 65C02 (`neo6502_bus.h`) | séquence GPIO de reload, intégrée ; **impulsion OE3 renvoyée juste avant le front descendant** après une lecture | sur carte, la donnée d'une lecture suivie d'une pause (fin de tranche ou de trame, plusieurs ms horloge haute) fuyait : TELEMON lisait `$FF4E` de la banque 3 (`$00`) autrement à la relecture, banque déclarée invalide (« 48 Ko ROM »), titre corrompu ; défaut latent aussi dans reload |
| Clavier : PB3 | touche dans la ligne sélectionnée parmi les colonnes actives (`scan & ligne`) | `oric.h` testait l'égalité (`scan == ligne`), fausse dès que deux touches de lignes différentes sont enfoncées : SHIFT + 8 (« * ») donnait « 8 » sur carte ; aussi dans reload |
| Clavier : CTRL | pas de CTRL+H ni CTRL+M déclarés | ils écrasaient DEL et RETURN (RETURN arrivait comme « M » sur carte) |
| Affichage (cœur 1) | image -> 3 plans 1 bpp (`telestrat_video.h`) -> 3 encodages TMDS 1 bpp ; 960x544 à 372 MHz (1,30 V) ; priorité bus au cœur 1 et au DMA | l'encodage à palette prend 72 µs par ligne pour 59-63 (mesure du BBC) ; mesuré sur carte : 35 µs par ligne, 0 retard |
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
9600 bauds 8N1). Liaison directe, sans modem ni signaux de contrôle (/DCD et
/DSR restent actifs). Banc PC : `-S` sur TCP. Neo6502 : UART0 de l'UEXT
(GPIO 28/29), réglé d'après les registres de l'ACIA (`mos6551acia_format`) à
chaque changement ; l'ACIA cadençant émission et réception au débit
programmé, l'UART ne bloque ni ne déborde (FIFO de 32 octets). Marque et
espace, absents de l'UART du RP2040, sont émis sans parité.

Protocole de `SSAVE`/`SLOAD` (TELEMON `$EE0A`/`$EE56`, observé au banc) : 50 x
`$16`, `$24`, nom sur 12 octets, `$00`, 7 octets `$052C`-`$0532` (début
en `$052D`, fin en `$052F` ; `$052C` vaut `$40` pour un bloc mémoire, rôle
non vérifié), somme (OU exclusif), données (fin − début octets), somme. `CONSOLE` (XCONSO) :
la liaison devient le terminal (octets reçus affichés, touches émises)
jusqu'à CTRL+C.

## Démarrage sur disquette (observé au banc)

TELEMON fait un RESTORE sur les lecteurs 3 à 0 (`$0314` = `$E4`, `$C4`, `$A4`,
`$84`), copie un chargeur en `$B800`, puis lit la piste 0 secteur 1 (en boucle
tant qu'il n'y a pas de disquette : « Inserez une disquette »). Ce secteur
charge STRATSED en banque 0, qui charge ensuite le menu des langages.
