# Tests

`make` (ou `make test`) compile et lance tout. Aucune carte n'est nécessaire.

## Cœur 65C02 du PC — `make cpu_harte` (2 540 000 tests)

SingleStepTests de Tom Harte (https://github.com/SingleStepTests/65x02, jeu
`wdc65c02/v1`) : 10 000 tests par opcode, avec l'accès au bus de chaque cycle,
passés sur la copie locale `src/chips/w65c02cpu.h` (programme
`~/reload-emulator/tools/cputest/harte.c`, détails dans
`~/reload-emulator/docs/CPU-TESTS.md`). Données (≈ 2 Go) dans
`~/.cache/65x02` (`HARTE_DIR`), téléchargées par
`~/reload-emulator/tools/cputest/fetch_harte.sh` ; absentes : test ignoré.

- Échoue s'il reste un échec fonctionnel : registres et RAM finals, nombre
  de cycles, octets écrits. Journal complet : `build/harte_c02.log`.
- `$5C` est exclu (`-x 5c`) : 4 cycles dans les tests, 8 dans le cœur.
  Mesuré sur le W65C02S de la carte (session reload-emulator, 2026-09-30,
  routine chronométrée par une VIA : 1, 10 et 20 fois `5C 00 00` = 24, 96 et
  176 cycles, soit 8 par instruction) : le cœur est juste, les tests ne
  valent pas pour ce processeur.
- Les écarts de lecture factice (482 360) sont signalés sans compter comme
  échecs. Sur le Telestrat, ils ne comptent que s'ils tombent sur un registre
  d'entrée-sortie ; ce cas n'a pas été vérifié.
- Contre-épreuve (v0.16.7) : l'ancien cœur échoue sur 25 opcodes (SBC et
  BBR/BBS).

Seule la version PC utilise ce cœur. Sur la carte, c'est le vrai W65C02.

## Tests unitaires — `tests/test_telestrat.c` (455 vérifications)

Programme 6502 synthétique en banque 7, exécuté par le cœur W65C02S de reload :

- banque 7 au RESET et après exécution ;
- commutation par V2DRA depuis la RAM (`$0400`) : écriture/relecture en banque
  RAM, écriture ignorée en ROM, `$FF` en banque vide, retour en banque 7 ;
- DDRA partiel : les lignes en entrée gardent la banque précédente ;
- son (v0.16.8) : exactement 22 050, 44 100 et 11 025 échantillons en une
  seconde émulée, 1er échantillon au bon cycle ; mélange de l'AY : une voie
  au maximum = 85, trois = 255 (sans repli sur 8 bits), silence = 0 ;
- touches multimédia (v0.16.9) : descripteur Consumer de TinyUSB (tableau
  16 bits, identifiant 3, après un rapport System Control), sans identifiant,
  forme variable (un bit par usage) ; Volume +, Volume −, Muet reconnus,
  autres touches et autres identifiants ignorés ; volume : butées, coupure
  levée par Volume + ou −, gain croissant ; bandeau du volume (jauge, son coupé) ;
- `volume=` de `TELESTRA.CFG` (v0.16.10) : valeurs 0 à 8 acceptées, 9, vide,
  `5x`, `-1` refusées ; écriture qui remplace l'ancienne ligne ; volume -1
  (PC sans son) : ligne existante gardée ;
- FDC sans disque : `$0314`/INTRQ, INTENA, non prêt, adresses hors FDC ;
- FDC sur une disquette MFM synthétique (CRC calculés) : en-tête refusé,
  SEEK avec vérification, STEP OUT, lecture d'un secteur (contenu exact, fin de
  DRQ, statut), multi-secteurs jusqu'à la fin de piste, secteur introuvable,
  READ ADDRESS, réécriture identique = image inchangée (CRC exacts), écriture et
  relecture, protection, FORCE INTERRUPT, lecteur vide ;
- WRITE TRACK (formatage `$F5`/`$F7`) puis lecture ; CRC de l'ID formaté ;
- mode flux : lecture par rappel, pas de relecture sur la même piste, piste
  réécrite une fois après WRITE SECTOR, relue en mode mémoire, protection ;
- ACIA : valeurs au RESET, RESET logiciel (parité gardée, IRQ interdites) ;
  1200 bauds 7E1 = 8333 cycles par caractère, IRQ d'émission à l'écriture de
  la commande, effacée par l'état, double tampon, émission sur 7 bits au bon
  rythme, bit 7 levé à la réception avec IRQ de réception interdite, broche
  IRQ avec `$65` jusqu'à la lecture de la donnée, DTR inactif ; format de
  liaison pour l'UART de la prise RS232 (1200 7E1, 9600 8N1, 19200 8N2, un seul
  stop avec 8 bits + parité) ;
- prise Minitel (fausse ligne), séquences de la STUM 1B : sonnerie (fronts à
  50 Hz pendant 1,5 s puis silence), XLIGNE décroche, SEP `$50` puis `$59`,
  pas de `$53` avant 3 s en mode opposé, SEP `$53` vers la prise et le modem,
  données et séquences Videotex transmises, PRO2 filtrée, touches reçues,
  SEP `$59 $53` au raccrochage, appel sortant (1,7 s en standard), XDECON,
  échec après 40 s sans porteuse ;
- modem Hayes (faux modem) : initialisation (`AT$SP`), RING et sa fin, ATA,
  CONNECT (sans `\n` parasite), données, NO CARRIER en ligne, ATD, `+++`/ATH
  avec gardes, BUSY, pas d'appel sortant sans numéro ;
- un modem pour les deux prises (`modem_mux.h`, faux modem) : pas
  d'initialisation Hayes tant que la RS232 le tient, initialisation au retour
  sur la prise Minitel, RING vu par `hayes_line`, octets bruts dans les deux
  sens sur la RS232 (CONNECT non interprété, pas de temporisation Hayes),
  ligne Minitel au repos, réinitialisation au retour, modem débranché ;
- affectation des lecteurs au démarrage (`drive_set.h`) : première image en
  A, `b=`/`d=` sans casse, nom inconnu ignoré, image demandée deux fois ;
- cartouche changée à chaud (`telestrat_set_bank_rom`), lecture seule,
  contenu d'origine rendu (ROM, RAM), banque courante vidée ;
- ROM intégrées (`rom_builtin.h`) : STRATORIC charge les banques 7, 6 et 5,
  noms court et identifiant, plus de place avec deux emplacements ;
- emplacements de banque (`rom_pool.h`) : ROM intégrée copiée, le 65C02
  tourne depuis l'emplacement, cartouche à la place d'une ROM (même
  emplacement), 8 Ko répétés, emplacement supplémentaire, plus de place,
  taille refusée d'abord, ROM recopiée de la flash, emplacement libéré,
  lecture échouée ;
- menu (`src/osd`) : UTF-8 vers la police, rendu d'une ligne (encre, fond
  tramé en damier, grandes lettres), navigation (Échap, Entrée, lettre,
  Suppr, gauche/droite, boutons, défilement), sélecteurs filtrés (.dsk / .rom),
  image déjà dans un autre lecteur signalée ; `.rom` répétée, tailles
  refusées, fusion de `TELESTRA.CFG` ;
- file d'octets de l'imprimante (`byte_fifo.h`) : bloc contigu, tour du
  tampon, file pleine (octets perdus comptés) ;
- Epson FX-80 (`printer_fx80.h`) : CRC-32 et Adler-32 de référence ; PNG
  décodé par le test (CRC des blocs, en-tête zlib, blocs stockés, Adler,
  1224 x 1584) ; pas de page pour des sauts de ligne seuls ; « A » en haut à
  gauche (marge de 1/4 pouce), « B » deux lignes plus bas, reste blanc ;
  `ESC K` (aiguilles 1 et 8, pas de 1/60 pouce) ; `ESC &` sauté ; `ESC !`,
  SI, SO (annulé par LF) ; `ESC 3`, `ESC A`, `ESC J` ; `ESC D` et HT ;
  81ᵉ caractère sur la ligne suivante et imprimé ; 70 lignes = deux pages ;
  écriture limitée par appel ; ouverture refusée sans blocage ;
- MCP-40 (`plotter_mcp40.h`) : carré du manuel (`D`), SVG complet, en-tête
  réécrit (étendue, hauteur en mm) ; `C1`, `L2`, `J`, `I` ; mode texte (40
  colonnes, interligne de 24 pas), `P` de taille `S1`, caractères échappés,
  `Q1` ; origine fixée par `CHR$(18)` ; taille `S3` gardée en mode texte ;
  pas de fichier sans tracé ;
- menu : modèle d'imprimante et dernière page affichés ; Entrée fait
  défiler texte, FX-80, MCP-40, coupée ; `imprimante_type=` écrit et lu ;
- cassette (`oric_tape.h`) : trames (parité), moteur arrêté, synchro
  prolongée et fin d'en-tête, premier front, durées des alternances,
  silence après l'en-tête, fin de bande, arrêt en cours d'octet, éjection ;
- enregistreur (`oric_tape_rec.h`) : fronts synthétisés comme la ROM (432 /
  640 cycles, trames de 13 bits) : fichier `JEU1.TAP` (nom nettoyé), `.tap`
  complet et fermé après le dernier octet, trame à parité fausse rejetée sans
  perdre la suite, nom vide (`SANSNOM.TAP`), moteur arrêté, silence ;
- menu : périphériques (imprimante sous la banque 1, modem à droite, Entrée :
  actions, panneau : état) ; `TELESTRA.CFG` : `impression=`, `modem=`,
  valeurs oui / non ;
- profils de démarrage : par identifiant, STRATORIC (SEDORIC, BASIC 1.1, 1.0
  en 7, 6, 5), retour au Telestrat (banque 5 vide) ; profils de la clé
  (libellé, ROM intégrée et fichier, ROM inconnue et fichier absent refusés) ; page « Démarrer sur… » :
  configuration de la clé, deuxième profil, titre, Échap ;
- instantanés : capture des registres par NMI (A, X, Y, S, P comparés au
  65C02 émulé, reprise à la bonne adresse, moins de 60 cycles), le programme
  continue ; restitution (registres remis, RAM intacte, la boucle reprend) ;
  capture après un NMI de F11 ; fichier en mémoire : texte de la plate-forme
  relu, machine relue puis 7000 cycles = même RAM, mêmes registres, même
  timer que la suite d'origine ; autre variante, fichier tronqué, accès
  disque refusés ; menu : ligne Instantanés, sélecteur, « enregistrer » ;
- cassette rapide (`oric_tape_turbo.h`, `oric_tape.h`) : pas de synchro
  sans cassette ni moteur ; synchro puis octets de deux programmes, signal
  suspendu puis libéré à l'arrêt du moteur, bout de bande ; synchro demandée
  pendant la lecture du signal ; patch de BASIC 1.1 (reconnu, appliqué deux
  fois sans effet, retiré = ROM d'origine), autre ROM intacte ; moteur
  toujours en marche puis relais ; menu : navigation et panneau ;
  `cassette_rapide=`, `cassette_moteur=` écrits ;
- menu : ligne Cassette (sélecteur des `.tap`, insertion, éjection, barre),
  ROM intégrées d'abord dans le choix d'une banque, lettre, bandeau (icône,
  nom, barre, fond tramé) ;
- banque vide instable (bus flottant) ;
- rendu de l'écran identique au rendu d'origine (copie d'oric_screen_update)
  sur 600 écrans aléatoires : texte, HIRES, attributs série, double hauteur,
  jeu alternatif, inversion, clignotement.

## Tests de démarrage — `tests/test_boot.sh` (33 vérifications)

Vraies ROM (`tools/fetch_roms.py`), 300 trames (6 s émulées), 4 configurations.
Écran texte attendu : « TELEMON V2.4 », « (c) 1986 ORIC International »,
« Drive:A-B-C-D », « Inserez une disquette », et la ligne mémoire propre à
chaque configuration :

| Config. | Attendu | Référence |
|---|---|---|
| `oricutron` | 128 Ko RAM, 48 Ko ROM | écran d'Oricutron 1.2.0 (rev. 002279f) avec les mêmes ROM |
| `ram64k` | 128 Ko RAM, 32 Ko ROM ; `$0200-$0204` = `00 0F 0F 0F 0F` | notice Extension RAM 64 Ko (II-5, V-10) |
| `standard` | 64 Ko RAM, 56 Ko ROM | somme des ROM de la notice (16+16+16+8) |
| `telemon` | 64 Ko RAM, 16 Ko ROM | |

### Sur disquette (12 vérifications)

Image `STRATSED.DSK` (MFM_DISK, 2 faces x 80 pistes, STRATSED V2.0c,
md5 `0c121a969a2b28e37ad831ee31ce660e`), prise dans l'archive locale
`~/oriclib/games/dsk/` (ou `STRATSED_DSK=...`) ; tests ignorés si absente.
Référence : écran d'Oricutron sur la même image (« STRATSED V2.0c », « HYPER
BASIC V2.0b », « TELEASS V1.0a », menu « 1- HYPER-BASIC / 2- TELE-ASS / Votre
choix: », « Imprimante,Drive:A-B-C-D » avec imprimante).
Puis : « 44 Ko libres », `PRINT 6*7` → 42, `DIR` → 88 fichiers ; `SAVE` sur une
copie puis, dans une nouvelle session, `LOAD`/`RUN` et `DIR` → 89 fichiers ;
`LPRINT` → fichier imprimante.

## Télématique de bout en bout — `tests/test_telematic.sh` (7 vérifications)

Configuration standard (TELEMATIC en banque 3), `STRATSED.DSK`, arborescence
`DEMO` chargée, serveur lancé ; ligne `-L listen:PORT` (port libre) ; le
correspondant `tests/minitel_client.py` appelle, attend la page, envoie ENVOI,
puis Connexion/Fin (`SEP $49`), raccroche. Vérifié : `ESC 9 o ESC 9 h` émis, `SEP $50 $59 $53` reçus, page
d'accueil (« SERVEUR REALISE ENTIEREMENT… »), réponse à ENVOI (« taper quelque
chose avant ENVOI »), `SEP $59 $53` au raccrochage, retour à « Attente de
communication » ; `ESC 9 g` (XDECON) émis à la fin de session.

## Émulation Minitel en appel sortant — `tests/test_minitel_emul.sh` (4 vérifications)

TELEMATIC `APLIC 1` (configuration standard, `STRATSED.DSK`) ; serveur de test
`tests/minitel_server.py` (port libre, fichier `.pret` une fois à l'écoute) ;
ligne `-L connect:`. FUNCT+D (`\fD` dans `-t`), puis « BONJOUR » et RETURN
(ENVOI). Vérifié : `ESC 9 o ESC 9 h` émis, page du serveur affichée, `BONJOUR`
+ `SEP $41` reçus par le serveur, sa réponse « RECU: BONJOUR » affichée. L'écran
de l'émulation est en HIRES : lu par `tests/hires_texte.py` dans l'image de la
RAM (`-r`), cellule par cellule contre le jeu de caractères en `$9800`.

## Prise RS232 — `tests/test_rs232.sh` (7 vérifications)

HYPER-BASIC (configuration standard, `STRATSED.DSK`), prise RS232 du banc
reliée par `-S connect:` au correspondant `tests/rs232_peer.py` (port libre,
fichier `.pret`) ; trois sessions, touches à 8 trames (`-k 8`, sinon les
lettres doublées se perdent) :

| Session | Vérifié |
|---|---|
| `SOUT 65:SOUT 66`, puis `SSAVE "ESSAI",A#9000,E#9010` d'un bloc « ABCDEFGHIJKLMNOP » | « AB » reçu ; en-tête : 50 x `$16`, `$24`, « ESSAI » ; données du bloc |
| `SOUT 33:SLOAD` (le correspondant renvoie le fichier au « ! »), puis lecture de `#9000` | nom « ESSAI    COM » affiché ; mémoire restituée |
| `CONSOLE`, « HELLO » tapé | « HELLO » reçu ; réponse « SALUT » affichée |

## Menu et clé USB — `tests/test_menu.sh` (14 vérifications)

Un répertoire tient lieu de clé (`-U`) : `STRATSED.DSK`, `hyperbas.rom`,
`teleass.rom`, une `.rom` de 1000 octets. Configuration `telemon` (TELEMON
seul) :

| Étape | Vérifié |
|---|---|
| menu (`-M 400:heSerdeHezueue`) : STRATSED en A, hyperbas.rom en banque 6, Enregistrer, RESET | messages du menu ; « 32 Ko ROM », STRATSED V2.0c et HYPER BASIC V2.0b après le RESET (à froid) ; `TELESTRA.CFG` = `a=`, `bank6=` |
| configuration `standard`, `TELESTRA.CFG` du menu plus `bank5=teleass.rom` | TELEASS deux fois (banques 2 et 5) ; `SAVE "MENUOK"` réécrit dans le fichier de la clé |
| menu : imprimante coupée (`uuuuuue`), Enregistrer ; puis `LPRINT "IMPRIME"` (`-P`) | « Imprimante coupée », `impression=non` et `modem=oui` écrits ; rien d'imprimé ; avec `impression=oui`, « IMPRIME » imprimé |
| `telemon` : `.rom` de 1000 octets puis teleass.rom en banque 5, hyperbas.rom en banque 4, image du menu (`-O`) | « taille invalide » ; banque 5 prise (emplacement supplémentaire) ; « plus de place » ; PPM 960 x 544 |

## Cassette — `tests/test_tape.sh` (18 vérifications)

Configuration `atmos` (BASIC 1.1 en banque 7). `10 PRINT "CASSETTE OK"` et
`20 PRINT 6*7` tapés, RAM relevée (`-r`), cassette faite par
`tools/mktap.py` (51 octets) ; `CLOAD""` (`-K`) : « Loading .. ESSAI »
pendant la lecture, `LIST` et `RUN` (« CASSETTE OK », 42) ; sans cassette,
« Searching ». Par le menu depuis un Telestrat standard (`-U`, `-M`) : ROM
Atmos intégrée en banque 7, `essai.tap`, RESET, `CLOAD`, `RUN` ; bandeau
(jaune et bleu) sur la sortie DVI (`-D`) pendant la lecture. `CSAVE"ESSAI"`
(`-C`) : `ESSAI.TAP` écrit, rechargé par `CLOAD`, `RUN` (« CSAVE OK », 42) ;
bandeau « Écriture » (point rouge) pendant l'enregistrement. Essai manuel :
« L'Aigle d'Or » (Loriciels, `AIGLE.TAP`, deux parties) se charge en entier.
Cassette rapide (`-Z`) : « Ready » sous `CLOAD""` à la trame 240 (en lecture
normale, « Loading » y est encore affiché), `RUN` donne « CASSETTE OK » et
42. Moteur toujours en marche (`-Y`) : bandeau de lecture sans `CLOAD` (pas
de bandeau avec le relais). Menu : cassette rapide et moteur activés,
Enregistrer → `cassette_rapide=oui`, `cassette_moteur=toujours` ; menu
relu au démarrage suivant (`-O menu.txt`) : réglages affichés, cartouches
nommées. Essai manuel : « L'Aigle d'Or » en rapide arrive à « APPUYEZ SUR
UNE TOUCHE » (en lecture normale, encore « Loading » après 120 s).

## Instantanés — `tests/test_state.sh` (7 vérifications)

HYPER-BASIC (STRATSED) : `A=1234:B$="INSTANTANE"`, instantané (`-X`),
nouvelle session reprise (`-J`) : `PRINT A;B$` donne `1234INSTANTANE`.
Mode Atmos : programme tapé, instantané, reprise, `RUN` (« REPRIS », 42),
programme à l'écran. Menu (`-U`) : Instantanés → Enregistrer
(`ETAT0001.STA`) avec STRATORIC en banque 7 ; reprise par le menu depuis une
configuration sans STRATORIC : message, STRATORIC remise en banque 7, ligne
Instantanés avec le dernier fichier (`-O menu.txt`). Fichier abîmé refusé
(« pas un instantané »). Essai manuel : « L'Aigle d'Or » repris donne la
même image que la suite d'origine.

## Démarrage — `tests/test_profiles.sh` (10 vérifications)

`demarrage=choix` : page « Démarrer sur… » (`-O page.txt` : titre, profils,
plus d'ORIX) ; STRATORIC choisie → « STRATORIC V4.0 » ; Échap → Telestrat de
la clé. `demarrage=atmos` → ORIC EXTENDED BASIC V1.1 ; `demarrage=stratoric`
→ STRATORIC V4.0 ; `demarrage=orix` (profil retiré) → Telestrat de la clé.
Profils de la clé : proposés après les intégrés ; « Atmos et outil » choisi →
BASIC 1.1, `outil.rom` en banque 4 (texte du menu) ; profil au fichier
absent signalé ; `demarrage=Atmos et outil` sans page.

## Imprimantes — `tests/test_printer.sh` (12 vérifications)

`build/printer_render` rend un flux brut : FX-80 (texte, gras, condensé,
`ESC K`, saut de page) → deux PNG valides (`tests/png_info.py` : CRC, zlib,
taille, encre dans un rectangle), texte en haut de la page 1, graphiques à
la 4ᵉ ligne ; MCP-40 → SVG valide (XML), numéro suivant, traits, rouge,
texte. De bout en bout (STRATSED, HYPER-BASIC) : `LPRINT "BONJOUR TELESTRAT"`
avec `-G fx80` → une page, texte en haut à gauche et rien d'autre, octets
bruts dans `-P` ; `LPRINT CHR$(18)`, `"D100,0,100,50"`, `"A"` avec
`-G mcp40` → le trait dans le SVG. Menu (`-U`, `-G fx80`) : Entrée →
« Imprimante : Traceur MCP-40 », Enregistrer → `imprimante_type=mcp40`.

## Coût du menu sur le cœur 1 — `make charge-menu`

Voir docs/PERFORMANCE.md (non inclus dans `make test` : unicorn et capstone).

## Rejeu contre la référence — `tests/test_replay.sh` (2 traces)

Le banc de référence (`build/telestrat_headless_ref`, compilé avec
`-DTELESTRAT_REF`) enregistre, `tests/replay.c` rejoue sur le système optimisé
et exige 0 différence : octet placé sur le bus et ligne IRQ à chaque cycle,
échantillons audio, octets série et leur cycle, empreinte de l'image par trame.

| Trace | Contenu |
|---|---|
| A (20 M cycles, config. `oricutron`) | démarrage STRATSED, HYPER-BASIC, PING, ZAP, DIR |
| B (60 M cycles, config. `standard`) | serveur TELEMATIC : sonnerie, connexion, pages, ENVOI, raccrochage |

## STRATORIC — `tests/test_stratoric.sh` (5 vérifications)

Telestrat standard, répertoire-clé (`-U`) : `bank7=@stratoric` → « STRATORIC
V4.0 », `PRINT 6*7` ; par le menu (banque 7 → STRATORIC, RESET) ; cassette
(programme fait en mode Atmos, `CLOAD`, `RUN`) ; disquette SEDORIC « 3D Munch »
(si présente) : copyright Loriciels dans les lignes de texte du mode HIRES
(`$BF68`).

## Outils de la carte — `tests/test_carte.py` (10 vérifications)

Sans carte : `tools/carte.py` décode l'état de la clé depuis une fausse mémoire
(clé montée, retirée, adresse USB nulle, nom long de 47 caractères).

## Oracle Oricutron — `tools/oracle/oracle.sh` (manuel)

Construit une copie locale d'Oricutron (GPL, non distribuée) avec
`oricutron-dump.patch` : écran texte après N trames, sans fenêtre (SDL
« dummy »), banques en RAM à la demande. Exemple (v0.8.1) : STRATSED avec les
banques 1-5 en RAM, HYPER-BASIC et TELEMON → même arrêt qu'au banc.

```sh
tools/oracle/oracle.sh 1500 STRATSED.DSK "telebank6 = 'roms/hyperbas'" "telebank7 = 'roms/telmon24'" RAM=12345
```

## Charge du RP2040 — `make charge`

Voir docs/PERFORMANCE.md (non inclus dans `make test` : unicorn et capstone).

## Recette sur carte — `tools/carte.py` (manuelle, sonde SWD)

2026-09-29 (v0.16.0, clé + clavier + écran HDMI) : démarrage, 0,998 MHz, 0
ligne DVI en retard ; fichiers déposés (`deposer`) et relus (`relire`) ;
`TELESTRA.CFG` appliqué ; page de démarrage au clavier ; STRATSED depuis la
clé : menu, HYPER-BASIC, `DIR` (88 fichiers), `SAVE "CARTE"` (89 fichiers),
`LPRINT` → `IMPR0001.PNG` valide ; mode Atmos (BASIC 1.1), cassette rapide :
« L'Aigle d'Or » chargé ; instantané en plein jeu (`ETAT0001.STA`, 70 Ko,
relu : cartouche `@atmos`, registres) puis repris : le jeu revient et
continue. Traceur MCP-40 choisi par le menu (piloté par la sonde,
`carte.py menu`) : `LPRINT CHR$(18)`, carré en C1, texte en C3 →
`IMPR0005.SVG` relu, XML valide, couleurs et tailles attendues.


Validé le 2026-09-29 sur Olimex Neo6502 (disquette STRATSED en flash) :

| Vérification | Résultat |
|---|---|
| Démarrage : bannière, « 64 Ko RAM, 56 Ko ROM », état des banques `00 10 ef e7 10 10 ef 2f` | identique au banc PC |
| STRATSED V2.0c, HYPER BASIC V2.0b, TELEMATIC V2.0b, TELEASS V1.0a, menu | ✅ |
| `1` puis `PRINT 6*7` | « 42 » |
| `DIR` | 88 fichiers, 2237 secteurs libres (identique au banc) |
| Vitesse du 65C02 | 1,000 MHz (10 000 385 cycles en 10 s) |
| Cœur 0 | 45 % au repos, 62 % pendant DIR (76 % au pire) |
| Cœur 1 | 35 µs par ligne (max 41), 0 ligne DVI en retard |

### Clé retirée puis rebranchée (US-91, v0.16.6) — procédure

`carte.py cle` lit par la sonde l'état de la clé (`usb_scanned`, adresse USB),
les noms des lecteurs A à D, la cassette et le compteur de trames ;
`carte.py cle retiree|montee [s]` attend le geste et refuse un redémarrage
(compteur de trames revenu en arrière). Un opérateur retire et rebranche la clé.

| Étape | Attendu |
|---|---|
| 1. `TELESTRA.CFG` avec `a=STRATSED.DSK` et une `.tap` insérée par le menu ; `carte.py cle` | clé montée, A = STRATSED.DSK, cassette nommée |
| 2. `carte.py cle retiree`, puis retirer la clé | clé absente, A = « (image en flash) », B à D vides, cassette éjectée ; écran intact, pas de redémarrage |
| 3. `carte.py taper "DIR\n"` (disquette en flash) | liste de la disquette en flash |
| 4. `carte.py cle montee`, puis rebrancher la clé | clé montée, A = STRATSED.DSK remise, trames continues ; cartouches inchangées |
| 5. `carte.py taper "SAVE \"RETOUR\"\nDIR\n"`, `carte.py ecran` | RETOUR dans la liste : disquette de la clé réécrite après rebranchement |
| 6. menu (F1) | fichiers de la clé listés de nouveau |

Résultat : **non fait** (carte indisponible le 2026-09-30 au matin).

### Télématique sur carte (v0.4.2)

Ligne de recette par SWD à la place du modem (`tools/carte.py ligne ...`) :
toute la chaîne embarquée est exercée (ACIA, prise Minitel, sonnerie sur CB1,
TELEMON, TELEMATIC), sauf le modem physique.

| Étape | Résultat sur carte |
|---|---|
| `1`, `APLIC 4`, Accès disque, `N DEMO` + CTRL+L, ESC, `2` | « Attente de communication » |
| `ligne appel` | sonnerie reconnue (XRING), décroché, porteuse, 977 octets émis (975 du banc + SEP $53 vers la ligne) |
| page d'accueil | « serveur TELESTRAT … SERVEUR REALISE ENTIEREMENT AVEC UN TELESTRAT … » |
| `ligne envoyer '\E'` | « taper quelque chose avant ENVOI » |
| `ligne envoyer '1\E'` | page MENU (« PERMETTEZ-MOI DE ME PRESENTER … VOTRE CHOIX : ENVOI ») |
| charge pendant le service | 1,000 MHz, cœur 0 53 % (66 % au pire) |
| `ligne raccrocher` | SEP $59 $53 transmis ; retour à « Attente de communication » au délai d'inactivité de TELEMATIC (comme au banc) |
| second appel | servi à nouveau (977 octets) |

Méthode de diagnostic : journal des 1024 premiers accès en `$03xx` sur la carte
(`diag_io`) comparé à la trace du banc PC — c'est ce qui a localisé le défaut
de maintien de la donnée sur le bus.

## Firmware

`make uf2` doit compiler sans erreur (prise RS232 sur le modem USB, ou sur
l'UART0 de l'UEXT avec `rs232=uext` ; `-DTELESTRAT_RS232_UART=OFF` retire
l'UEXT) ; l'occupation RAM est relevée dans
docs/ARCHITECTURE.md. L'essai sur carte est manuel (non fait aux sprints 1 à 3).
