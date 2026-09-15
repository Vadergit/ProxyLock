#include <Arduino.h>
#include <DNSServer.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cmath>
#include <cstring>
#include <mbedtls/sha256.h>

#include "config.h"

namespace {
constexpr char kFirmwareVersion[] = "0.5.1";
const IPAddress kPortalIp(192, 168, 4, 1);
constexpr uint32_t kPortalWindowMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kLoginBlockMs = 30UL * 1000UL;
const NimBLEUUID kAncsService("7905F431-B5CE-4E99-A40F-4B1E122D00D0");
const NimBLEUUID kAncsNotificationSource("9FBF120D-6301-42D9-8C58-25E699A21DBD");

struct DeviceConfig {
  bool configured = false;
  bool macOs = false;
  String bleName = "ProxyLock";
  uint32_t blePasskey = 123456;
  float distanceMetres = 2.0F;
  int rssiAtOneMetre = -59;
  String loginPassword;
  uint16_t wakeDelayMs = 2000;
  uint8_t keyDelayMs = 10;
  uint8_t keyboardLayout = 0;  // 0 = US, 1 = German (Switzerland)
  bool keepAwakeEnabled = false;
  uint16_t keepAwakeIntervalSeconds = 45;
  bool portalAuthEnabled = false;
  String adminSalt;
  String adminHash;
} config;

enum class PresenceState { kUnknown, kFar, kNear };

Preferences preferences;
USBHIDKeyboard keyboard;
DNSServer dnsServer;
WebServer webServer(80);
NimBLEServer* bleServer = nullptr;
NimBLEClient* peerClient = nullptr;
NimBLEHIDDevice* bleHid = nullptr;
volatile bool bleConnected = false;
volatile bool ancsSetupRequested = false;
bool ancsReady = false;
bool ancsUnavailable = false;
uint8_t ancsAttempts = 0;
bool haveRssi = false;
bool haveEverBeenNear = false;
float filteredRssi = -127.0F;
float estimatedDistanceMetres = NAN;
PresenceState presence = PresenceState::kUnknown;
PresenceState pendingPresence = PresenceState::kUnknown;
uint32_t conditionSince = 0;
bool conditionActive = false;
uint32_t disconnectedSince = 0;
bool disconnectPending = false;
uint32_t lastRssiSample = 0;
uint32_t lastAncsAttempt = 0;
uint32_t lastKeepAwakeAt = 0;
bool portalRunning = false;
uint32_t portalStartedAt = 0;
uint32_t lastPortalClientSeenAt = 0;
bool portalHadClient = false;
uint32_t restartAt = 0;
bool restartPending = false;
String sessionToken;
uint8_t failedLogins = 0;
uint32_t loginBlockedAt = 0;
bool loginBlocked = false;

String randomHex(size_t bytes) {
  static const char hex[] = "0123456789abcdef";
  String value;
  value.reserve(bytes * 2);
  for (size_t i = 0; i < bytes; ++i) {
    const uint8_t b = static_cast<uint8_t>(esp_random());
    value += hex[b >> 4];
    value += hex[b & 0x0F];
  }
  return value;
}

String sha256Hex(const String& value) {
  uint8_t digest[32];
  mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(value.c_str()),
                     value.length(), digest, 0);
  String result;
  result.reserve(64);
  static const char hex[] = "0123456789abcdef";
  for (uint8_t b : digest) {
    result += hex[b >> 4];
    result += hex[b & 0x0F];
  }
  return result;
}

bool constantTimeEquals(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t difference = 0;
  for (size_t i = 0; i < a.length(); ++i) difference |= a[i] ^ b[i];
  return difference == 0;
}
String htmlEscape(const String& input) {
  String output;
  output.reserve(input.length() + 8);
  for (char c : input) {
    switch (c) {
      case '&': output += F("&amp;"); break;
      case '<': output += F("&lt;"); break;
      case '>': output += F("&gt;"); break;
      case '\"': output += F("&quot;"); break;
      case '\'': output += F("&#39;"); break;
      default: output += c;
    }
  }
  return output;
}

void loadConfig() {
  // Keep the original NVS namespace so existing installations retain their
  // settings when updating from the pre-rename firmware.
  preferences.begin("near-key", false);
  config.configured = preferences.getBool("configured", false);
  config.macOs = preferences.getBool("macos", false);
  config.bleName = preferences.getString("ble_name", "ProxyLock");
  config.blePasskey = preferences.getUInt("ble_pin", 123456);
  config.distanceMetres = preferences.getFloat("distance", 2.0F);
  config.rssiAtOneMetre = preferences.getInt("rssi1m", -59);
  config.loginPassword = preferences.getString("login_pw", "");
  config.wakeDelayMs = static_cast<uint16_t>(preferences.getUInt("wake_ms", 2000U));
  config.keyDelayMs = static_cast<uint8_t>(preferences.getUInt("key_ms", 10U));
  config.keyboardLayout = static_cast<uint8_t>(preferences.getUInt("kbd_layout", 0U));
  config.keepAwakeEnabled = preferences.getBool("keep_awake", false);
  config.keepAwakeIntervalSeconds = static_cast<uint16_t>(
      preferences.getUInt("awake_sec", 45U));
  config.portalAuthEnabled = preferences.getBool("portal_auth", false);
  config.adminSalt = preferences.getString("admin_salt", "");
  config.adminHash = preferences.getString("admin_hash", "");
  config.distanceMetres = constrain(config.distanceMetres, 0.2F, 10.0F);
  config.rssiAtOneMetre = constrain(config.rssiAtOneMetre, -90, -30);
  if (config.bleName.isEmpty() || config.bleName.length() > 20) config.bleName = "ProxyLock";
  if (config.blePasskey < 100000 || config.blePasskey > 999999) config.blePasskey = 123456;
  config.wakeDelayMs = constrain(config.wakeDelayMs, 500, 10000);
  config.keyDelayMs = constrain(config.keyDelayMs, 5, 150);
  if (!preferences.getBool("timing_v2", false)) {
    config.wakeDelayMs = 200;
    config.keyDelayMs = 10;
    preferences.putUInt("wake_ms", config.wakeDelayMs);
    preferences.putUInt("key_ms", config.keyDelayMs);
    preferences.putBool("timing_v2", true);
  }
  // v0.5.1 separates waking Windows from opening its sign-in screen. Existing
  // installations using the former 200 ms default need enough resume time too.
  if (!preferences.getBool("wake_v3", false)) {
    if (config.wakeDelayMs < 1000) config.wakeDelayMs = 2000;
    preferences.putUInt("wake_ms", config.wakeDelayMs);
    preferences.putBool("wake_v3", true);
  }
  if (config.keyboardLayout > 1) config.keyboardLayout = 0;
  config.keepAwakeIntervalSeconds = constrain(
      config.keepAwakeIntervalSeconds, 15, 300);
  if (config.adminSalt.isEmpty() || config.adminHash.length() != 64) {
    config.portalAuthEnabled = false;
  }
}

void saveConfig() {
  preferences.putBool("configured", config.configured);
  preferences.putBool("macos", config.macOs);
  preferences.putString("ble_name", config.bleName);
  preferences.putUInt("ble_pin", config.blePasskey);
  preferences.putFloat("distance", config.distanceMetres);
  preferences.putInt("rssi1m", config.rssiAtOneMetre);
  preferences.putString("login_pw", config.loginPassword);
  preferences.putUInt("wake_ms", config.wakeDelayMs);
  preferences.putUInt("key_ms", config.keyDelayMs);
  preferences.putUInt("kbd_layout", config.keyboardLayout);
  preferences.putBool("keep_awake", config.keepAwakeEnabled);
  preferences.putUInt("awake_sec", config.keepAwakeIntervalSeconds);
  preferences.putBool("portal_auth", config.portalAuthEnabled);
  preferences.putString("admin_salt", config.adminSalt);
  preferences.putString("admin_hash", config.adminHash);
  // Remove the plaintext Wi-Fi password used by pre-0.3 development builds.
  preferences.remove("portal_pw");
}

bool isAuthorized() {
  if (!config.configured || !config.portalAuthEnabled) return true;
  if (sessionToken.isEmpty() || !webServer.hasHeader("Cookie")) return false;
  return webServer.header("Cookie").indexOf("PLSESSION=" + sessionToken) >= 0;
}

String pageShell(const String& title, const String& content) {
  String page;
  page.reserve(9000);
  page += F("<!doctype html><html lang='de'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta name='theme-color' content='#f5f5f7'><title>");
  page += htmlEscape(title);
  page += F("</title><style>*{box-sizing:border-box}body{margin:0;background:#f5f5f7;color:#1d1d1f;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}.wrap{max-width:720px;margin:0 auto;padding:48px 20px 70px}.brand{display:flex;align-items:center;gap:12px;margin-bottom:28px}.logo{width:42px;height:42px;border-radius:13px;background:linear-gradient(145deg,#0a84ff,#5e5ce6);display:grid;place-items:center;color:white;font-weight:750;font-size:18px;box-shadow:0 8px 24px #0a84ff33}h1{font-size:34px;letter-spacing:-1.2px;margin:0 0 8px}h2{font-size:20px;margin:0 0 18px}p{color:#6e6e73;line-height:1.5;margin:0 0 22px}.card{background:#fff;border-radius:22px;padding:26px;box-shadow:0 1px 0 #00000008,0 12px 38px #0000000a;margin:18px 0}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.full{grid-column:1/-1}label{display:block;font-size:13px;font-weight:650;color:#424245;margin:0 0 7px}input,select{width:100%;border:1px solid #d2d2d7;background:#fbfbfd;border-radius:12px;padding:12px 13px;font:inherit;outline:none}input:focus,select:focus{border-color:#0a84ff;box-shadow:0 0 0 3px #0a84ff22;background:#fff}.hint{font-size:12px;color:#86868b;margin-top:6px}button{width:100%;border:0;border-radius:13px;background:#0071e3;color:#fff;padding:13px 18px;font:600 16px inherit;cursor:pointer}.status{display:flex;gap:8px;flex-wrap:wrap;margin:0 0 20px}.pill{background:#e8f3ff;color:#0066cc;border-radius:999px;padding:7px 11px;font-size:12px;font-weight:650}.warning{background:#fff8e6;color:#7a4d00;border-radius:12px;padding:12px 14px;font-size:13px;margin-bottom:16px}.error{background:#fff0f0;color:#b42318;border-radius:12px;padding:12px 14px;font-size:13px;margin-bottom:16px}.foot{text-align:center;color:#a1a1a6;font-size:12px;margin-top:24px}@media(max-width:620px){.wrap{padding-top:28px}.grid{grid-template-columns:1fr}.full{grid-column:auto}h1{font-size:29px}.card{padding:20px}}</style></head><body><main class='wrap'><div class='brand'><div class='logo'>PL</div><div><strong>ProxyLock</strong><div style='font-size:12px;color:#86868b'>ESP32-S3 Proximity</div></div></div>");
  page += content;
  page += F("<div class='foot'>ProxyLock Firmware v");
  page += kFirmwareVersion;
  page += F(" · Lokale Konfiguration</div></main></body></html>");
  return page;
}

void sendLoginPage(const String& error = "") {
  String body = F("<h1>ProxyLock Login</h1><p>Gib das Portalpasswort ein, um die Einstellungen zu öffnen.</p><div class='card'><h2>Anmelden</h2>");
  if (!error.isEmpty()) body += "<div class='error'>" + htmlEscape(error) + "</div>";
  body += F("<form method='post' action='/login'><label for='password'>Portalpasswort</label><input id='password' name='password' type='password' autocomplete='current-password' required autofocus><div style='height:16px'></div><button type='submit'>Einstellungen öffnen</button></form></div>");
  webServer.sendHeader("Cache-Control", "no-store, max-age=0");
  webServer.send(200, "text/html; charset=utf-8", pageShell("ProxyLock Login", body));
}

void sendSetupPage(const String& error = "") {
  if (!isAuthorized()) { sendLoginPage(); return; }
  const bool first = !config.configured;
  String body = first ? F("<h1>ProxyLock einrichten</h1><p>Einmal konfigurieren, danach arbeitet ProxyLock selbstständig.</p>") : F("<h1>ProxyLock verwalten</h1><p>Geänderte Werte werden nach dem Speichern und Neustart aktiv.</p>");
  body += F("<div class='status'><span class='pill'>BLE: ");
  body += bleConnected ? "verbunden" : "wartet";
  body += F("</span><span class='pill'>Distanz: ");
  body += haveRssi ? String(estimatedDistanceMetres, 2) + " m" : "–";
  body += F("</span></div>");
  if (!error.isEmpty()) body += "<div class='error'>" + htmlEscape(error) + "</div>";
  body += F("<div class='warning'>Das Setup-WLAN ist derzeit offen. Konfiguriere ProxyLock nur in einer vertrauenswürdigen Umgebung.</div>");
  body += F("<form method='post' action='/save'><div class='card'><h2>Computer</h2><div class='grid'><div><label for='os'>Betriebssystem</label><select id='os' name='os'>");
  body += config.macOs ? F("<option value='mac' selected>macOS</option><option value='windows'>Windows</option>") : F("<option value='mac'>macOS</option><option value='windows' selected>Windows</option>");
  body += F("</select></div><div><label for='keyboard_layout'>Tastaturlayout am Anmeldebildschirm</label><select id='keyboard_layout' name='keyboard_layout'>");
  body += config.keyboardLayout == 1 ? F("<option value='us'>English (US)</option><option value='ch' selected>Deutsch (Schweiz)</option>") : F("<option value='us' selected>English (US)</option><option value='ch'>Deutsch (Schweiz)</option>");
  body += F("</select></div><div><label for='distance'>Schaltabstand in Metern</label><input id='distance' name='distance' type='number' min='.2' max='10' step='.1' value='");
  body += String(config.distanceMetres, 1);
  body += F("' required></div><div><label for='wake_delay'>Wartezeit nach Weckimpuls (ms)</label><input id='wake_delay' name='wake_delay' type='number' min='500' max='10000' step='100' value='");
  body += String(config.wakeDelayMs);
  body += F("' required><div class='hint'>Standard: 2000 ms. Bei langsamem Aufwachen erhöhen.</div></div><div><label for='key_delay'>Pause pro Zeichen (ms)</label><input id='key_delay' name='key_delay' type='number' min='5' max='150' step='5' value='");
  body += String(config.keyDelayMs);
  body += F("' required><div class='hint'>Bei fehlenden Zeichen auf 50–80 ms erhöhen.</div></div><div class='full'><label style='font-weight:500'><input style='width:auto;margin-right:7px' type='checkbox' name='keep_awake' value='1'");
  if (config.keepAwakeEnabled) body += F(" checked");
  body += F("> Computer wach halten, solange Telefon verbunden und in Reichweite ist</label><div style='margin-top:12px'><label for='keep_awake_interval'>Keep-awake-Intervall in Sekunden</label><input id='keep_awake_interval' name='keep_awake_interval' type='number' min='15' max='300' step='5' value='");
  body += String(config.keepAwakeIntervalSeconds);
  body += F("' required><div class='hint'>Standard: 45 Sekunden. Sendet einen unbenutzten F24-Tastenimpuls.</div></div></div><div class='full'><label for='login_password'>Windows PIN/Kennwort bzw. macOS-Passwort</label><input id='login_password' name='login_password' type='password' autocomplete='new-password' placeholder='");
  body += first ? "Passwort eingeben oder leer lassen" : "Leer lassen = unverändert";
  body += F("'><div class='hint'>Windows erwartet häufig die Windows-Hello-PIN statt des Microsoft-Kontopassworts. Nur druckbare ASCII-Zeichen verwenden; das Tastaturlayout des Anmeldebildschirms muss passen.</div>"
            "<label style='margin-top:12px;font-weight:500'><input style='width:auto;margin-right:7px' type='checkbox' name='clear_login' value='1'> Gespeichertes Anmeldepasswort löschen</label>"
            "</div></div></div><div class='card'><h2>Bluetooth</h2><div class='grid'><div><label for='ble_name'>Gerätename</label><input id='ble_name' name='ble_name' maxlength='20' value='");
  body += htmlEscape(config.bleName);
  body += F("' required></div><div><label for='ble_pin'>Kopplungscode</label><input id='ble_pin' name='ble_pin' inputmode='numeric' pattern='[0-9]{6}' minlength='6' maxlength='6' value='");
  char pin[7];
  snprintf(pin, sizeof(pin), "%06lu", static_cast<unsigned long>(config.blePasskey));
  body += pin;
  body += F("' required><div class='hint'>Dieser Code darf später sichtbar bleiben.</div></div><div class='full'><label for='rssi1m'>RSSI-Kalibrierung bei genau 1 m</label><input id='rssi1m' name='rssi1m' type='number' min='-90' max='-30' step='1' value='");
  body += String(config.rssiAtOneMetre);
  body += F("' required><div class='hint'>Typischer Startwert: −59 dBm. Beeinflusst die Meter-Schätzung.</div></div></div></div><div class='card'><h2>Portal-Schutz</h2><label style='font-weight:500'><input style='width:auto;margin-right:7px' id='portal_auth' type='checkbox' name='portal_auth' value='1'");
  if (config.portalAuthEnabled) body += F(" checked");
  body += F("> Portal mit Passwort schützen</label><div class='grid' style='margin-top:16px'><div><label for='portal_password'>Neues Portalpasswort</label><input id='portal_password' name='portal_password' type='password' minlength='8' autocomplete='new-password' placeholder='");
  body += config.portalAuthEnabled ? "Leer lassen = unverändert" : "Mindestens 8 Zeichen";
  body += F("'></div><div><label for='portal_password_confirm'>Passwort bestätigen</label><input id='portal_password_confirm' name='portal_password_confirm' type='password' minlength='8' autocomplete='new-password'></div></div><div class='hint'>Das Setup-WLAN bleibt sichtbar. Das Passwort schützt die Weboberfläche ab dem nächsten Neustart. Bei Verlust ist ein vollständiges Löschen und Neuflashen nötig.</div></div><button type='submit'>Speichern und neu starten</button></form>");
  webServer.sendHeader("Cache-Control", "no-store, max-age=0");
  webServer.send(200, "text/html; charset=utf-8", pageShell("ProxyLock Setup", body));
}

bool validBleName(const String& name) {
  if (name.isEmpty() || name.length() > 20) return false;
  for (char c : name) if (static_cast<uint8_t>(c) < 0x20) return false;
  return true;
}

bool validLoginPassword(const String& password) {
  for (char c : password) {
    const uint8_t value = static_cast<uint8_t>(c);
    if (value < 0x20 || value > 0x7E) return false;
  }
  return true;
}

void handleSave() {
  if (!isAuthorized()) { webServer.sendHeader("Location", "/", true); webServer.send(303); return; }
  const String name = webServer.arg("ble_name");
  const String pinText = webServer.arg("ble_pin");
  const float distance = webServer.arg("distance").toFloat();
  const int calibration = webServer.arg("rssi1m").toInt();
  const int wakeDelay = webServer.arg("wake_delay").toInt();
  const int keyDelay = webServer.arg("key_delay").toInt();
  const String keyboardLayout = webServer.arg("keyboard_layout");
  const bool keepAwakeEnabled = webServer.hasArg("keep_awake");
  const int keepAwakeInterval = webServer.arg("keep_awake_interval").toInt();
  const String newLoginPassword = webServer.arg("login_password");
  const bool portalAuthEnabled = webServer.hasArg("portal_auth");
  const String portalPassword = webServer.arg("portal_password");
  const String portalPasswordConfirm = webServer.arg("portal_password_confirm");
  const bool passwordRequired = portalAuthEnabled &&
                                (!config.portalAuthEnabled || config.adminHash.length() != 64);
  const bool passwordInvalid = portalAuthEnabled &&
                               ((passwordRequired && portalPassword.length() < 8) ||
                                (!portalPassword.isEmpty() && portalPassword.length() < 8) ||
                                portalPassword != portalPasswordConfirm);
  if (!validBleName(name) || pinText.length() != 6 || pinText.toInt() < 100000 ||
      distance < 0.2F || distance > 10.0F || calibration < -90 || calibration > -30 ||
      wakeDelay < 500 || wakeDelay > 10000 || keyDelay < 5 || keyDelay > 150 ||
      (keyboardLayout != "us" && keyboardLayout != "ch") ||
      keepAwakeInterval < 15 || keepAwakeInterval > 300 ||
      !validLoginPassword(newLoginPassword) || passwordInvalid) {
    sendSetupPage("Bitte prüfe die Werte und Mindestlängen."); return;
  }
  const bool resetBonds = config.configured &&
                          (name != config.bleName ||
                           static_cast<uint32_t>(pinText.toInt()) != config.blePasskey);
  config.macOs = webServer.arg("os") == "mac";
  config.bleName = name;
  config.blePasskey = static_cast<uint32_t>(pinText.toInt());
  config.distanceMetres = distance;
  config.rssiAtOneMetre = calibration;
  config.wakeDelayMs = static_cast<uint16_t>(wakeDelay);
  config.keyDelayMs = static_cast<uint8_t>(keyDelay);
  config.keyboardLayout = keyboardLayout == "ch" ? 1 : 0;
  config.keepAwakeEnabled = keepAwakeEnabled;
  config.keepAwakeIntervalSeconds = static_cast<uint16_t>(keepAwakeInterval);
  if (webServer.hasArg("clear_login")) config.loginPassword = "";
  else if (!newLoginPassword.isEmpty()) config.loginPassword = newLoginPassword;
  config.portalAuthEnabled = portalAuthEnabled;
  if (!portalAuthEnabled) {
    config.adminSalt = "";
    config.adminHash = "";
    sessionToken = "";
  } else if (!portalPassword.isEmpty()) {
    config.adminSalt = randomHex(16);
    config.adminHash = sha256Hex(config.adminSalt + portalPassword);
  }
  config.configured = true;
  saveConfig();
  if (resetBonds) NimBLEDevice::deleteAllBonds();
  webServer.send(200, "text/html; charset=utf-8", pageShell("Gespeichert", F("<h1>Gespeichert</h1><div class='card'><p>ProxyLock startet neu. Verbinde dich anschließend mit dem konfigurierten Bluetooth-Namen.</p></div>")));
  restartAt = millis();
  restartPending = true;
}

void handleLogin() {
  const uint32_t now = millis();
  if (loginBlocked && now - loginBlockedAt < kLoginBlockMs) {
    sendLoginPage("Zu viele Versuche. Bitte 30 Sekunden warten.");
    return;
  }
  loginBlocked = false;
  const String candidate = sha256Hex(config.adminSalt + webServer.arg("password"));
  if (constantTimeEquals(candidate, config.adminHash)) {
    failedLogins = 0;
    sessionToken = randomHex(24);
    webServer.sendHeader("Set-Cookie", "PLSESSION=" + sessionToken + "; HttpOnly; SameSite=Strict; Path=/");
    webServer.sendHeader("Location", "/", true);
    webServer.send(303);
    return;
  }
  if (++failedLogins >= 5) {
    failedLogins = 0;
    loginBlockedAt = now;
    loginBlocked = true;
  }
  sendLoginPage("Falsches Portalpasswort.");
}

void handleCaptiveProbe() {
  webServer.sendHeader("Location", "http://192.168.4.1/", true);
  webServer.sendHeader("Cache-Control", "no-store, max-age=0");
  webServer.send(302, "text/plain", "");
}

// Android considers a non-empty HTTP 200 response to a generate_204 probe a
// captive network. Serving the portal itself is more reliable on Samsung/One
// UI versions that do not consistently follow a 302 inside the mini-browser.
void handleAndroidProbe() {
  sendSetupPage();
}

void handleCaptiveApi() {
  webServer.sendHeader("Cache-Control", "no-store, max-age=0");
  webServer.send(200, "application/captive+json",
                 "{\"captive\":true,\"user-portal-url\":"
                 "\"http://192.168.4.1/\"}");
}

void startPortal() {
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFF));
  const String ssid = String("ProxyLock-Setup-") + suffix;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kPortalIp, kPortalIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid.c_str());
  dnsServer.start(53, "*", kPortalIp);
  const char* headers[] = {"Cookie"};
  webServer.collectHeaders(headers, 1);
  webServer.on("/", HTTP_GET, [] { sendSetupPage(); });
  webServer.on("/login", HTTP_POST, handleLogin);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.on("/generate_204", HTTP_ANY, handleAndroidProbe);
  webServer.on("/gen_204", HTTP_ANY, handleAndroidProbe);
  webServer.on("/mobile/status.php", HTTP_ANY, handleAndroidProbe);
  webServer.on("/hotspot-detect.html", HTTP_ANY, handleCaptiveProbe);
  webServer.on("/library/test/success.html", HTTP_ANY, handleCaptiveProbe);
  webServer.on("/connecttest.txt", HTTP_ANY, handleCaptiveProbe);
  webServer.on("/ncsi.txt", HTTP_ANY, handleCaptiveProbe);
  webServer.on("/canonical.html", HTTP_ANY, handleCaptiveProbe);
  webServer.on("/success.txt", HTTP_ANY, handleCaptiveProbe);
  webServer.on("/check_network_status.txt", HTTP_ANY, handleCaptiveProbe);
  webServer.on("/wifistub.html", HTTP_ANY, handleCaptiveProbe);
  webServer.on("/.well-known/captive-portal", HTTP_ANY, handleCaptiveApi);
  webServer.on("/favicon.ico", HTTP_ANY,
               [] { webServer.send(204, "image/x-icon", ""); });
  webServer.onNotFound(handleCaptiveProbe);
  webServer.begin();
  portalRunning = true;
  portalStartedAt = millis();
  lastPortalClientSeenAt = portalStartedAt;
  portalHadClient = false;
}

void stopPortal() {
  webServer.stop(); dnsServer.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); portalRunning = false;
}

void sendLockAction() {
  if (!Config::kUsbActionsEnabled) return;
  if (config.macOs) { keyboard.press(KEY_LEFT_CTRL); keyboard.press(KEY_LEFT_GUI); keyboard.press('q'); }
  else { keyboard.press(KEY_LEFT_GUI); keyboard.press('l'); }
  delay(80); keyboard.releaseAll();
}

void sendRawKey(uint8_t usage, bool shift = false, bool altGr = false) {
  if (shift) keyboard.press(KEY_LEFT_SHIFT);
  if (altGr) keyboard.press(KEY_RIGHT_ALT);
  keyboard.pressRaw(usage);
  delay(12);
  keyboard.releaseAll();
}

bool sendSwissGermanCharacter(char character) {
  if (character >= 'a' && character <= 'z') {
    uint8_t usage = static_cast<uint8_t>(0x04 + character - 'a');
    if (character == 'y') usage = 0x1D;
    else if (character == 'z') usage = 0x1C;
    sendRawKey(usage);
    return true;
  }
  if (character >= 'A' && character <= 'Z') {
    const char lower = static_cast<char>(character - 'A' + 'a');
    uint8_t usage = static_cast<uint8_t>(0x04 + lower - 'a');
    if (lower == 'y') usage = 0x1D;
    else if (lower == 'z') usage = 0x1C;
    sendRawKey(usage, true);
    return true;
  }
  if (character >= '1' && character <= '9') {
    sendRawKey(static_cast<uint8_t>(0x1E + character - '1'));
    return true;
  }
  if (character == '0') { sendRawKey(0x27); return true; }

  uint8_t usage = 0;
  bool shift = false;
  bool altGr = false;
  bool deadKey = false;
  switch (character) {
    case ' ': usage = 0x2C; break;
    case '!': usage = 0x30; shift = true; break;
    case '"': usage = 0x1F; shift = true; break;
    case '#': usage = 0x20; altGr = true; break;
    case '$': usage = 0x32; break;
    case '%': usage = 0x22; shift = true; break;
    case '&': usage = 0x23; shift = true; break;
    case '\'': usage = 0x2D; break;
    case '(': usage = 0x25; shift = true; break;
    case ')': usage = 0x26; shift = true; break;
    case '*': usage = 0x20; shift = true; break;
    case '+': usage = 0x1E; shift = true; break;
    case ',': usage = 0x36; break;
    case '-': usage = 0x38; break;
    case '.': usage = 0x37; break;
    case '/': usage = 0x24; shift = true; break;
    case ':': usage = 0x37; shift = true; break;
    case ';': usage = 0x36; shift = true; break;
    case '<': usage = 0x64; break;
    case '=': usage = 0x27; shift = true; break;
    case '>': usage = 0x64; shift = true; break;
    case '?': usage = 0x2D; shift = true; break;
    case '@': usage = 0x1F; altGr = true; break;
    case '[': usage = 0x2F; altGr = true; break;
    case '\\': usage = 0x64; altGr = true; break;
    case ']': usage = 0x30; altGr = true; break;
    case '^': usage = 0x2E; deadKey = true; break;
    case '_': usage = 0x38; shift = true; break;
    case '`': usage = 0x2E; shift = true; deadKey = true; break;
    case '{': usage = 0x34; altGr = true; break;
    case '|': usage = 0x24; altGr = true; break;
    case '}': usage = 0x32; altGr = true; break;
    case '~': usage = 0x2E; altGr = true; deadKey = true; break;
    default: return false;
  }
  sendRawKey(usage, shift, altGr);
  if (deadKey) sendRawKey(0x2C);
  return true;
}

void sendPasswordCharacter(char character) {
  if (config.keyboardLayout == 1) sendSwissGermanCharacter(character);
  else keyboard.write(static_cast<uint8_t>(character));
}

void sendNearAction() {
  if (!Config::kUsbActionsEnabled) return;
  if (config.macOs) {
    // Keep the established macOS sequence, which wakes and focuses the login
    // field reliably on the tested setup.
    keyboard.write(' ');
    delay(config.wakeDelayMs);
  } else {
    // A first, otherwise unused key wakes a sleeping display/PC. Only after
    // Windows had time to resume do we open the actual sign-in screen.
    keyboard.write(KEY_F24);
    delay(config.wakeDelayMs);
    keyboard.write(' ');
    delay(500);

    // When Windows already shows the input field, the wake-up space can land
    // in it. Clear any existing input before entering the configured secret.
    keyboard.press(KEY_LEFT_CTRL);
    keyboard.press('a');
    delay(60);
    keyboard.releaseAll();
    delay(40);
    keyboard.write(KEY_BACKSPACE);
    delay(100);
  }
  for (size_t i = 0; i < config.loginPassword.length(); ++i) {
    const uint8_t character = static_cast<uint8_t>(config.loginPassword[i]);
    if (character >= 0x20 && character <= 0x7E) {
      sendPasswordCharacter(static_cast<char>(character));
    }
    delay(config.keyDelayMs);
  }
  delay(150);
  keyboard.write(KEY_RETURN);
}

void changePresence(PresenceState next) {
  if (presence == next) return;
  const PresenceState previous = presence;
  presence = next;
  conditionActive = false;
  if (next == PresenceState::kNear) { haveEverBeenNear = true; sendNearAction(); }
  else if (next == PresenceState::kFar && previous == PresenceState::kNear && haveEverBeenNear) sendLockAction();
}

void discardAncsNotification(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool) {}

class ServerCallbacks final : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    peerClient = server->getClient(info); bleConnected = true; ancsReady = false;
    ancsUnavailable = false; ancsAttempts = 0; ancsSetupRequested = true;
    haveRssi = false; conditionActive = false;
    disconnectPending = false;
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    bleConnected = false; ancsReady = false; ancsSetupRequested = false;
    ancsUnavailable = false; ancsAttempts = 0; peerClient = nullptr;
    haveRssi = false; disconnectedSince = millis();
    disconnectPending = true;
  }
  uint32_t onPassKeyDisplay() override { return config.blePasskey; }
  void onAuthenticationComplete(NimBLEConnInfo&) override { ancsSetupRequested = true; }
} serverCallbacks;

void startBle() {
  NimBLEDevice::init(config.bleName.c_str());
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(config.blePasskey);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setPower(3);
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(&serverCallbacks, false);
  bleServer->advertiseOnDisconnect(true);
  static uint8_t reportMap[] = {0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x01,0x15,0x00,0x25,0x01,0x09,0xE9,0x09,0xEA,0x09,0xE2,0x09,0xCD,0x75,0x01,0x95,0x04,0x81,0x02,0x75,0x04,0x95,0x01,0x81,0x03,0xC0};
  bleHid = new NimBLEHIDDevice(bleServer);
  bleHid->setManufacturer("ProxyLock Open Source");
  bleHid->setPnp(0x02, 0x303A, 0x1001, 0x0200);
  bleHid->setHidInfo(0x00, 0x02);
  bleHid->getInputReport(1);
  bleHid->setReportMap(reportMap, sizeof(reportMap));
  bleHid->setBatteryLevel(100);
  bleHid->startServices();
  NimBLEAdvertising* advertising = bleServer->getAdvertising();
  NimBLEAdvertisementData advertisement;
  advertisement.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advertisement.setCompleteServices(NimBLEUUID(static_cast<uint16_t>(0x1812)));
  uint8_t solicitation[18] = {17, 0x15};
  std::memcpy(solicitation + 2, kAncsService.getValue(), 16);
  advertisement.addData(solicitation, sizeof(solicitation));
  advertisement.setShortName("PL");
  advertising->setAdvertisementData(advertisement);
  NimBLEAdvertisementData scanResponse;
  scanResponse.setName(config.bleName.c_str());
  scanResponse.setAppearance(GENERIC_HID);
  advertising->setScanResponseData(scanResponse);
  advertising->enableScanResponse(true);
  advertising->setMinInterval(160); advertising->setMaxInterval(240); advertising->start();
}

void trySetupAncs() {
  if (!bleConnected || peerClient == nullptr || ancsReady || ancsUnavailable) return;
  if (!ancsSetupRequested && millis() - lastAncsAttempt < 3000) return;
  ancsSetupRequested = false; lastAncsAttempt = millis();
  if (!peerClient->getConnInfo().isEncrypted()) { peerClient->secureConnection(true); return; }
  peerClient->getServices(true);
  NimBLERemoteService* service = peerClient->getService(kAncsService);
  if (service == nullptr) {
    // iOS may publish ANCS shortly after authorization. Ten retries allow
    // that delay; Android is then marked unsupported for this connection.
    if (++ancsAttempts >= 10) ancsUnavailable = true;
    return;
  }
  NimBLERemoteCharacteristic* source = service->getCharacteristic(kAncsNotificationSource);
  if (source != nullptr && source->canNotify()) ancsReady = source->subscribe(true, discardAncsNotification, true);
}

void updatePresence() {
  const uint32_t now = millis();
  if (!bleConnected || peerClient == nullptr) {
    if (presence == PresenceState::kNear && disconnectPending &&
        now - disconnectedSince >= Config::kConnectionLostHoldMs) {
      disconnectPending = false;
      changePresence(PresenceState::kFar);
    }
    return;
  }
  if (now - lastRssiSample < Config::kRssiSampleIntervalMs) return;
  lastRssiSample = now;
  const int sample = peerClient->getRssi();
  if (sample > 20 || sample < -127) return;
  if (!haveRssi) { filteredRssi = sample; haveRssi = true; }
  else filteredRssi += Config::kRssiEmaAlpha * (sample - filteredRssi);
  estimatedDistanceMetres = std::pow(10.0F, (config.rssiAtOneMetre - filteredRssi) / (10.0F * Config::kPathLossExponent));
  const PresenceState target = estimatedDistanceMetres <= config.distanceMetres ? PresenceState::kNear : PresenceState::kFar;
  if (target == presence) {
    conditionActive = false;
    pendingPresence = PresenceState::kUnknown;
    return;
  }
  if (!conditionActive || pendingPresence != target) {
    conditionSince = now;
    pendingPresence = target;
    conditionActive = true;
  }
  const uint32_t hold = target == PresenceState::kNear ? Config::kNearHoldMs : Config::kFarHoldMs;
  if (now - conditionSince >= hold) changePresence(target);
}

void updateKeepAwake() {
  const uint32_t now = millis();
  if (!config.keepAwakeEnabled || !bleConnected || presence != PresenceState::kNear) {
    lastKeepAwakeAt = now;
    return;
  }
  const uint32_t intervalMs =
      static_cast<uint32_t>(config.keepAwakeIntervalSeconds) * 1000UL;
  if (now - lastKeepAwakeAt < intervalMs) return;
  keyboard.write(KEY_F24);
  lastKeepAwakeAt = now;
}
}  // namespace

void setup() {
  loadConfig();
  USB.productName("ProxyLock"); USB.manufacturerName("ProxyLock Open Source");
  keyboard.begin(); USB.begin();
  startPortal();
  if (config.configured) startBle();
}

void loop() {
  if (portalRunning) {
    dnsServer.processNextRequest(); webServer.handleClient();
    const uint32_t now = millis();
    if (WiFi.softAPgetStationNum() > 0) {
      portalHadClient = true;
      lastPortalClientSeenAt = now;
    } else if (config.configured) {
      const uint32_t idleSince = portalHadClient ? lastPortalClientSeenAt
                                                : portalStartedAt;
      if (now - idleSince >= kPortalWindowMs) stopPortal();
    }
  }
  if (config.configured) { trySetupAncs(); updatePresence(); updateKeepAwake(); }
  if (restartPending && millis() - restartAt >= 1200) ESP.restart();
  delay(3);
}
