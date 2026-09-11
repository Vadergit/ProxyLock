# Security Policy

## Unterstützte Versionen

Sicherheitskorrekturen werden jeweils für die neueste veröffentlichte
ProxyLock-Version bereitgestellt.

## Sicherheitsproblem melden

Bitte sicherheitsrelevante Schwachstellen nicht zuerst als öffentliches Issue
veröffentlichen. Verwende nach Möglichkeit GitHubs private Security-Advisory-
Funktion unter **Security → Advisories → New draft security advisory**.

Beschreibe betroffene Version, verwendetes Board, mögliche Auswirkungen und
reproduzierbare Schritte. Zugangsdaten, Computerpasswörter, BLE-Codes und
Speicherabbilder dürfen nicht veröffentlicht werden.

## Bekannte Einschränkungen

ProxyLock speichert das Computerpasswort lokal, da es dieses als USB-HID eingibt.
Ohne ESP32 Secure Boot und Flash-Verschlüsselung schützt die Firmware nicht vor
Angreifern mit physischem Zugriff. Das Captive Portal nutzt lokales HTTP und das
Setup-WLAN ist offen; der optionale Portal-Login schützt nur die Weboberfläche.

ProxyLock ist ein experimentelles Komfortprojekt und kein Ersatz für starke
Kontosicherheit, Festplattenverschlüsselung oder Mehrfaktor-Authentifizierung.
