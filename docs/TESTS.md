# Tests

`make` (ou `make test`) compile et lance tout. Aucune carte n'est nécessaire.

## Tests unitaires — `tests/test_telestrat.c` (24 vérifications)

Programme 6502 synthétique en banque 7, exécuté par le cœur W65C02S de reload :

- banque 7 au RESET et après exécution ;
- commutation par V2DRA depuis la RAM (`$0400`) : écriture/relecture en banque
  RAM, écriture ignorée en ROM, `$FF` en banque vide, retour en banque 7 ;
- DDRA partiel : les lignes en entrée gardent la banque précédente ;
- FDC : `$0314`/INTRQ, INTENA, RESTORE/SEEK/STEP IN/STEP OUT, READ SECTOR sans
  disque (non prêt + secteur introuvable), adresses hors FDC ;
- ACIA : valeurs au RESET, RESET logiciel (parité gardée, IRQ interdites).

## Tests de démarrage — `tests/test_boot.sh` (21 vérifications)

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

## Firmware

`make uf2` doit compiler sans erreur ; l'occupation RAM est relevée dans
docs/ARCHITECTURE.md. L'essai sur carte est manuel (non fait au sprint 1).
