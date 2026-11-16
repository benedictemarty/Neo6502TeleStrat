// net_status.h — état du Wi-Fi et du volume Réseau (TNFS) pour la ligne « Réseau » du menu (F1), US-196
//
// Fonction pure, sans dépendance au socle : la plate-forme lui passe les faits connus à chaque événement du montage
// (neo_net.c du socle) et le menu affiche deux signes suivis d'un détail :
//   « ● Wi-Fi ● TNFS srv:16384 »   Wi-Fi : le modem a une adresse IP (STATUS:2, 3 ou 4 de AT+CIPSTATUS) ;
//                                   TNFS : volume monté (connecté)
//   signe de chaque élément : NET_SIGN_OK point vert, NET_SIGN_KO croix rouge (non connecté / déconnecté),
//   NET_SIGN_BUSY « … » (essai en cours), NET_SIGN_UNKNOWN sans signe (rien de sûr : modem muet, retiré…)
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
    NET_SIGN_UNKNOWN = 0,
    NET_SIGN_OK = 1,
    NET_SIGN_KO = -1,
    NET_SIGN_BUSY = 2,
};

// Événements vus par la plate-forme (ceux de neo_net_event_t, recopiés pour rester testables sur PC)
enum {
    NET_EV_CONNECTING,     // tentative commencée
    NET_EV_PORT_ENABLED,   // port TNFS du modem activé : il redémarre, nouvelle tentative ensuite
    NET_EV_MOUNTED,        // volume monté
    NET_EV_FAILED,         // tentative échouée
    NET_EV_REMOVED,        // modem retiré
};

typedef struct {
    int8_t wifi;      // NET_SIGN_*
    int8_t tnfs;      // NET_SIGN_*
    char detail[48];  // vide : aucun essai depuis le démarrage ou les derniers réglages (le menu montre l'aide)
} net_status_t;

static inline void net_status_clear(net_status_t* s) {
    s->wifi = s->tnfs = NET_SIGN_UNKNOWN;
    s->detail[0] = 0;
}

// esp_status : dernier STATUS:n de AT+CIPSTATUS (-1 : aucun pendant cette tentative ; la plate-forme le remet à -1
// au début de chaque tentative) ; relay : port TNFS du modem utilisé ; usb_val : réponse à AT$TNFSUSB? (-1 :
// commande inconnue du modem)
static inline void net_status_update(net_status_t* s, int ev, bool server_invalid, int esp_status, bool relay,
                                     int usb_val, const char* host, unsigned port) {
    const bool ip = esp_status >= 2 && esp_status <= 4;
    char* d = s->detail;
    const size_t n = sizeof(s->detail);
    if (!host) host = "?";
    switch (ev) {
        case NET_EV_CONNECTING:
            s->wifi = s->tnfs = NET_SIGN_BUSY;
            snprintf(d, n, "connexion en cours");
            break;
        case NET_EV_PORT_ENABLED:
            s->tnfs = NET_SIGN_BUSY;
            snprintf(d, n, "port TNFS activé, redémarrage");
            break;
        case NET_EV_MOUNTED:
            s->wifi = s->tnfs = NET_SIGN_OK;
            snprintf(d, n, "%s:%u", host, port);
            break;
        case NET_EV_REMOVED:
            s->wifi = NET_SIGN_UNKNOWN;
            s->tnfs = NET_SIGN_KO;
            snprintf(d, n, "modem retiré");
            break;
        case NET_EV_FAILED:
            s->tnfs = NET_SIGN_KO;
            if (server_invalid) {
                s->wifi = NET_SIGN_UNKNOWN;
                snprintf(d, n, "serveur invalide (reseau=)");
            } else if (esp_status < 0) {
                s->wifi = NET_SIGN_UNKNOWN;
                snprintf(d, n, "modem sans réponse");
            } else if (!ip) {
                s->wifi = NET_SIGN_KO;
                snprintf(d, n, "Wi-Fi absent (STATUS:%d)", esp_status);
            } else {
                s->wifi = NET_SIGN_OK;
                if (!relay) snprintf(d, n, usb_val < 0 ? "modem sans port TNFS" : "port TNFS du modem absent");
                else snprintf(d, n, "%s:%u sans réponse", host, port);
            }
            break;
        default:
            break;
    }
}
