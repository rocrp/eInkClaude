#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "epd_driver.h"
#include "dashboard.h"
#include "credentials.h"
#include "touch_ui.h"
#include "secrets.h"

uint8_t *framebuffer = NULL;

// Loaded credentials
static char wifi_ssid[64];
static char wifi_pass[64];
static char oauth_token[256];
static char refresh_token[256];
static unsigned long token_expires_at = 0; // epoch seconds

ClaudeStats currentStats = {};
unsigned long lastFetch = 0;
static const unsigned long FETCH_INTERVAL = 300000; // 5 minutes

static const int SETUP_BUTTON_PIN = 21;
static const char *OAUTH_CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";

// GlobalSign Root CA — trust anchor for api.anthropic.com
// Chain: api.anthropic.com → Google Trust Services WE1 → GTS Root R4 → GlobalSign Root CA
// Valid until 2028-01-28. Verified with: openssl s_client -connect api.anthropic.com:443
static const char *ROOT_CA = R"(
-----BEGIN CERTIFICATE-----
MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkG
A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv
b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAw
MDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i
YWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYDVQQDExJHbG9iYWxT
aWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDaDuaZ
jc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavp
xy0Sy6scTHAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp
1Wrjsok6Vjk4bwY8iGlbKk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdG
snUOhugZitVtbNV4FpWi6cgKOOvyJBNPc1STE4U6G7weNLWLBYy5d4ux2x8gkasJ
U26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrXgzT/LCrBbBlDSgeF59N8
9iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E
BTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0B
AQUFAAOCAQEAIlPZpboj8K8bBkjM5JEAHFNEgGFiIxYR3JTjmFPX3hhaPL+4LgnU
YhDP/31jQVZ/PVfRDe5KxPVCr2rUvDpoMhSTIWfUeyLdpJJnxLBMDn8MQ7sjL/fH
U+eHfTDQViR4bfFP1J4SrKq0iz3JClGELf5bYJZfWPSgnPY/LEkam/bS80jsUJKm
Rm9ISrBDcbgI+wds5EmJbOilAyFy2/C6MVBoOQWjBfCPk24p3XEdrIxRFSKj/LdN
Yy/Je/VnhHQ3o0CPxPiDH/R0mYnsFM1LFIQyhN+g5gzNSf0FE0T7IbmasMv0K6q4
BCBfP+6gtNb7I6F7LkNK7yR90dRx3CJY9Q==
-----END CERTIFICATE-----
)";

// ISRG Root X1 — trust anchor for console.anthropic.com (Let's Encrypt)
static const char *ISRG_ROOT_X1 = R"(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)";

bool connectWiFi() {
    Serial.printf("Connecting to WiFi: %s\n", wifi_ssid);
    WiFi.begin(wifi_ssid, wifi_pass);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    Serial.println("\nWiFi connection failed!");
    return false;
}

void syncNTP() {
    Serial.println("Syncing time via NTP...");
    configTime(3600, 3600, "pool.ntp.org", "time.nist.gov"); // CET (UTC+1), DST +1h

    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (getLocalTime(&timeinfo)) {
        Serial.printf("\nTime synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.println("\nNTP sync failed!");
    }
}

// Time tracking — uses NTP-synced system time

// Simple ISO8601 parser: "2025-12-01T18:30:00Z" -> epoch seconds
static unsigned long parseISO8601(const char *iso) {
    int yr, mo, dy, hr, mn, sc;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc) != 6) return 0;

    // Days from year 1970 to yr
    unsigned long days = 0;
    for (int y = 1970; y < yr; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }
    // Days from months
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = (yr % 4 == 0 && (yr % 100 != 0 || yr % 400 == 0));
    for (int m = 1; m < mo; m++) {
        days += mdays[m - 1];
        if (m == 2 && leap) days++;
    }
    days += dy - 1;

    return days * 86400UL + hr * 3600UL + mn * 60UL + sc;
}

// Get current epoch from NTP-synced system clock
static unsigned long currentEpoch() {
    time_t now;
    time(&now);
    return (unsigned long)now;
}

// Format seconds into "Xh Ym" or "Xd Yh" string
static void formatCountdown(unsigned long secs, char *buf, size_t len) {
    if (secs == 0) {
        snprintf(buf, len, "now");
        return;
    }
    unsigned long days = secs / 86400;
    unsigned long hours = (secs % 86400) / 3600;
    unsigned long mins = (secs % 3600) / 60;

    if (days > 0) {
        snprintf(buf, len, "%lud %luh", days, hours);
    } else if (hours > 0) {
        snprintf(buf, len, "%luh %lum", hours, mins);
    } else {
        snprintf(buf, len, "%lum", mins);
    }
}

// Refresh OAuth token using refresh_token
bool refreshOAuthToken() {
    if (refresh_token[0] == '\0') {
        Serial.println("No refresh token available");
        return false;
    }

    Serial.println("Refreshing OAuth token...");

    WiFiClientSecure client;
    client.setCACert(ISRG_ROOT_X1);

    HTTPClient http;
    if (!http.begin(client, "https://console.anthropic.com/v1/oauth/token")) {
        Serial.println("HTTP begin failed for token refresh");
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    JsonDocument reqDoc;
    reqDoc["grant_type"] = "refresh_token";
    reqDoc["refresh_token"] = refresh_token;
    reqDoc["client_id"] = OAUTH_CLIENT_ID;

    String body;
    serializeJson(reqDoc, body);

    int httpCode = http.POST(body);
    Serial.printf("Token refresh response: %d\n", httpCode);

    if (httpCode != 200) {
        if (httpCode > 0) {
            Serial.println(http.getString());
        }
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument respDoc;
    DeserializationError err = deserializeJson(respDoc, payload);
    if (err) {
        Serial.printf("Token refresh JSON parse error: %s\n", err.c_str());
        return false;
    }

    const char *newToken = respDoc["access_token"] | (const char *)NULL;
    const char *newRefresh = respDoc["refresh_token"] | (const char *)NULL;
    int expiresIn = respDoc["expires_in"] | 0;

    if (!newToken || !newRefresh) {
        Serial.println("Token refresh: missing fields in response");
        return false;
    }

    // Update in-memory tokens
    strncpy(oauth_token, newToken, sizeof(oauth_token) - 1);
    oauth_token[sizeof(oauth_token) - 1] = '\0';
    strncpy(refresh_token, newRefresh, sizeof(refresh_token) - 1);
    refresh_token[sizeof(refresh_token) - 1] = '\0';
    token_expires_at = currentEpoch() + expiresIn;

    // Persist to NVS
    creds_save_token(oauth_token, refresh_token, token_expires_at);

    Serial.printf("Token refreshed, expires in %ds\n", expiresIn);
    return true;
}

// Ensure we have a valid (non-expired) access token
bool ensureValidToken() {
    unsigned long now = currentEpoch();
    // Refresh if token expires within 5 minutes
    if (token_expires_at > 0 && now + 300 < token_expires_at) {
        return true; // Still valid
    }
    Serial.println("Token expired or expiring soon, refreshing...");
    return refreshOAuthToken();
}

bool fetchUsage() {
    WiFiClientSecure client;
    client.setCACert(ROOT_CA);

    HTTPClient http;
    if (!http.begin(client, "https://api.anthropic.com/api/oauth/usage")) {
        Serial.println("HTTP begin failed");
        return false;
    }

    char authHeader[300];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", oauth_token);
    http.addHeader("Authorization", authHeader);
    http.addHeader("anthropic-beta", "oauth-2025-04-20");

    int httpCode = http.GET();
    Serial.printf("HTTP response code: %d\n", httpCode);
    if (httpCode != 200) {
        if (httpCode > 0) {
            Serial.println(http.getString());
        }
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.println("API response received:");
    Serial.println(payload);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return false;
    }

    // Actual API response format:
    // {"five_hour":{"utilization":14.0,"resets_at":"..."},
    //  "seven_day":{"utilization":17.0,"resets_at":"..."},
    //  "seven_day_opus":null,
    //  "seven_day_sonnet":{"utilization":0.0,"resets_at":"..."},
    //  "extra_usage":{...}}
    JsonObject obj = doc.as<JsonObject>();
    unsigned long now = currentEpoch();

    // 5-hour utilization
    if (obj["five_hour"].is<JsonObject>()) {
        JsonObject fh = obj["five_hour"];
        currentStats.fiveHourPct = fh["utilization"].as<float>();
        const char *resetAt = fh["resets_at"] | (const char *)NULL;
        if (resetAt && now > 0) {
            unsigned long resetEpoch = parseISO8601(resetAt);
            unsigned long diff = (resetEpoch > now) ? (resetEpoch - now) : 0;
            formatCountdown(diff, currentStats.fiveHourReset, sizeof(currentStats.fiveHourReset));
        } else {
            strlcpy(currentStats.fiveHourReset, "", sizeof(currentStats.fiveHourReset));
        }
    } else {
        currentStats.fiveHourPct = -1;
        strlcpy(currentStats.fiveHourReset, "", sizeof(currentStats.fiveHourReset));
    }

    // 7-day utilization
    if (obj["seven_day"].is<JsonObject>()) {
        JsonObject sd = obj["seven_day"];
        currentStats.sevenDayPct = sd["utilization"].as<float>();
        const char *resetAt = sd["resets_at"] | (const char *)NULL;
        if (resetAt && now > 0) {
            unsigned long resetEpoch = parseISO8601(resetAt);
            unsigned long diff = (resetEpoch > now) ? (resetEpoch - now) : 0;
            formatCountdown(diff, currentStats.sevenDayReset, sizeof(currentStats.sevenDayReset));
        } else {
            strlcpy(currentStats.sevenDayReset, "", sizeof(currentStats.sevenDayReset));
        }
    } else {
        currentStats.sevenDayPct = -1;
        strlcpy(currentStats.sevenDayReset, "", sizeof(currentStats.sevenDayReset));
    }

    // Opus (7-day per-model)
    if (obj["seven_day_opus"].is<JsonObject>()) {
        currentStats.opusPct = obj["seven_day_opus"]["utilization"].as<float>();
    } else {
        currentStats.opusPct = -1;
    }

    // Sonnet (7-day per-model)
    if (obj["seven_day_sonnet"].is<JsonObject>()) {
        currentStats.sonnetPct = obj["seven_day_sonnet"]["utilization"].as<float>();
    } else {
        currentStats.sonnetPct = -1;
    }

    strlcpy(currentStats.plan, "Max", sizeof(currentStats.plan));

    currentStats.valid = true;
    return true;
}

// Run the touchscreen setup wizard (WiFi select → password → token → save)
void runSetupWizard() {
    Serial.println("Entering setup wizard...");

    // Screen 1: WiFi selector
    run_wifi_selector(wifi_ssid);
    Serial.printf("Selected SSID: %s\n", wifi_ssid);

    // Screen 2: WiFi password
    run_keyboard_input("Enter WiFi Password", wifi_pass, sizeof(wifi_pass), true);
    Serial.println("WiFi password entered");

    // Screen 3: OAuth token via serial monitor
    Serial.println("=== Paste your Claude OAuth token in Serial Monitor and press Enter ===");
    dashboard_draw_waiting("Paste OAuth token in\nSerial Monitor...");

    // Read token from serial
    oauth_token[0] = '\0';
    int pos = 0;
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (pos > 0) break;  // Got something, done
                continue;            // Skip leading newlines
            }
            if (pos < (int)sizeof(oauth_token) - 1) {
                oauth_token[pos++] = c;
                oauth_token[pos] = '\0';
            }
        }
        delay(10);
    }
    Serial.printf("Token received (%d chars)\n", pos);

    // Save to NVS
    creds_save(wifi_ssid, wifi_pass, oauth_token);
    Serial.println("Credentials saved to NVS");
}

void setup() {
    Serial.begin(115200);
    Serial.println("eInkClaude v2.0");

    // Check setup button early
    pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
    delay(100);
    bool force_setup = (digitalRead(SETUP_BUTTON_PIN) == LOW);
    if (force_setup) {
        Serial.println("Setup button held — clearing saved credentials");
    }

    epd_init();

    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("framebuffer alloc failed!");
        while (1);
    }

    dashboard_init();

    // Init touch
    touch_init();

    // Force setup if button held
    if (force_setup) {
        creds_clear();
    }

    // Try loading saved credentials
    bool have_creds = creds_load(wifi_ssid, wifi_pass, oauth_token);

    if (!have_creds) {
        // Check for hardcoded defaults in secrets.h
        if (strlen(DEFAULT_SSID) > 0 && strlen(DEFAULT_TOKEN) > 0) {
            Serial.println("Using hardcoded defaults from secrets.h");
            strncpy(wifi_ssid, DEFAULT_SSID, sizeof(wifi_ssid) - 1);
            strncpy(wifi_pass, DEFAULT_PASS, sizeof(wifi_pass) - 1);
            strncpy(oauth_token, DEFAULT_TOKEN, sizeof(oauth_token) - 1);
            creds_save(wifi_ssid, wifi_pass, oauth_token);
        } else {
            // No defaults — run interactive setup wizard
            runSetupWizard();
        }
    }

    // Load refresh token from NVS (or use default)
    if (!creds_load_refresh(refresh_token, &token_expires_at)) {
        if (strlen(DEFAULT_REFRESH_TOKEN) > 0) {
            Serial.println("Using default refresh token from secrets.h");
            strncpy(refresh_token, DEFAULT_REFRESH_TOKEN, sizeof(refresh_token) - 1);
            token_expires_at = 0; // Force immediate refresh
            creds_save_refresh(refresh_token, token_expires_at);
        }
    }

    // Connect WiFi with loaded/entered credentials
    dashboard_draw_waiting("Connecting to WiFi...");
    int wifi_attempts = 0;
    while (!connectWiFi() && wifi_attempts < 3) {
        wifi_attempts++;
        Serial.printf("WiFi attempt %d/3 failed\n", wifi_attempts);
        delay(1000);
    }

    if (WiFi.status() != WL_CONNECTED) {
        // WiFi failed — retry without wiping credentials
        dashboard_draw_waiting("WiFi failed! Rebooting...");
        delay(3000);
        ESP.restart();
    }

    dashboard_draw_waiting("Syncing time...");
    syncNTP();

    dashboard_draw_waiting("Fetching Claude usage...");

    ensureValidToken();
    if (fetchUsage()) {
        dashboard_draw(currentStats);
        touch_set_screen("dashboard");
        lastFetch = millis();
    } else {
        // API fetch failed — retry next loop, don't wipe credentials
        dashboard_draw_waiting("API fetch failed, retrying...");
        lastFetch = millis() - FETCH_INTERVAL + 30000; // retry in 30s
    }
}

void loop() {
    // Check for WiFi icon tap (top-right corner)
    int16_t tx, ty;
    if (touch_get_tap(tx, ty)) {
        bool hit = dashboard_wifi_icon_tapped(tx, ty);
        Serial.printf("[MAIN] tap (%d,%d) wifi_hit=%d\n", tx, ty, hit);
        if (!hit) {
            Serial.printf("[MAIN] MISS — icon zone: x>=%d, y<=%d\n",
                          960 - 60 - 10 - 40, 5 + 50 + 40);
        }
        if (hit) {
            Serial.println("WiFi icon tapped — entering WiFi setup");
            touch_set_screen("wifi_sel");

            // Run WiFi selector — user can cancel with X
            if (!run_wifi_selector(wifi_ssid)) {
                Serial.println("WiFi setup cancelled");
                dashboard_init(); // Force full refresh on return
                dashboard_draw(currentStats);
                touch_set_screen("dashboard");
                lastFetch = millis(); // Prevent immediate re-fetch racing with next tap
                return;
            }

            touch_set_screen("keyboard");
            run_keyboard_input("Enter WiFi Password", wifi_pass, sizeof(wifi_pass), true);

            WiFi.disconnect();

            // Save new WiFi credentials (keep existing OAuth token)
            creds_save(wifi_ssid, wifi_pass, oauth_token);
            Serial.printf("WiFi changed to: %s\n", wifi_ssid);

            // Reconnect with new credentials
            dashboard_draw_waiting("Connecting to WiFi...");
            if (!connectWiFi()) {
                dashboard_draw_waiting("WiFi failed! Rebooting...");
                delay(3000);
                ESP.restart();
            }

            // Re-sync time and fetch
            syncNTP();
            ensureValidToken();
            if (fetchUsage()) {
                dashboard_draw(currentStats);
                touch_set_screen("dashboard");
                lastFetch = millis();
            }
            return;
        }
    }

    // Reconnect WiFi if dropped
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost, reconnecting...");
        if (!connectWiFi()) {
            delay(5000);
            return;
        }
    }

    // Fetch every FETCH_INTERVAL ms
    if (millis() - lastFetch >= FETCH_INTERVAL) {
        ensureValidToken();
        if (fetchUsage()) {
            dashboard_draw(currentStats);
        } else {
            Serial.println("Fetch failed, will retry next interval");
        }
        lastFetch = millis();
    }

    delay(50);
}
