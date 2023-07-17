#include "config.h"

#include <iomanip>
#include <sstream>
#include <M5Unified.h>

#include "app/App.h"
#include "lib/network.h"
#include "lib/spiffs.h"
#include "lib/utils.h"

[[noreturn]] void halt() {
    while (true) { delay(1000); }
}

void App::setup() {
    auto cfg = m5::M5Unified::config();
    M5.begin(cfg);

    M5.Display.setRotation(3);
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(0, 0);

    if (!connectNetwork(WIFI_SSID, WIFI_PASS)) {
        Serial.println("ERROR: Failed to connect network. Rebooting...");
        delay(5000);
        ESP.restart();
        halt();
    }
    syncTime(TIMEZONE, NTP_SERVER);

#if defined(MQTT_ENABLE)
    _mqtt = std::make_unique<Mqtt>(
            MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID,
            spiffsLoadString(MQTT_CA_CERTIFICATE_PATH),
            spiffsLoadString(MQTT_CERTIFICATE_PATH),
            spiffsLoadString(MQTT_PRIVATE_KEY_PATH)
    );
    if (!_mqtt->connect()) {
        Serial.println("ERROR: Failed to connect to MQTT server. Rebooting...");
        delay(5000);
        ESP.restart();
        halt();
    }
#endif // defined(MQTT_ENABLE)

    // rxPin: Wi-SUN HAT rev 0.1 の場合は 36 にする
    _meter = std::make_unique<SmartMeter>(Serial2, 26, 0);
    if (!_meter->connect()) {
        Serial.println("ERROR: Failed to connect to the smart meter. Rebooting...");
        delay(5000);
        ESP.restart();
    }

    M5.Display.setBrightness(64);
}

void showDateTime() {
    struct tm now{};
    if (getLocalTime(&now)) {
        std::stringstream dt;
        dt << std::put_time(&now, "%m/%d %H:%M");
        M5.Display.setTextSize(3);
        M5.Display.setCursor(16, 8);
        M5.Display.println(dt.str().c_str());
    }
}

void showCurrent(uint32_t value) {
    if (value > 9999) {
        value = 9999;
    }
    M5.Display.setTextSize(7);
    M5.Display.setCursor(32, 40);
    M5.Display.printf("%4u", value);
    M5.Display.setTextSize(4);
    M5.Display.setCursor(208, 64);
    M5.Display.print("W");
}

void showIntegral(const String &value) {
    M5.Display.setTextSize(3);
    M5.Display.setCursor(20, 100);
    M5.Display.printf("%9s", value.c_str());
    M5.Display.setTextSize(2);
    M5.Display.setCursor(192, 108);
    M5.Display.print("kWh");
}

void App::loop() {
    auto power = _meter->getMeterValues();
    if (power == nullptr) {
        delay(1000);
        this->_failure++;
        // Restart when failed to get repeatedly
        if (this->_failure > 4) {
            Serial.println("err");
            Serial.flush();
            ESP.restart();
            halt();
        }
        Serial.println("Retrying");
        return;
    }
    this->_failure = 0;
    M5.Display.fillScreen(BLACK);
    showCurrent(power->getInstantaneous());
    showIntegral(power->getCumulativeStr());
    showDateTime();

#if defined(MQTT_ENABLE)
    DynamicJsonDocument message{128};
    message["timestamp"] = power->getTimestamp();
    message["instantaneous"] = power->getInstantaneous();
    message["cumulative"] = power->getCumulative();
    _mqtt->publish(MQTT_TOPIC, jsonEncode(message));
#endif // defined(MQTT_ENABLE)

    delay(MEASURE_INTERVAL_MS - millis() % MEASURE_INTERVAL_MS);
}
