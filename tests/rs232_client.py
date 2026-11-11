#!/usr/bin/env python3
"""Client de la prise RS232 pour le test du mode « écoute » du banc (-S listen:0) :
deux appels successifs sur la même ligne, pour vérifier que le banc raccroche au
départ du premier correspondant et accepte le suivant (platforms/pc/line_tcp.h).

Pour chaque appel n (1 puis 2) : connexion au banc (nouvel essai pendant 10 s),
lecture jusqu'à « HELLO », envoi de « SALUTn », fermeture de la connexion. Écrit
dans SORTIE une ligne « appel n : OCTETS » par appel réussi.

Usage : rs232_client.py PORT SORTIE DURÉE
"""
import socket
import sys
import time


def appel(port, n, fin):
    while True:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=2)
            break
        except OSError:
            if time.time() > fin:
                sys.exit("rs232_client : appel %d : pas de ligne sur le port %d" % (n, port))
            time.sleep(0.1)
    s.settimeout(0.2)
    recu = b""
    while b"HELLO" not in recu:
        if time.time() > fin:
            s.close()
            sys.exit("rs232_client : appel %d : « HELLO » jamais reçu (%r)" % (n, recu))
        try:
            b = s.recv(256)
        except socket.timeout:
            continue
        if not b:
            break
        recu += b
    s.sendall(b"SALUT%d" % n)
    s.close()  # raccrochage : le banc doit libérer la ligne
    return recu


def main():
    port, path, duree = int(sys.argv[1]), sys.argv[2], float(sys.argv[3])
    fin = time.time() + duree
    with open(path, "w") as f:
        for n in (1, 2):
            recu = appel(port, n, fin)
            f.write("appel %d : %r\n" % (n, recu))
            f.flush()


if __name__ == "__main__":
    main()
