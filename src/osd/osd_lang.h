// osd_lang.h — langue du menu (F1) : français (défaut) ou anglais (US-198)
//
// Une langue pour tout le menu, choisie par « langue=fr|en » de TELESTRA.CFG ou par la touche L de la page
// principale, puis enregistrée avec la configuration. Les textes sont écrits par paire au point d'usage :
//   osd_puts(s, row, col, OSD_T("Disquettes", "Floppy disks"), attr, -1);
// Une seule variable par programme (fonction à variable statique : l'en-tête est inclus dans une seule unité de
// compilation par programme, firmware, banc PC ou test). Non traduits : la page Réseau et la liste des réseaux Wi-Fi
// (osd_netcfg.h du socle commun), le journal de la console (printf).
#pragma once
#include <stdint.h>
#include <string.h>

enum {
    OSD_LANG_FR = 0,
    OSD_LANG_EN = 1,
};

static inline uint8_t* _osd_lang_ptr(void) {
    static uint8_t lang = OSD_LANG_FR;
    return &lang;
}
#define osd_lang (*_osd_lang_ptr())

// Texte dans la langue du menu
#define OSD_T(fr, en) (osd_lang == OSD_LANG_EN ? (en) : (fr))

// Valeur de « langue= » : fr / francais / français, en / english / anglais ; dflt sinon
static inline int osd_config_lang(const char* v, int dflt) {
    if (!v) return dflt;
    if (!strcmp(v, "fr") || !strcmp(v, "francais") || !strcmp(v, "français")) return OSD_LANG_FR;
    if (!strcmp(v, "en") || !strcmp(v, "english") || !strcmp(v, "anglais")) return OSD_LANG_EN;
    return dflt;
}

static inline const char* osd_lang_key(int lang) { return lang == OSD_LANG_EN ? "en" : "fr"; }
