#pragma once

bool creds_load(char *ssid, char *password, char *token);
void creds_save(const char *ssid, const char *password, const char *token);
void creds_clear();
bool creds_exist();
