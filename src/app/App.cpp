#include "config.h"

#include <M5Unified.h>

#include "app/App.h"
#include "lib/network.h"

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
    }
    syncTime(TIMEZONE, NTP_SERVER);

    M5.Display.setBrightness(64);

    _meter->setup();
    _server->setup();
}

void App::loop() {
    M5.update();
    if (M5.BtnA.wasPressed()) _onButtonA();

    _server->loop();

    delay(50);
}

void App::_onButtonA() {
    _meter->toggleDisplayMode();
}
