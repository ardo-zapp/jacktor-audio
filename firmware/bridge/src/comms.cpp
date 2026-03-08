#include "comms.h"
#include "config.h"
#include "display.h"

String hostRxBuffer;
String ampRxBuffer;
String lastAmpTelemetry;

static bool pcAsleep = false;
static uint32_t lastPcActiveMs = 0;

void commsInit() {
  Serial.begin(HOST_SERIAL_BAUD); // USB CDC Native
  Serial1.begin(AMP_SERIAL_BAUD, SERIAL_8N1, PIN_UART_AMP_RX, PIN_UART_AMP_TX);

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
  // Native USB ESP32-S3 memberikan dtr() & rts() dari koneksi port.
  // Jika port ditutup (atau PC sleep/tercabut), bool(Serial) akan bernilai false
  if (Serial) {
    lastPcActiveMs = now;
    if (pcAsleep) {
      pcAsleep = false;
      // Beritahu amplifier bahwa PC sudah ON via power wake (opsional),
      // tetapi amplifier Jacktor punya PC Detect opto di HW nya sendiri.
      // Kita asumsikan deteksi ini sinkron juga dari bridge.
      JsonDocument doc;
      JsonObject root = doc.to<JsonObject>();
      root["type"] = "cmd";
      root["cmd"]["power"] = true;
      commsSendAmpCommand(doc);
    }
  } else {
    // Jika tidak ada koneksi CDC terhubung selama timeout, anggap Sleep/Mati
    if (!pcAsleep && (now - lastPcActiveMs >= PC_SLEEP_TIMEOUT_MS)) {
      pcAsleep = true;
      // Kirim auto-off command ke Amp
      JsonDocument doc;
      JsonObject root = doc.to<JsonObject>();
      root["type"] = "cmd";
      root["cmd"]["power"] = false;
      commsSendAmpCommand(doc);
    }
  }

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
