/*
  Switch Bot SmartLock Opener

  2026/02/28 m.matsubara
  Code authored with assistance from OpenAI Codex.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <M5StickCPlus.h>
#include <time.h>
#include <esp_system.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>

#include "const.hpp"

// Use true for a quick first test. Replace with setCACert for better security.
static const bool USE_INSECURE_TLS = true;

// M5StickC Plus buttons (A=GPIO37, B=GPIO39)
static const int PIN_BTN_A = 37;
static const int PIN_BTN_B = 39;
static const bool BUTTON_ACTIVE_LOW = true;

// Behavior
static const uint32_t LONG_PRESS_MS = 2000;
static const uint32_t COOLDOWN_MS = 5000;

// ======= Helpers =======
static uint32_t last_action_ms = 0;
static uint32_t a_down_ms = 0;
static uint32_t b_down_ms = 0;

static const char* current_label = "* Locked *";
static uint16_t current_color = TFT_GREEN;
static uint32_t last_ui_ms = 0;

static const int TZ_OFFSET_HOURS = 9; // JST = UTC+9

static String formatTime(time_t t) {
  if (t < 1600000000) return String("--:--");
  t += TZ_OFFSET_HOURS * 3600;
  struct tm tm_info;
  gmtime_r(&t, &tm_info);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
  return String(buf);
}

static uint32_t error_until_ms = 0;

static void showStatus(bool force) {
  uint32_t now = millis();
  if (!force && now - last_ui_ms < 60000) return; // refresh once per minute
  last_ui_ms = now;

  M5.Lcd.fillScreen(TFT_BLACK);

  // Main lock state
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(current_color, TFT_BLACK);
  M5.Lcd.setCursor(10, 30);
  M5.Lcd.print(current_label);

  // Battery
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(10, 80);
  float vbat = M5.Axp.GetBatVoltage();
  char vbuf[20];
  snprintf(vbuf, sizeof(vbuf), "Battery: %.2fv", vbat);
  M5.Lcd.print(vbuf);

  // Time (JST)
  M5.Lcd.setCursor(10, 105);
  M5.Lcd.print("JST ");
  M5.Lcd.print(formatTime(time(nullptr)));

  // Error overlay (shown for a few seconds)
  if (error_until_ms && now < error_until_ms) {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Lcd.setCursor(10, 5);
    M5.Lcd.print("API ERROR");
  }
}

static void waitForTimeSync() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = 0;
  for (int i = 0; i < 30; ++i) {
    time(&now);
    if (now > 1600000000) { // 2020-09-13
      return;
    }
    delay(500);
  }
}

static String makeUuidV4() {
  uint8_t b[16];
  esp_fill_random(b, sizeof(b));
  b[6] = (b[6] & 0x0F) | 0x40; // version 4
  b[8] = (b[8] & 0x3F) | 0x80; // variant 1
  char out[37];
  snprintf(out, sizeof(out),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11],
           b[12], b[13], b[14], b[15]);
  return String(out);
}

static String hmacSha256Base64Upper(const String& payload, const char* secret) {
  uint8_t hmac[32];
  size_t olen = 0;

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, reinterpret_cast<const unsigned char*>(secret), strlen(secret));
  mbedtls_md_hmac_update(&ctx, reinterpret_cast<const unsigned char*>(payload.c_str()), payload.length());
  mbedtls_md_hmac_finish(&ctx, hmac);
  mbedtls_md_free(&ctx);

  unsigned char b64[64] = {0};
  mbedtls_base64_encode(b64, sizeof(b64), &olen, hmac, sizeof(hmac));

  String out = String(reinterpret_cast<char*>(b64));
  out.toUpperCase();
  return out;
}

static bool sendCommand(const char* command) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected.");
    return false;
  }

  time_t now = time(nullptr);
  if (now < 1600000000) {
    Serial.println("Time not synced.");
    return false;
  }

  String nonce = makeUuidV4();
  uint64_t t_ms = static_cast<uint64_t>(now) * 1000ULL;
  String t = String(t_ms);
  String signPayload = String(SWITCHBOT_TOKEN) + t + nonce;
  String sign = hmacSha256Base64Upper(signPayload, SWITCHBOT_SECRET);

  String url = String("https://api.switch-bot.com/v1.1/devices/") +
               SWITCHBOT_DEVICE_ID + "/commands";

  WiFiClientSecure client;
  if (USE_INSECURE_TLS) {
    client.setInsecure();
  } else {
    // TODO: Replace with current CA cert for api.switch-bot.com
    // client.setCACert(your_root_ca_pem);
  }

  HTTPClient https;
  if (!https.begin(client, url)) {
    Serial.println("HTTPS begin failed.");
    return false;
  }

  https.addHeader("Content-Type", "application/json; charset=utf8");
  https.addHeader("Authorization", SWITCHBOT_TOKEN);
  https.addHeader("sign", sign);
  https.addHeader("t", t);
  https.addHeader("nonce", nonce);

  String body = String("{\"command\":\"") + command +
                "\",\"parameter\":\"default\",\"commandType\":\"command\"}";

  int httpCode = https.POST(body);
  String resp = https.getString();
  https.end();

  Serial.printf("HTTP %d, resp: %s\n", httpCode, resp.c_str());
  return httpCode == 200;
}

static bool isPressed(int pin) {
  int v = digitalRead(pin);
  return BUTTON_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  M5.begin();
  M5.Lcd.setRotation(1);
  M5.Axp.ScreenBreath(40);  // 0..100, smaller is dimmer
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print("WiFi connecting");

  pinMode(PIN_BTN_A, INPUT);
  pinMode(PIN_BTN_B, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  current_label = "* Locked *";
  current_color = TFT_GREEN;
  showStatus(true);

  waitForTimeSync();
  Serial.println("Time sync done.");
}

void loop() {
  uint32_t now = millis();

  bool aPressed = isPressed(PIN_BTN_A);
  bool bPressed = isPressed(PIN_BTN_B);

  if (aPressed && a_down_ms == 0) a_down_ms = now;
  if (!aPressed) a_down_ms = 0;

  if (bPressed && b_down_ms == 0) b_down_ms = now;
  if (!bPressed) b_down_ms = 0;

  if (a_down_ms && (now - a_down_ms) >= LONG_PRESS_MS) {
    if (now - last_action_ms >= COOLDOWN_MS) {
      Serial.println("Unlock command");
      if (sendCommand("unlock")) {
        current_label = "* Unlocked *";
        current_color = TFT_RED;
        showStatus(true);
        last_action_ms = now;
      } else {
        error_until_ms = now + 3000;
        showStatus(true);
      }
    }
    a_down_ms = 0;
  }

  if (b_down_ms && (now - b_down_ms) >= LONG_PRESS_MS) {
    if (now - last_action_ms >= COOLDOWN_MS) {
      Serial.println("Lock command");
      if (sendCommand("lock")) {
        current_label = "* Locked *";
        current_color = TFT_GREEN;
        showStatus(true);
        last_action_ms = now;
      } else {
        error_until_ms = now + 3000;
        showStatus(true);
      }
    }
    b_down_ms = 0;
  }

  showStatus(false);
  delay(10);
}



