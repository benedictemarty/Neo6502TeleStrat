#!/usr/bin/env python3
"""Faux modem pour tests/test_modem.sh : joue le PicoWiFiModemUSB du côté TCP.

Le banc (telestrat_headless -H listen:0) écoute ; ce programme s'y relie, comme
le CDC du modem à l'hôte. Il ne modélise QUE ce que src/devices/hayes_line.h
attend ; comportement du vrai modem non vérifié (formes des réponses, cadence
des RING, temps de garde de « +++ », « NO CARRIER » en ligne, tampon).

Ce qui est joué :
  - mode commande : toute ligne « AT… » terminée par CR reçoit « OK » (ATE0V1,
    ATS0=0, AT$SP=…, ATH) ; « ATD… » reçoit CONNECT ou l'échec choisi
    (--dial) ; « ATA » reçoit CONNECT ;
  - --ring : « RING » répété (toutes les 50 ms, temps réel) tant que ATA n'est
    pas reçu, une fois ATS0=0 reçu ;
  - mode en ligne : données transparentes, enregistrées dans SORTIE ; un bloc
    exactement égal à « +++ » rend le mode commande (« OK »), sans mesure des
    temps de garde (le banc n'est pas cadencé : seul l'ordre des octets compte) ;
  - en mode commande, --console répond « SALUT » à « HELLO » reçu (prise
    RS232 : le logiciel du Telestrat parle au modem sans Hayes).
  Le programme s'arrête quand le banc ferme la connexion (ou après DUREE s).

Usage : modem_peer.py PORT SORTIE DUREE [options]
  --dial RÉSULTAT   réponse à ATD : CONNECT (défaut), BUSY, « NO ANSWER »,
                    « NO DIALTONE », ERROR, NO CARRIER
  --ring            appel entrant
  --console         voir ci-dessus
  --on MARQUEUR=TEXTE   étape (répétable, dans l'ordre) : dès que MARQUEUR (mêmes échappements que TEXTE)
                    est reçu en ligne depuis l'étape précédente, TEXTE est envoyé
                    (MARQUEUR vide : tout de suite après CONNECT). Dans TEXTE :
                    \\E = ENVOI ($13 $41), \\F = Connexion/Fin ($13 $49),
                    \\xNN, \\r, \\n, \\\\ ; « @NOCARRIER » = le correspondant
                    raccroche (« \\r\\nNO CARRIER\\r\\n », retour en mode
                    commande) ; « @BIG:N » = N octets d'un coup (ASCII, motif
                    « 0123456789ABCDEF… » repéré par numéro d'ordre)
Fichiers : SORTIE (octets reçus en ligne), SORTIE.cmds (une ligne par commande
  ou événement : « ATE0V1 », « ATD… », « +++ », « ATH »…).
"""
import select
import socket
import sys
import time

KEYS = {"E": b"\x13\x41", "F": b"\x13\x49", "S": b"\x13\x46", "R": b"\x13\x42"}


def decode(text):
    out = b""
    i = 0
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text):
            n = text[i + 1]
            if n in KEYS:
                out += KEYS[n]
                i += 2
            elif n == "x":
                out += bytes([int(text[i + 2:i + 4], 16)])
                i += 4
            elif n == "r":
                out += b"\r"
                i += 2
            elif n == "n":
                out += b"\n"
                i += 2
            else:
                out += n.encode("latin-1")
                i += 2
        else:
            out += c.encode("latin-1")
            i += 1
    return out


def big(n):
    """N octets ASCII imprimables : lignes de 32 caractères numérotées (« 0001:… »)."""
    out = b""
    k = 0
    while len(out) < n:
        out += (b"%04X:" % k) + b"ABCDEFGHIJKLMNOPQRSTUVWXYZ" + b"."
        k += 1
    return out[:n]


def main():
    port, path, duree = int(sys.argv[1]), sys.argv[2], float(sys.argv[3])
    dial, ring, console, steps = "CONNECT", False, False, []
    a = sys.argv[4:]
    while a:
        o = a.pop(0)
        if o == "--dial":
            dial = a.pop(0)
        elif o == "--ring":
            ring = True
        elif o == "--console":
            console = True
        elif o == "--on":
            m, _, t = a.pop(0).partition("=")
            steps.append((decode(m), t))
        else:
            sys.exit("modem_peer : option inconnue " + o)
    for _ in range(300):
        try:
            s = socket.create_connection(("127.0.0.1", port))
            break
        except OSError:
            time.sleep(0.1)
    else:
        sys.exit("modem_peer : pas de banc sur le port %d" % port)
    s.setblocking(False)
    data = open(path, "wb", buffering=0)
    cmds = open(path + ".cmds", "w", buffering=1)

    def send(b):
        s.setblocking(True)
        try:
            s.sendall(b)
        finally:
            s.setblocking(False)

    def reply(r):
        send(b"\r\n" + r + b"\r\n")

    online, ready, answered = False, False, False
    line = b""
    since = b""  # reçu en ligne depuis la dernière étape
    pending = list(steps)
    last_ring = 0.0

    def connect():
        nonlocal online, since
        reply(b"CONNECT")
        online = True
        since = b""
        run_steps()

    def run_steps():
        nonlocal online, since
        while pending and online:
            m, t = pending[0]
            if m and m not in since:
                return
            pending.pop(0)
            since = since[since.find(m) + len(m):] if m else since
            if t == "@NOCARRIER":
                cmds.write("NO CARRIER (correspondant)\n")
                send(b"\r\nNO CARRIER\r\n")
                online = False
            elif t.startswith("@BIG:"):
                send(big(int(t[5:])))
            else:
                send(decode(t))

    fin = time.time() + duree
    while time.time() < fin:
        if ring and ready and not answered and time.time() - last_ring >= 0.05:
            send(b"RING\r\n")
            last_ring = time.time()
        r, _, _ = select.select([s], [], [], 0.02)
        if not r:
            continue
        try:
            b = s.recv(65536)
        except BlockingIOError:
            continue
        except ConnectionError:
            break
        if not b:
            break
        if online:
            if b == b"+++":
                cmds.write("+++\n")
                online = False
                reply(b"OK")
                continue
            data.write(b)
            since += b
            run_steps()
            continue
        for c in b:
            if c not in (13, 10):
                line += bytes([c])
                if console and line.endswith(b"HELLO"):
                    cmds.write("RS232 : HELLO\n")
                    send(b"SALUT\r\n")
                    line = b""
                continue
            if not line:
                continue
            txt = line.decode("latin-1")
            line = b""
            if txt.upper().startswith("ATD"):
                cmds.write(txt + "\n")
                if dial == "CONNECT":
                    connect()
                else:
                    reply(dial.encode())
            elif txt.upper() == "ATA":
                cmds.write(txt + "\n")
                answered = True
                connect()
            elif txt.upper().startswith("AT"):
                cmds.write(txt + "\n")
                if txt.upper() == "ATS0=0":
                    ready = True
                reply(b"OK")
            else:
                cmds.write("? " + txt + "\n")
    data.close()
    cmds.close()


if __name__ == "__main__":
    main()
