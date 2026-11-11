#!/usr/bin/env python3
"""Serveur Minitel minimal pour les tests : attend un appel TCP (le Telestrat en
émulation Minitel, banc -L connect:127.0.0.1:PORT), envoie une page Videotex,
puis à chaque ENVOI ($13 $41) répond « RECU: » suivi du texte tapé.
Enregistre tout ce qu'il reçoit ; SORTIE.pret contient le port d'écoute une fois ouverte
(PORT 0 : choisi par le système).

Usage : minitel_server.py PORT SORTIE DURÉE
"""
import os
import socket
import sys
import time

PAGE = (b"\x0c"                                   # effacement
        b"\x1f\x41\x41" b"SERVEUR DE TEST NEO6502TELESTRAT"  # rangée 1, colonne 1
        b"\x1f\x43\x41" b"TAPEZ UN TEXTE PUIS ENVOI")


def main():
    port, path, duree = int(sys.argv[1]), sys.argv[2], float(sys.argv[3])
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))  # port 0 : choisi par le système, annoncé dans SORTIE.pret
    srv.listen(1)
    # Annonce atomique du port effectif (le test le lit : aucun choix préalable de port)
    with open(path + ".pret.tmp", "w") as f:
        f.write("%d\n" % srv.getsockname()[1])
    os.replace(path + ".pret.tmp", path + ".pret")
    srv.settimeout(duree)
    recu = b""
    try:
        conn, _ = srv.accept()
    except socket.timeout:
        open(path, "wb").write(b"")
        sys.exit("minitel_server : aucun appel")
    conn.settimeout(0.2)
    sortie = open(path, "wb")  # écrit au fil de l'eau : le test peut arrêter le serveur
    conn.sendall(PAGE)
    texte = b""
    sep = False
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
        for c in b:  # octet par octet : le découpage TCP est quelconque
            if sep:
                sep = False
                if c == 0x41:  # ENVOI
                    conn.sendall(b"\x1f\x45\x41RECU: " + texte)
                    sys.stderr.write("minitel_server : réponse envoyée (%r)\n" % texte)
                    texte = b""
                continue  # autre touche de fonction ou état du modem : ignoré
            if c == 0x13:
                sep = True
            elif 0x20 <= c < 0x7F:
                texte += bytes([c])
    sortie.close()
    print("minitel_server : %d octets reçus" % len(recu))


if __name__ == "__main__":
    main()
