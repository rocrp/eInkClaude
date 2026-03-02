#include "credentials.h"
#include <Preferences.h>
#include <cstring>

static const char *NVS_NS = "eInkClaude";

bool creds_exist() {
    Preferences prefs;
    prefs.begin(NVS_NS, true);
    bool has = prefs.isKey("ssid") && prefs.isKey("token");
    prefs.end();
    return has;
}

bool creds_load(char *ssid, char *password, char *token) {
    Preferences prefs;
    prefs.begin(NVS_NS, true);

    if (!prefs.isKey("ssid") || !prefs.isKey("token")) {
        prefs.end();
        return false;
    }

    String s = prefs.getString("ssid", "");
    String p = prefs.getString("pass", "");
    String t = prefs.getString("token", "");
    prefs.end();

    if (s.length() == 0 || t.length() == 0) return false;

    strncpy(ssid, s.c_str(), 64);
    ssid[63] = '\0';
    strncpy(password, p.c_str(), 64);
    password[63] = '\0';
    strncpy(token, t.c_str(), 256);
    token[255] = '\0';
    return true;
}

void creds_save(const char *ssid, const char *password, const char *token) {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.putString("token", token);
    prefs.end();
}

void creds_clear() {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.clear();
    prefs.end();
}

bool creds_load_refresh(char *refresh_token, unsigned long *expires_at) {
    Preferences prefs;
    prefs.begin(NVS_NS, true);

    if (!prefs.isKey("rtoken")) {
        prefs.end();
        return false;
    }

    String rt = prefs.getString("rtoken", "");
    *expires_at = prefs.getULong("exp_at", 0);
    prefs.end();

    if (rt.length() == 0) return false;

    strncpy(refresh_token, rt.c_str(), 256);
    refresh_token[255] = '\0';
    return true;
}

void creds_save_refresh(const char *refresh_token, unsigned long expires_at) {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putString("rtoken", refresh_token);
    prefs.putULong("exp_at", expires_at);
    prefs.end();
}

void creds_save_token(const char *token, const char *refresh_token, unsigned long expires_at) {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putString("token", token);
    prefs.putString("rtoken", refresh_token);
    prefs.putULong("exp_at", expires_at);
    prefs.end();
}
