# ProxyLock

**Your presence is the key.**

ProxyLock ist eine Open-Source-Firmware für ESP32-S3-Boards. Das Gerät erkennt
die Entfernung eines gekoppelten iPhones oder Android-Telefons anhand der
Bluetooth-Signalstärke und meldet einen per USB angeschlossenen Windows-PC oder
Mac automatisch an beziehungsweise sperrt ihn beim Weggehen.

Die Einrichtung erfolgt vollständig über ein lokales Captive Portal. Nach dem
Flashen müssen weder Quellcode noch Pins angepasst werden; Display, App und
Cloudkonto werden nicht benötigt.

> **Experimentelles Projekt:** RSSI ist keine exakte Entfernungsmessung und das
> automatische Eingeben eines Computerpassworts hat Sicherheitsrisiken. Bitte
> zuerst mit einem Testkonto ausprobieren.

## Funktionen

- ein Firmware-Image für ESP32-S3-Boards mit mindestens 4 MB Flash und nativem USB;
- Captive Portal für iPhone/iPad, Android/Samsung, macOS und Windows;
- Auswahl zwischen Windows und macOS;
- einstellbarer Abstand von 0,2 bis 10 m und RSSI-Kalibrierung;
- frei wählbarer Bluetooth-Gerätename und sechsstelliger Kopplungscode;
- Unterstützung privater, wechselnder iPhone-Bluetooth-Adressen durch BLE-Bonding;
- BLE Consumer Control statt einer BLE-Tastatur, damit die Bildschirmtastatur
  des Telefons weiterhin erscheint;
- USB-HID-Tastatur ausschließlich gegenüber dem Computer;
- optionaler Passwortschutz für die Weboberfläche;
- optionales Keep-awake bei verbundener, naher BLE-Quelle;
- Portal bei jedem Neustart mindestens fünf Minuten verfügbar;
- keine App, kein Internetzugang und kein Cloudkonto erforderlich.

## Benötigte Hardware

- ESP32-S3 mit mindestens 4 MB Flash;
- USB-Anschluss mit direkter Verbindung zum nativen USB/OTG des ESP32-S3;
- USB-Datenkabel.

Getestet wurde die Firmware mit dem **LilyGO T-QT Pro ESP32-S3, 4 MB Flash**.
Andere S3-Boards funktionieren, wenn deren USB-Anschluss natives USB-HID
unterstützt. Ein reiner USB-UART-Anschluss kann keine Tastatur emulieren.

ESP32, ESP32-C3 und andere Varianten ohne natives USB-HID sind nicht mit diesem
fertigen Image kompatibel.

## Schnellstart mit fertiger Firmware

Die vollständige 4-MB-Firmware befindet sich unter
[`firmware/proxylock-esp32s3-4mb-v0.5.0.bin`](firmware/proxylock-esp32s3-4mb-v0.5.0.bin)
und zusätzlich im GitHub-Release `v0.5.0`.

### 1. Flashmodus aktivieren

1. Board vom USB-Kabel trennen.
2. **BOOT** gedrückt halten.
3. USB-Kabel einstecken.
4. Nach etwa zwei Sekunden BOOT loslassen.

### 2. Firmware schreiben

Python und `esptool` installieren:

```sh
python3 -m pip install esptool
```

Verfügbare Ports anzeigen:

```sh
python3 -m esptool --chip esp32s3 chip-id
```

Firmware unter macOS/Linux flashen (Port entsprechend ersetzen):

```sh
python3 -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodem1101 \
  write-flash 0x0 firmware/proxylock-esp32s3-4mb-v0.5.0.bin
```

Unter Windows sieht der Port beispielsweise so aus:

```powershell
py -m esptool --chip esp32s3 --port COM5 write-flash 0x0 firmware/proxylock-esp32s3-4mb-v0.5.0.bin
```

Danach das Board kurz trennen und ohne gedrückte BOOT-Taste wieder verbinden.

## Ersteinrichtung

1. Mit dem offenen WLAN **`ProxyLock-Setup-XXXX`** verbinden.
2. Das Portal sollte automatisch erscheinen. Andernfalls im Browser
   **http://192.168.4.1** öffnen.
3. Windows oder macOS auswählen.
4. Unter Windows die aktuell ausgewählte **Windows-Hello-PIN oder das Kennwort**
   eingeben. Das Feld darf auch leer bleiben, wenn nur Sperren oder Aufwecken
   getestet werden soll.
5. Gewünschten Schaltabstand und RSSI-Wert bei einem Meter einstellen.
6. Bluetooth-Namen und sechsstelligen Kopplungscode festlegen.
7. Optional **„Portal mit Passwort schützen“** aktivieren und ein Passwort mit
   mindestens acht Zeichen zweimal eingeben.
8. Speichern. ProxyLock startet neu.
9. Das Telefon in dessen Bluetooth-Einstellungen mit dem konfigurierten Namen
   koppeln und den sechsstelligen Code bestätigen.

Bluetooth wird erst nach der ersten gespeicherten Konfiguration gestartet.

## Funktionsweise

ProxyLock hält eine gebondete BLE-Verbindung zum Telefon und liest regelmäßig
deren RSSI. Daraus wird mit einer kalibrierbaren Näherungsformel eine ungefähre
Distanz berechnet.

- näher als der Grenzwert für mindestens 0,4 s: Bildschirm aufwecken,
  Computerpasswort eingeben und Enter senden;
- weiter entfernt als der Grenzwert für mindestens 5 s: Computer sperren;
- BLE-Verbindungsverlust für mindestens 5 s: Computer sperren.

Windows wird mit `Windows + L`, macOS mit `Control + Command + Q` gesperrt.
Filterung und Haltezeiten vermeiden möglichst, dass einzelne RSSI-Ausreißer
sofort eine Aktion auslösen.

### Windows meldet „falsches Passwort“

- Auf dem Windows-Anmeldebildschirm unter **Anmeldeoptionen** kontrollieren, ob
  gerade PIN oder Kennwort ausgewählt ist. ProxyLock muss genau diesen Wert senden.
- Im Portal dasselbe Tastaturlayout wie am Windows-Anmeldebildschirm auswählen.
  Für Schweizer Windows-Systeme ist das **Deutsch (Schweiz)**. Damit werden unter
  anderem `Y/Z`, `*`, Klammern und weitere ASCII-Sonderzeichen korrekt umgesetzt.
- Die **Wartezeit nach Aufwecken** zunächst auf `2000 ms` stellen.
- Die **Pause pro Zeichen** zunächst auf `50 ms` stellen.
- Das Passwort testweise in einem leeren Editorfeld ausgeben lassen und prüfen,
  ob Zeichen fehlen oder vertauscht sind.
- Ist ein anderes als das ausgewählte Tastaturlayout aktiv, können insbesondere
  `Y/Z` und Sonderzeichen vertauscht werden.

Ab Version 0.4.1 leert ProxyLock das Windows-Eingabefeld vor der Eingabe, wartet
konfigurierbar auf den Anmeldebildschirm und sendet jedes Zeichen mit einer
separaten Pause. Seit Version 0.4.3 sind die Standardwerte `200 ms` und `10 ms`.

## Keep-awake

Im Portal kann **„Computer wach halten, solange Telefon verbunden und in
Reichweite ist“** aktiviert werden. ProxyLock sendet dann standardmäßig alle
45 Sekunden einen kurzen `F24`-Tastenimpuls. Diese Taste ist normalerweise
unbelegt und erzeugt kein sichtbares Zeichen, zählt aber als Eingabeaktivität.

Keep-awake läuft nur, wenn Bluetooth verbunden ist und das Telefon als **nah**
erkannt wurde. Sobald das Telefon ausserhalb des konfigurierten Grenzwerts liegt
oder die Verbindung abbricht, werden keine weiteren Impulse gesendet. Das
Intervall kann zwischen 15 und 300 Sekunden eingestellt werden.

## Abstand richtig einstellen

RSSI schwankt durch Körper, Tisch, Wände, Taschen, Antennenausrichtung und
Funkreflexionen. Die Meteranzeige ist deshalb eine Schätzung.

1. Telefon genau einen Meter vom ESP32-S3 entfernt platzieren.
2. Im Portal den aktuellen Wert beobachten.
3. Den RSSI-Kalibrierwert schrittweise anpassen.
4. Danach den gewünschten Schaltabstand testen.

Für einen sehr kurzen Bereich zunächst etwa `0,5 m` wählen und anschließend in
kleinen Schritten korrigieren.

## Captive Portal

Beim ersten Start bleibt das Portal aktiv, bis die Konfiguration gespeichert
wurde. Danach wird es bei jedem Neustart mindestens fünf Minuten geöffnet.
Solange ein WLAN-Gerät verbunden ist, bleibt es ohne Zeitlimit aktiv. Nach der
Trennung des letzten Clients beginnt die Fünf-Minuten-Frist erneut.

ProxyLock beantwortet typische Erkennungsadressen von Apple, Android/Google,
Samsung, Windows und Firefox. VPN, Private DNS oder herstellerspezifische
HTTPS-Prüfungen können das automatische Pop-up trotzdem verhindern. In diesem
Fall funktioniert **http://192.168.4.1** weiterhin.

## Optionaler Portal-Schutz

Ohne aktivierten Schutz öffnet sich die Weboberfläche direkt. Mit aktiviertem
Schutz erscheint zuerst der ProxyLock-Login. Das Portalpasswort:

- wird nicht im Klartext gespeichert;
- kann nach einer Anmeldung geändert werden;
- kann durch Entfernen des Häkchens deaktiviert werden;
- sperrt den Login nach fünf Fehlversuchen für 30 Sekunden.

Das Setup-WLAN selbst bleibt offen. Der Schutz gilt für die Weboberfläche.

### Portalpasswort vergessen

Es gibt bewusst kein universelles Hintertür-Passwort. Der komplette Flash muss
gelöscht und ProxyLock neu eingerichtet werden:

```sh
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 erase-flash
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 \
  write-flash 0x0 firmware/proxylock-esp32s3-4mb-v0.5.0.bin
```

Unter Windows den Port und `python3` entsprechend durch `COM…` und `py` ersetzen.
Ein normaler Neustart löscht weder Passwort noch Konfiguration.

## Bestehende Einstellungen ändern

Board neu starten und innerhalb von fünf Minuten mit
`ProxyLock-Setup-XXXX` verbinden. Beim Speichern gilt für das Computerpasswort:

- leeres Feld: bestehendes Passwort unverändert lassen;
- neues Passwort: gespeichertes Passwort ersetzen;
- **„Gespeichertes Anmeldepasswort löschen“**: Passwort entfernen.

Das Passwort wird niemals zurück in das HTML-Formular übertragen. Werden
Bluetooth-Name oder Kopplungscode geändert, löscht ProxyLock alte BLE-Bonds. Das
Gerät muss danach auch auf dem Telefon ignoriert und neu gekoppelt werden.

## Selbst kompilieren

[PlatformIO](https://platformio.org/) installieren und im Repository ausführen:

```sh
pio run
pio run -t upload
```

Die Build-Konfiguration verwendet Arduino für ESP32 und
`NimBLE-Arduino 2.3.7`. Display, PSRAM und benutzerdefinierte Pins sind nicht
erforderlich.

## Sicherheitshinweise

- ProxyLock muss das Computerpasswort lokal speichern, um es per USB einzugeben.
- Ohne Secure Boot und Flash-Verschlüsselung kann physischer Zugriff auf das
  Board das Auslesen gespeicherter Daten ermöglichen.
- Das lokale Portal verwendet HTTP und das Setup-WLAN bleibt offen.
- Ohne Portal-Schutz kann jeder verbundene WLAN-Client Einstellungen ändern.
- Ein starkes, separates Portalpasswort verwenden – nicht das Computerpasswort.
- Für öffentlich zugängliche oder besonders schützenswerte Computer ist dieses
  experimentelle System nicht als alleiniger Sicherheitsmechanismus gedacht.

Weitere Details und das Melden vertraulicher Probleme stehen in
[`SECURITY.md`](SECURITY.md).

## Projektstruktur

```text
boards/       Generische PlatformIO-Boarddefinition
firmware/     Fertiges, zusammengeführtes Flash-Image
include/      Einstellbare Firmware-Konstanten
src/          Firmware-Quellcode
platformio.ini
```

## Lizenz

ProxyLock steht unter der [MIT-Lizenz](LICENSE).
