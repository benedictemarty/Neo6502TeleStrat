#!/usr/bin/env python3
"""Correspondant Minitel minimal pour les tests : se connecte en TCP à la ligne
du banc (telestrat_headless -L listen:PORT), attend une première page, envoie
éventuellement des touches, et enregistre tout ce qui est reçu.

Usage : minitel_client.py PORT SORTIE DUREE SILENCE [TOUCHES...]
  SILENCE : secondes sans données qui marquent la fin d'une page
  TOUCHES : texte à envoyer après la première page ; \\E = ENVOI ($13 $41),
            \\S = SOMMAIRE ($13 $46), \\R = RETOUR ($13 $42), \\F = touche
            CONNEXION/FIN ($13 $49), \\X = le client raccroche (ligne coupée)
"""
import socket
import sys
import time

KEYS = {"\\E": b"\x13\x41", "\\S": b"\x13\x46", "\\R": b"\x13\x42", "\\G": b"\x13\x44",
        "\\F": b"\x13\x49"}


def encode(text):
    out = b""
    i = 0
    while i < len(text):
        two = text[i:i + 2]
        if two in KEYS:
            out += KEYS[two]
            i += 2
        else:
            out += text[i].encode("ascii")
            i += 1
    return out


def main():
    port, path, duration, quiet = int(sys.argv[1]), sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
    keys = sys.argv[5:]
    for _ in range(300):
        try:
            s = socket.create_connection(("127.0.0.1", port))
            break
        except OSError:
            time.sleep(0.1)
    else:
        sys.exit("minitel_client : pas de ligne sur le port %d" % port)
    s.settimeout(0.2)
    data = b""
    marks = []  # position des envois dans le flux reçu
    start = time.time()
    last = start
    while time.time() - start < duration:
        try:
            b = s.recv(4096)
            if not b:
                break
            data += b
            last = time.time()
        except socket.timeout:
            pass
        # Page complète : `quiet` secondes de silence après des données
        if keys and data and time.time() - last > quiet:
            k = keys.pop(0)
            if k == "\\X":
                break
            marks.append(len(data))
            s.sendall(encode(k))
            last = time.time()
    s.close()
    with open(path, "wb") as f:
        f.write(data)
    print("minitel_client : %d octets reçus, envois après %s" % (len(data), marks))


if __name__ == "__main__":
    main()
