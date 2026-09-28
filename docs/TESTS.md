# Tests

`make` (ou `make test`) compile et lance tout. Aucune carte n'est nécessaire.

## Tests unitaires — `tests/test_telestrat.c` (89 vérifications)

Programme 6502 synthétique en banque 7, exécuté par le cœur W65C02S de reload :

- banque 7 au RESET et après exécution ;
- commutation par V2DRA depuis la RAM (`$0400`) : écriture/relecture en banque
  RAM, écriture ignorée en ROM, `$FF` en banque vide, retour en banque 7 ;
- DDRA partiel : les lignes en entrée gardent la banque précédente ;
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
  IRQ avec `$65` jusqu'à la lecture de la donnée, DTR inactif ;
- prise Minitel (fausse ligne) : sonnerie (fronts à 50 Hz pendant 1,5 s puis
  silence), XLIGNE décroche, pas de réponse avant la porteuse, `$13 $53`,
  données et séquences Videotex transmises, PRO2 filtrée, touches reçues,
  `$13 $54` au raccrochage, appel sortant, XDECON ;
- modem Hayes (faux modem) : initialisation (`AT$SP`), RING et sa fin, ATA,
  CONNECT (sans `\n` parasite), données, NO CARRIER en ligne, ATD, `+++`/ATH
  avec gardes, BUSY, pas d'appel sortant sans numéro ;
- banque vide instable (bus flottant).

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

## Télématique de bout en bout — `tests/test_telematic.sh` (6 vérifications)

Configuration standard (TELEMATIC en banque 3), `STRATSED.DSK`, arborescence
`DEMO` chargée, serveur lancé ; ligne `-L listen:PORT` (port libre) ; le
correspondant `tests/minitel_client.py` appelle, attend la page, envoie ENVOI,
raccroche. Vérifié : `ESC 9 o ESC 9 h` émis, `$13 $53` reçu, page d'accueil
(« SERVEUR REALISE ENTIEREMENT… »), réponse à ENVOI (« taper quelque chose
avant ENVOI »), `$13 $54` au raccrochage, retour à « Attente de communication ».

## Firmware

`make uf2` doit compiler sans erreur ; l'occupation RAM est relevée dans
docs/ARCHITECTURE.md. L'essai sur carte est manuel (non fait aux sprints 1 à 3).
