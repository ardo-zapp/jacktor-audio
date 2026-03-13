#include "net.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <time.h>
#include <Update.h>
#include "display.h"
#include "comms.h"

static AsyncWebServer server(80);
static Preferences prefs;
static bool connected = false;
static uint32_t last_ntp_sync = 0;

// Waktu NTP (WIB)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;
const int   daylightOffset_sec = 0;

static String saved_ssid;
static String saved_pass;

void netInit() {
  prefs.begin("wifi", false);
  saved_ssid = prefs.getString("ssid", "");
  saved_pass = prefs.getString("pass", "");

  if (saved_ssid.length() > 0) {
    displayBootLog((String("[ WAIT ] Connecting to ") + saved_ssid + "...").c_str());
    WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
  } else {
    displayBootLog("[ WARN ] No WiFi configured. Use Settings Tab.");
  }

  // Siapkan Web Server OTA
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<html><body><h1>Jacktor Audio Panel</h1><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form></body></html>");
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    bool shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
    if(shouldReboot) {
      delay(500);
      ESP.restart();
    }
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      if(!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("Update Success: %uB\n", index+len);
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
  displayBootLog("[ OK ] Web Server started for OTA.");
}

void netConnectToWifi(const String& ssid, const String& password) {
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  saved_ssid = ssid;
  saved_pass = password;
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), password.c_str());
}

bool netIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String netGetIP() {
  if (netIsConnected()) return WiFi.localIP().toString();
  return "Disconnected";
}

void netSyncRTC() {
  if (!netIsConnected()) return;

  struct tm timeinfo;
  // Coba dapatkan waktu dengan timeout 5 detik
  if (!getLocalTime(&timeinfo, 5000)) {
    displayBootLog("[ WARN ] NTP Sync failed. Retrying later.");
    return;
  }

  // Periksa apakah tahunnya masuk akal (>2023). Jika tahun 1970, berarti gagal sync epoch
  if (timeinfo.tm_year < 123) {
      return;
  }

  // Format waktu lokal ISO 8601 YYYY-MM-DDTHH:MM:SS
  char timeStr[32];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", &timeinfo);

  // Kirim JSON sinkronisasi waktu ke amplifier
  JsonDocument doc;
  doc["type"] = "cmd";
  doc["cmd"]["rtc_set"] = timeStr;
  commsSendAmpCommand(doc);
}

void netTick(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!connected) {
      connected = true;
      displayBootLog((String("[ OK ] WiFi connected: ") + WiFi.localIP().toString()).c_str());
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    }
    // Auto NTP sync tiap 1 jam
    if (now - last_ntp_sync >= 3600000 || last_ntp_sync == 0) {
      netSyncRTC();
      last_ntp_sync = now;
    }
  } else {
    connected = false;
  }
}
