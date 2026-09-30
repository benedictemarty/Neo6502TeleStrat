"""Tests sans carte de tools/carte.py : décodage de l'état de la clé (US-91)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import carte  # noqa: E402

ok = 0


def verifie(cond, message):
    global ok
    if not cond:
        raise SystemExit("ÉCHEC : " + message)
    ok += 1


# Fausse mémoire du RP2040 : symboles à des adresses arbitraires
S = {"usb_scanned": 0x100, "msc_addr": 0x101, "msc_inquiry_complete": 0x102, "usb_first_mount": 0x103,
     "printer_open": 0x104, "drive_name": 0x200, "tape_name": 0x400, "diag_frames": 0x500}
carte.TAILLES.update({"drive_name": 4 * 48, "tape_name": 48})
mem = bytearray(0x600)


def lire(adresse, n):
    return bytes(mem[adresse:adresse + n])


def ecrire_nom(adresse, nom):
    mem[adresse:adresse + 48] = nom.encode().ljust(48, b"\0")


def machine(montee, lecteurs, cassette, trames):
    for n, v in (("usb_scanned", montee), ("msc_addr", 1 if montee else 0), ("msc_inquiry_complete", montee),
                 ("usb_first_mount", 0), ("printer_open", 0)):
        mem[S[n]] = int(v)
    for d, nom in enumerate(lecteurs):
        ecrire_nom(S["drive_name"] + 48 * d, nom)
    ecrire_nom(S["tape_name"], cassette)
    mem[S["diag_frames"]:S["diag_frames"] + 4] = trames.to_bytes(4, "little")


# Clé montée : STRATSED en A, JEUX.DSK en B, cassette insérée
machine(True, ["STRATSED.DSK", "JEUX.DSK", "", ""], "AIGLE.TAP", 70000)
e = carte.etat_cle(S, lire)
verifie(e["montee"], "clé montée")
verifie(e["lecteurs"] == ["STRATSED.DSK", "JEUX.DSK", "", ""], "lecteurs de la clé")
verifie(e["cassette"] == "AIGLE.TAP", "cassette")
verifie(e["trames"] == 70000, "trames")
texte = carte.decrire_cle(e)
verifie("lecteur A : STRATSED.DSK" in texte and "lecteur C : (vide)" in texte, "description des lecteurs")

# Clé retirée : image en flash dans A, reste vide, cassette éjectée
machine(False, ["(image en flash)", "", "", ""], "", 71000)
e = carte.etat_cle(S, lire)
verifie(not e["montee"], "clé retirée")
verifie(e["lecteurs"][0] == "(image en flash)" and e["lecteurs"][1] == "", "lecteurs après retrait")
verifie("clé : absente" in carte.decrire_cle(e) and "cassette : (aucune)" in carte.decrire_cle(e),
        "description après retrait")

# usb_scanned sans adresse USB (clé partie, pas encore vue par la boucle) : absente
machine(True, ["STRATSED.DSK", "", "", ""], "", 71001)
mem[S["msc_addr"]] = 0
verifie(not carte.etat_cle(S, lire)["montee"], "adresse USB nulle : absente")

# Nom de la longueur maximale (47 caractères), sans zéro de fin perdu
long_nom = "N" * 43 + ".DSK"
machine(True, [long_nom, "", "", ""], "", 1)
verifie(carte.etat_cle(S, lire)["lecteurs"][0] == long_nom, "nom long")

print(f"test_carte : {ok} vérifications, toutes passées")
