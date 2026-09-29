#!/usr/bin/env python3
"""Recette du Telestrat sur la carte Neo6502 par sonde SWD (Debugprobe CMSIS-DAP).

  carte.py flasher [ELF]       programme le firmware (défaut build/rp2040/telestrat.elf), puis reset
  carte.py taper "1\\nDIR\\n"   frappe au clavier du Telestrat (file de touches du firmware)
  carte.py ecran [image.png]   écran texte (28 x 40 en $BB80) ; image 240x224 si un fichier est donné
  carte.py mesure [secondes]   vitesse réelle du 65C02, µs par trame (cœur 0), µs par ligne (cœur 1)
  carte.py ligne appel         ligne de recette SWD à la place du modem ; un correspondant appelle
  carte.py ligne lire [n]      octets émis par le Telestrat vers le correspondant (les n derniers)
  carte.py ligne envoyer TEXTE touches du correspondant (\\E = ENVOI, \\S = SOMMAIRE, \\R = RETOUR)
  carte.py ligne raccrocher    le correspondant raccroche
  carte.py ligne etat          sonnerie, porteuse, octets émis

Sur le modèle de ~/Neo6502bbc/tools/carte/carte.py (session BBC). Pièges connus :
OpenOCD du système ne connaît pas la flash Puya P25Q16 de la carte (utiliser
~/.local/openocd-dev) ; ne jamais laisser un cœur arrêté (l'affichage gèle) ;
attendre ~25 s après un flash (montage de la clé USB).

Variables : NEO_ELF, NEO_OPENOCD.
"""
import os
import re
import subprocess
import sys
import time

ICI = os.path.dirname(os.path.abspath(__file__))
ELF = os.environ.get("NEO_ELF", os.path.join(ICI, "..", "build", "rp2040", "telestrat.elf"))
OPENOCD = os.environ.get("NEO_OPENOCD", os.path.expanduser("~/.local/openocd-dev/bin/openocd"))

# Palette du Telestrat (bit 0 rouge, 1 vert, 2 bleu)
PALETTE = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255),
           (255, 255, 255)]


def symboles(elf=ELF):
    sortie = subprocess.run(["arm-none-eabi-nm", "-S", elf], capture_output=True, text=True, check=True).stdout
    table = {}
    for ligne in sortie.splitlines():
        champs = ligne.split()
        if len(champs) >= 3:
            table[champs[-1]] = int(champs[0], 16)
    return table


def openocd(*commandes, timeout=180, vitesse=2000):
    args = [OPENOCD, "-f", "interface/cmsis-dap.cfg", "-f", "target/rp2040.cfg",
            "-c", f"adapter speed {vitesse}", "-c", "init"]
    for c in commandes:
        args += ["-c", c]
    args += ["-c", "shutdown"]
    r = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    journal = r.stdout + r.stderr
    if "Error" in journal:
        raise SystemExit("OpenOCD : " + "\n".join(l for l in journal.splitlines() if "Error" in l))
    return journal


def lire_mots(adresse, n):
    journal = openocd(f"mdw 0x{adresse:08x} {n}")
    mots = []
    for ligne in journal.splitlines():
        if re.match(r"^0x[0-9a-f]+:", ligne):
            mots += [int(m, 16) for m in ligne.split(":")[1].split()]
    return mots[:n]


def lire_octets(adresse, n):
    mots = lire_mots(adresse & ~3, (n + (adresse & 3) + 3) // 4)
    brut = b"".join(m.to_bytes(4, "little") for m in mots)
    return brut[adresse & 3:(adresse & 3) + n]


def disposition():
    s = symboles()
    ram, fb, ticks, bank = lire_mots(s["diag_layout"], 4)
    return s, s["state"] + ram, s["state"] + fb, s["state"] + ticks


def flasher(elf):
    openocd(f"program {elf} verify reset", timeout=300, vitesse=5000)
    print(f"flashé : {elf} (attendre ~25 s avant de mesurer)")


def taper(texte):
    s = symboles()
    codes = [0x0D if c == "\n" else ord(c) for c in texte.replace("\\n", "\n")]
    tete, queue = lire_mots(s["diag_keyq_head"], 2)
    if queue - tete + len(codes) > 256:
        raise SystemExit("file de touches pleine")
    cmds = [f"mwb 0x{s['diag_keyq'] + ((queue + i) & 255):08x} 0x{c:02x}" for i, c in enumerate(codes)]
    cmds.append(f"mww 0x{s['diag_keyq_tail']:08x} {queue + len(codes)}")
    openocd(*cmds)
    # Attente de la file (7 trames par touche)
    fin = time.time() + 5 + len(codes) * 0.2
    while time.time() < fin:
        tete, queue = lire_mots(s["diag_keyq_head"], 2)
        if tete == queue:
            return len(codes)
        time.sleep(1)
    raise SystemExit("la file de touches ne se vide pas (firmware arrêté ?)")


def ecran(png=None):
    s, ram, fb, _ = disposition()
    texte = lire_octets(ram + 0xBB80, 28 * 40)
    for y in range(28):
        ligne = "".join(chr(c & 0x7F) if 0x20 <= (c & 0x7F) < 0x7F else " " for c in texte[y * 40:(y + 1) * 40])
        print(ligne.rstrip())
    if png:
        img = lire_octets(fb, 120 * 224)
        pixels = []
        for octet in img:
            pixels += [PALETTE[octet >> 4 & 7], PALETTE[octet & 7]]
        entete = b"P6\n240 224\n255\n"
        brut = bytes(v for p in pixels for v in p)
        ppm = png if png.endswith(".ppm") else png + ".ppm"
        with open(ppm, "wb") as f:
            f.write(entete + brut)
        if not png.endswith(".ppm"):
            subprocess.run(["convert", ppm, "-scale", "300%", png], check=True)
            os.remove(ppm)
        print(png)


def mesure(secondes=10):
    s, _, _, ticks = disposition()
    # Compteurs remis à zéro au début de la fenêtre (le démarrage, avec l'USB, fausse le maximum)
    raz = [f"mww 0x{s[n]:08x} 0" for n in ("diag_frame_us_sum", "diag_frame_us_max", "diag_frame_n",
                                            "diag_line_us_sum", "diag_line_us_max", "diag_line_n")]
    journal = openocd(*raz, f"mdw 0x{ticks:08x} 1", f"mdw 0x{s['diag_frames']:08x} 1", f"sleep {secondes * 1000}",
                      f"mdw 0x{ticks:08x} 1", f"mdw 0x{s['diag_frames']:08x} 1")
    v = [int(l.split(":")[1], 16) for l in journal.splitlines() if re.match(r"^0x[0-9a-f]+:", l)]
    t0, f0, t1, f1 = v
    retard = lire_mots(s["diag_late"], 1)[0]
    # Lu variable par variable : l'ordre en mémoire dépend de l'éditeur de liens
    somme = lire_mots(s["diag_frame_us_sum"], 1)[0]
    maxi = lire_mots(s["diag_frame_us_max"], 1)[0]
    n = lire_mots(s["diag_frame_n"], 1)[0]
    lsum = lire_mots(s["diag_line_us_sum"], 1)[0]
    lmax = lire_mots(s["diag_line_us_max"], 1)[0]
    ln = lire_mots(s["diag_line_n"], 1)[0]
    mhz = (t1 - t0) / secondes / 1e6
    print(f"65C02 : {(t1 - t0) & 0xFFFFFFFF} cycles en {secondes} s = {mhz:.3f} MHz ; {f1 - f0} trames")
    print(f"cœur 0 : {somme / max(n, 1):.0f} µs de travail par trame de 19 968 µs en moyenne"
          f" ({100 * somme / max(n, 1) / 19968:.0f} %), {maxi} au pire")
    print(f"cœur 1 : {lsum / max(ln, 1):.1f} µs par ligne (max {lmax}) ; lignes DVI en retard : {retard}")


def lire_u8(s, nom):
    return lire_octets(s[nom], 1)[0]


def ligne(action, arg=None):
    s = symboles()
    if action == "appel":
        openocd(f"mwb 0x{s['diag_line_carrier']:08x} 0", f"mwb 0x{s['diag_line_on']:08x} 1",
                f"mwb 0x{s['diag_line_ring']:08x} 1")
        print("appel en cours (ligne SWD)")
    elif action == "raccrocher":
        openocd(f"mwb 0x{s['diag_line_carrier']:08x} 0", f"mwb 0x{s['diag_line_ring']:08x} 0")
        print("raccroché")
    elif action == "etat":
        n = lire_mots(s["diag_tx_n"], 1)[0]
        print(f"ligne SWD {lire_u8(s, 'diag_line_on')}, sonnerie {lire_u8(s, 'diag_line_ring')},"
              f" porteuse {lire_u8(s, 'diag_line_carrier')}, octets émis {n}")
    elif action == "lire":
        n = lire_mots(s["diag_tx_n"], 1)[0]
        k = min(n, int(arg) if arg else 4096, 4096)
        brut = lire_octets(s["diag_tx"], 4096)
        data = bytes(brut[(n - k + i) & 4095] for i in range(k))
        sys.stdout.write(bytes(c if 32 <= c < 127 else 32 for c in data).decode() + "\n")
        return data
    elif action == "envoyer":
        cles = {"\\E": b"\x13\x41", "\\S": b"\x13\x46", "\\R": b"\x13\x42"}
        data, i = b"", 0
        while i < len(arg):
            if arg[i:i + 2] in cles:
                data += cles[arg[i:i + 2]]
                i += 2
            else:
                data += arg[i].encode()
                i += 1
        head, tail = lire_mots(s["diag_rx_head"], 2)
        cmds = [f"mwb 0x{s['diag_rx'] + ((tail + j) & 255):08x} 0x{c:02x}" for j, c in enumerate(data)]
        cmds.append(f"mww 0x{s['diag_rx_tail']:08x} {tail + len(data)}")
        openocd(*cmds)
        print(f"{len(data)} octets envoyés")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "flasher":
        flasher(a[1] if len(a) > 1 else ELF)
    elif len(a) == 2 and a[0] == "taper":
        print(f"{taper(a[1])} touches")
    elif a and a[0] == "ecran":
        ecran(a[1] if len(a) > 1 else None)
    elif a and a[0] == "ligne" and len(a) >= 2:
        ligne(a[1], a[2] if len(a) > 2 else None)
    elif a and a[0] == "mesure":
        mesure(int(a[1]) if len(a) > 1 else 10)
    else:
        print(__doc__)
        sys.exit(2)
