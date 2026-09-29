# Sonde SWD et carte Neo6502 — mode d'emploi partagé

Retour d'expérience de la session Telestrat (2026-09-29/30), envoyé aux autres
projets Neo6502. Tout ce qui suit a été vérifié sur la carte, sauf mention
contraire.

## Matériel

- Sonde : Raspberry Pi Debugprobe on Pico (CMSIS-DAP), `lsusb` : `2e8a:000c`,
  port série `/dev/ttyACM0` (pas relié à l'UART de la carte : rien n'y
  arrive).
- Branchée sur le connecteur SWD du Neo6502.
- **Une seule carte, une seule sonde pour tous les projets** : une session à
  la fois ; bmarty décide qui l'a. Annoncer quand on la prend et quand on la
  rend, et dans quel état on la laisse (quel firmware).

## Logiciel

OpenOCD de développement : `~/.local/openocd-dev/bin/openocd`
(0.12.0+dev-g21f88d7). D'après la session BBC, l'OpenOCD du système ne
connaît pas la flash Puya P25Q16 de la carte : l'init répond, la programmation
peut échouer.

Forme de toutes les commandes :

    ~/.local/openocd-dev/bin/openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c "adapter speed 2000" -c init -c "<commande>" ... -c shutdown

Adresses : symboles globaux de l'ELF (`arm-none-eabi-nm -S firmware.elf`),
variables `volatile`.

| Besoin | Commande OpenOCD |
|---|---|
| flasher | `program firmware.elf verify reset` (vitesse 5000) |
| redémarrer | `reset run` |
| lire | `mdw 0xADRESSE N` (mots de 32 bits, adresse alignée) |
| écrire un octet | `mwb 0xADRESSE 0xVV` |
| écrire un mot | `mww 0xADRESSE V` (adresse alignée sur 4) |
| charger un fichier en RAM | `load_image fichier.bin 0xADRESSE bin` |
| attendre une valeur (Tcl) | `while {[lindex [read_memory 0xADR 32 1] 0] != 0} { sleep 20 }` |
| plusieurs commandes | un script Tcl : `-c "source script.tcl"` (une seule session) |

## Précautions (toutes vécues)

1. **Vérifier l'ELF avant de flasher** : un « program » lancé alors que
   l'édition des liens avait échoué a effacé la carte (plus d'image). Refuser
   un ELF absent ou plus ancien que les sources.
2. **Ne jamais laisser un cœur arrêté** (`halt` sans `resume`) : l'affichage
   gèle. Pour lire le PC d'un cœur planté : `halt`, `reg pc`, `reg sp`,
   `mdw $sp N`, `resume`, puis `arm-none-eabi-addr2line -f -e firmware.elf ADR`.
3. **Un octet s'écrit par `mwb`**, pas par `mww` (adresse non alignée,
   risque d'écrire les octets voisins).
4. **« Failed to connect multidrop rp2040.dap0 »** : câble SWD ou alimentation
   qui a bougé (deux fois) ; bmarty rebranche.
5. **Reset après flash** : `program … verify reset` a toujours redémarré le
   firmware Telestrat (une dizaine de fois), sans `set USE_CORE 0`. La
   session Trinity a vu un HardFault après le reset avec `USE_CORE 0` ;
   hypothèse non vérifiée : n'attacher que le cœur 0 laisserait le cœur 1 sur
   l'ancien firmware.
6. Une seule session OpenOCD à la fois.

## Pièges du firmware vus sur la carte (pas sur le banc PC)

- **Tas trop petit au démarrage** : `dvi_init` (PicoDVI) alloue ses tampons
  TMDS par `malloc` (3 x 3 x 480 mots = 17 280 octets en 960 de large) ;
  `malloc` (newlib) arrondit par pages : il a fallu ~21,8 Ko de tas. Sans
  cela : « Out of memory », panic dans `dvi_init`. Remède : réserver le tas à
  l'édition des liens (`PICO_HEAP_SIZE`, par exemple `0x5800`) ; un manque de
  RAM fait alors échouer la compilation au lieu de planter la carte. La RAM
  « libre » calculée sans cela est trompeuse.
- **2 tampons TMDS au lieu de 3** : image défectueuse sur la carte (plus de
  marge pour les lignes en retard).
- **Clé USB** : avec le TinyUSB de 2023 (reload-emulator), le premier READ10
  d'une clé (512 octets, 8 paquets) n'aboutissait jamais, avec ou sans
  clavier. TinyUSB 0.21.0 (pilote hôte RP2040 refondu, hathach/tinyusb#3561)
  lit la clé, seule ou avec un clavier. Monter la clé (`f_mount`) depuis la
  boucle principale, pas dans le rappel d'INQUIRY (appelé dans `tuh_task`).
- **RAM regagnée** si besoin : FatFs avec `FF_FS_TINY=1` (512 octets de moins
  par fichier ouvert), noms longs courts (`FF_MAX_LFN`), un seul `FATFS` pour
  la clé, tampons CDC de 128 octets.
- L'écran HDMI de bmarty affiche le 960x544 à 372 MHz du Telestrat ; la
  session BBC l'a vu refuser d'autres modes (voir avec elle).

## Déposer des fichiers sur la clé sans la débrancher

Principe (Telestrat, `tools/carte.py deposer`, 1 Mo en 8 s) : le firmware
offre un tampon en RAM et quelques variables (commande, longueur, nom,
état) ; la sonde charge un morceau (`load_image`), écrit la longueur puis la
commande, attend que le firmware remette la commande à 0 ; le firmware
écrit le morceau par FatFs **depuis sa boucle principale**, émulation en
pause pendant le transfert (le tampon est libre), puis redessine l'écran.
Détails : `platforms/rp2040/src/telestrat.c`, `diag_upload_poll()`.

## Référence

`~/Neo6502TeleStrat/tools/carte.py` (lecture seule) : flasher, reset, écran,
clavier (file de touches), menu, mesures, dépôt et relecture de fichiers.
Sur le modèle de `~/Neo6502bbc/tools/carte/carte.py` (session BBC).
