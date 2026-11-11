#!/usr/bin/env python3
"""Correspondant de la prise RS232 pour les tests (liaison directe, banc -S
connect:127.0.0.1:PORT) : enregistre tout ce qu'il reçoit ; à la réception de
« ! » envoie le fichier FICHIER (s'il est donné) ; à la réception de « HELLO »
répond « SALUT ». SORTIE.pret contient le port d'écoute une fois ouverte (PORT 0 :
choisi par le système).

Usage : rs232_peer.py PORT SORTIE DURÉE [FICHIER]
"""
import os
import socket
import sys
import time


def main():
    port, path, duree = int(sys.argv[1]), sys.argv[2], float(sys.argv[3])
    envoi = open(sys.argv[4], "rb").read() if len(sys.argv) > 4 else b""
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))  # port 0 : choisi par le système, annoncé dans SORTIE.pret
    srv.listen(1)
    # Annonce atomique du port effectif (le test le lit : aucun choix préalable de port)
    with open(path + ".pret.tmp", "w") as f:
        f.write("%d\n" % srv.getsockname()[1])
    os.replace(path + ".pret.tmp", path + ".pret")
    srv.settimeout(duree)
    sortie = open(path, "wb")  # écrit au fil de l'eau : le test peut arrêter le correspondant
    try:
        conn, _ = srv.accept()
    except socket.timeout:
        sys.exit("rs232_peer : aucune connexion")
    conn.settimeout(0.2)
    recu = b""
    fin = time.time() + duree
    while time.time() < fin:
        try:
            b = conn.recv(256)
            if not b:
                break
        except socket.timeout:
            continue
        recu += b
        sortie.write(b)
        sortie.flush()
        if envoi and b"!" in b:
            conn.sendall(envoi)
            envoi = b""
        if b"HELLO" in recu:
            conn.sendall(b"SALUT")
            recu = recu.replace(b"HELLO", b"")
    sortie.close()


if __name__ == "__main__":
    main()
