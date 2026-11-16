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

#include "osd/osd_lang.h"   // détail dans la langue du menu (OSD_T)

enum {
    NET_SIGN_UNKNOWN = 0,
    NET_SIGN_OK = 1,
    NET_SIGN_KO = -1,
    NET_SIGN_BUSY = 2,
};

// Événements vus par la plate-forme (ceux de neo_net_event_t, recopiés pour rester testables sur PC)
enum {
    NET_EV_CONNECTING = 1, // tentative commencée (0 : aucun événement)
    NET_EV_PORT_ENABLED,   // port TNFS du modem activé : il redémarre, nouvelle tentative ensuite
    NET_EV_MOUNTED,        // volume monté
    NET_EV_FAILED,         // tentative échouée
    NET_EV_REMOVED,        // modem retiré
};

typedef struct {
    int8_t wifi;      // NET_SIGN_*
    int8_t tnfs;      // NET_SIGN_*
    char detail[48];  // vide : aucun essai depuis le démarrage ou les derniers réglages (le menu montre l'aide)
    // Faits du dernier événement, gardés pour réécrire le détail dans une autre langue (net_status_render)
    int8_t ev;        // NET_EV_* ; 0 : aucun (état à zéro : rien à afficher)
    bool server_invalid, relay;
    int esp_status, usb_val;
    char host[48];
    unsigned port;
} net_status_t;

static inline void net_status_clear(net_status_t* s) {
    s->wifi = s->tnfs = NET_SIGN_UNKNOWN;
    s->detail[0] = 0;
    s->ev = 0;
}

// Signes et détail recalculés à partir des faits gardés, dans la langue du menu (osd_lang.h)
static inline void net_status_render(net_status_t* s) {
    const bool ip = s->esp_status >= 2 && s->esp_status <= 4;
    char* d = s->detail;
    const size_t n = sizeof(s->detail);
    const char* host = s->host;
    const unsigned port = s->port;
    switch (s->ev) {
        case NET_EV_CONNECTING:
            s->wifi = s->tnfs = NET_SIGN_BUSY;
            snprintf(d, n, "%s", OSD_T("connexion en cours", "connecting"));
            break;
        case NET_EV_PORT_ENABLED:
            s->tnfs = NET_SIGN_BUSY;
            snprintf(d, n, "%s", OSD_T("port TNFS activé, redémarrage", "TNFS port enabled, restarting"));
            break;
        case NET_EV_MOUNTED:
            s->wifi = s->tnfs = NET_SIGN_OK;
            snprintf(d, n, "%.41s:%u", host, (unsigned)(uint16_t)port);   // 41 + 1 + 5 : tient dans detail
            break;
        case NET_EV_REMOVED:
            s->wifi = NET_SIGN_UNKNOWN;
            s->tnfs = NET_SIGN_KO;
            snprintf(d, n, "%s", OSD_T("modem retiré", "modem removed"));
            break;
        case NET_EV_FAILED:
            s->tnfs = NET_SIGN_KO;
            if (s->server_invalid) {
                s->wifi = NET_SIGN_UNKNOWN;
                snprintf(d, n, "%s", OSD_T("serveur invalide (reseau=)", "invalid server (reseau=)"));
            } else if (s->esp_status < 0) {
                s->wifi = NET_SIGN_UNKNOWN;
                snprintf(d, n, "%s", OSD_T("modem sans réponse", "modem not responding"));
            } else if (!ip) {
                s->wifi = NET_SIGN_KO;
                snprintf(d, n, OSD_T("Wi-Fi absent (STATUS:%d)", "no Wi-Fi (STATUS:%d)"), s->esp_status);
            } else {
                s->wifi = NET_SIGN_OK;
                if (!s->relay)
                    snprintf(d, n, "%s", s->usb_val < 0 ? OSD_T("modem sans port TNFS", "modem without TNFS port")
                                                        : OSD_T("port TNFS du modem absent", "TNFS port of the modem missing"));
                else snprintf(d, n, OSD_T("%.27s:%u sans réponse", "%.27s:%u no answer"), host, (unsigned)(uint16_t)port);
            }
            break;
        default:
            break;
    }
}

// esp_status : dernier STATUS:n de AT+CIPSTATUS (-1 : aucun pendant cette tentative ; la plate-forme le remet à -1
// au début de chaque tentative) ; relay : port TNFS du modem utilisé ; usb_val : réponse à AT$TNFSUSB? (-1 :
// commande inconnue du modem)
static inline void net_status_update(net_status_t* s, int ev, bool server_invalid, int esp_status, bool relay,
                                     int usb_val, const char* host, unsigned port) {
    s->ev = (int8_t)ev;
    s->server_invalid = server_invalid;
    s->esp_status = esp_status;
    s->relay = relay;
    s->usb_val = usb_val;
    snprintf(s->host, sizeof(s->host), "%s", host ? host : "?");
    s->port = port;
    net_status_render(s);
}
