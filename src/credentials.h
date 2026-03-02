#pragma once

bool creds_load(char *ssid, char *password, char *token);
void creds_save(const char *ssid, const char *password, const char *token);
void creds_clear();
bool creds_exist();

// Refresh token storage
bool creds_load_refresh(char *refresh_token, unsigned long *expires_at);
void creds_save_refresh(const char *refresh_token, unsigned long expires_at);
void creds_save_token(const char *token, const char *refresh_token, unsigned long expires_at);
