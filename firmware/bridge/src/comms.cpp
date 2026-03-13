#include "comms.h"
#include "config.h"
#include "display.h"
#include "USB.h"

String hostRxBuffer;
String ampRxBuffer;
String lastAmpTelemetry;

// Anti-spam debounce timers for USB events
static uint32_t last_usb_suspend_ms = 0;
static uint32_t last_usb_resume_ms = 0;
static bool pending_suspend = false;
static bool pending_resume = false;

static void usbEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  if (event_base == ARDUINO_USB_EVENTS) {
    uint32_t now = millis();
    switch (event_id) {
      case ARDUINO_USB_SUSPEND_EVENT:
        // Setel antrean event suspend, batalkan resume jika ada
        pending_suspend = true;
        pending_resume = false;
        last_usb_suspend_ms = now;
        break;
      case ARDUINO_USB_RESUME_EVENT:
        // Setel antrean event resume, batalkan suspend jika ada
        pending_resume = true;
        pending_suspend = false;
        last_usb_resume_ms = now;
        break;
    }
  }
}

void commsInit() {
  Serial.begin(HOST_SERIAL_BAUD); // USB CDC Native
  Serial1.begin(AMP_SERIAL_BAUD, SERIAL_8N1, PIN_UART_AMP_RX, PIN_UART_AMP_TX);

  USB.onEvent(usbEventCallback);

  hostRxBuffer.reserve(BRIDGE_MAX_FRAME);
  ampRxBuffer.reserve(BRIDGE_MAX_FRAME);
}

void commsSendAmpCommandRaw(const String &jsonStr) {
  Serial1.print(jsonStr);
  Serial1.print('\n');
}

void commsSendAmpCommand(JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  commsSendAmpCommandRaw(out);
}

static void forwardToHost(const String &line) {
  Serial.print(line);
  Serial.print('\n');
}

static void handleAmpFrame(const String &line) {
  forwardToHost(line); // forward segala respon ke PC host
  JsonDocument doc;
  if (deserializeJson(doc, line) == DeserializationError::Ok) {
    const char *type = doc["type"] | "";
    if (strcmp(type, "telemetry") == 0) {
      lastAmpTelemetry = line;
      displayUpdateTelemetry(doc);
    }
  }
}

static void handleHostFrame(const String &line) {
  // Parsing json utk kontrol, atau cuma redirect ke Amp
  // tools/esp32_monitor dkk mengirim json format 'cmd'
  // atau command baris.
  if (line.startsWith("{")) {
    commsSendAmpCommandRaw(line); // forward as it is ke amplifier
  }
}

void commsTick(uint32_t now) {
  // --- Handle USB Events with Debounce ---
  // Jika suspend bertahan > 2 detik tanpa diselingi resume (stabil)
  if (pending_suspend && (now - last_usb_suspend_ms > 2000)) {
      pending_suspend = false;
      displaySetBacklight(false);
      JsonDocument doc;
      doc["type"] = "cmd";
      doc["cmd"]["power"] = false;
      commsSendAmpCommand(doc);
  }

  // Jika resume bertahan > 1 detik tanpa diselingi suspend (stabil)
  if (pending_resume && (now - last_usb_resume_ms > 1000)) {
      pending_resume = false;
      displaySetBacklight(true);
      JsonDocument doc;
      doc["type"] = "cmd";
      doc["cmd"]["power"] = true;
      commsSendAmpCommand(doc);
  }

  // Service AMP UART
  while (Serial1.available()) {
    char c = static_cast<char>(Serial1.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (ampRxBuffer.length() > 0) {
        handleAmpFrame(ampRxBuffer);
        ampRxBuffer = "";
      }
    } else if (ampRxBuffer.length() < BRIDGE_MAX_FRAME - 1) {
      ampRxBuffer += c;
    }
  }

  // Service HOST USB
  // Karena fitur PC Detect sebelumnya mengandalkan OTG, pada Native USB CDC `bool(Serial)`
  // hanya bernilai true jika DTR (Data Terminal Ready) aktif / aplikasi Terminal terbuka.
  // Untuk mencegah amplifier mati tiba-tiba saat dicolokkan ke Charger biasa atau terminal ditutup,
  // kita nonaktifkan pengiriman auto power off dari Bridge ini.
  // Biarkan fitur PC Detect ditangani murni dari input hardware PC_DETECT_PIN di unit Amplifier secara mandiri.

  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (hostRxBuffer.length() > 0) {
        handleHostFrame(hostRxBuffer);
        hostRxBuffer = "";
      }
    } else if (hostRxBuffer.length() < BRIDGE_MAX_FRAME - 1) {
      hostRxBuffer += c;
    }
  }
}
