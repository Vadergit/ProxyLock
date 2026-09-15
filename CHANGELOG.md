# Changelog

## 0.5.0 – 2026-09-15

- Optionales Keep-awake im Captive Portal ergänzt.
- Konfigurierbares Intervall von 15 bis 300 Sekunden.
- Unbelegten F24-Impuls zur Verhinderung des Ruhezustands verwendet.
- Keep-awake stoppt automatisch bei Entfernung oder BLE-Verbindungsverlust.

## 0.4.3 – 2026-09-15

- Standard-Aufwachzeit auf 200 ms gesetzt.
- Standard-Zeichenpause auf 10 ms gesetzt.
- Bestehende Installationen einmalig auf die neuen Werte migriert.
- Untergrenze der konfigurierbaren Aufwachzeit auf 100 ms reduziert.

## 0.4.2 – 2026-09-15

- Tastaturlayout im Captive Portal auswählbar gemacht.
- Abbildung für `Deutsch (Schweiz)` ergänzt.
- Schweizer `Y/Z` sowie druckbare ASCII-Sonderzeichen korrekt per USB-HID gesendet.
- Insbesondere die falsche Ausgabe von `(` anstelle von `*` behoben.

## 0.4.1 – 2026-09-15

- Windows-Aufwachzeit im Portal konfigurierbar gemacht.
- Konfigurierbare Pause zwischen einzelnen Zeichen ergänzt.
- Windows-Eingabefeld vor der Anmeldung zuverlässig geleert.
- Hinweis auf Windows-Hello-PIN und Tastaturlayout ergänzt.
- Nicht unterstützte Nicht-ASCII-Zeichen sicher abgewiesen.

## 0.4.0 – 2026-09-11

- Projekt und alle sichtbaren Gerätenamen in ProxyLock umbenannt.
- Optionalen Passwortschutz für die Weboberfläche ergänzt.
- Passwortänderung und Deaktivierung des Portal-Schutzes ergänzt.
- Recovery durch vollständiges Löschen des ESP32-Flash dokumentiert.
- Captive-Portal-Erkennung für Apple, Android, Samsung, Windows und Firefox.
- Generisches, bildschirmloses ESP32-S3-Image mit 4 MB Flash bereitgestellt.
