#include "config.h"

#include <iomanip>
#include <sstream>
#include <M5Unified.h>

#include "app/AppMeter.h"
#include "lib/spiffs.h"
#include "lib/utils.h"

void AppMeter::setup() {
    xTaskCreatePinnedToCore(
            [](void *arg) {
                auto *self = (AppMeter *) arg;
                self->_setup();
#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
                while (true) {
                    self->_loop();
                }
#pragma clang diagnostic pop
            },
            "AppMeter",
            8192,
            this,
            1,
            &_taskHandle,
            APP_CPU_NUM
    );
}

void showDateTime(time_t now) {
    struct tm tm{};
    if (localtime_r(&now, &tm)) {
        std::stringstream dt;
        dt << std::put_time(&tm, "%m/%d %H:%M");
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

void showCost(int value) {
    M5.Display.setTextSize(3);
    M5.Display.setCursor(20, 100);
    M5.Display.printf("%9d", value);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(192, 108);
    M5.Display.print("Yen");
}

/**
 * Toggle display mode
 */
void AppMeter::toggleDisplayMode() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _displayMode = (_displayMode + 1) % 2;
    _updateDisplay();
    xSemaphoreGive(_lock);
}

std::unique_ptr<MeterValue> AppMeter::getLatest() {
    std::unique_ptr<MeterValue> ret = nullptr;
    xSemaphoreTake(_lock, portMAX_DELAY);
    if (_measured != nullptr) {
        ret = std::make_unique<MeterValue>(*_measured);
    }
    xSemaphoreGive(_lock);
    return ret;
}

std::vector<MeterValue> AppMeter::getHistory() {
    std::vector<MeterValue> ret;
    xSemaphoreTake(_lock, portMAX_DELAY);
    std::copy(_meterHistory.begin(), _meterHistory.end(), std::back_inserter(ret));
    xSemaphoreGive(_lock);
    return ret;
}

void AppMeter::_setup() {
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
    }
#endif // defined(MQTT_ENABLE)

    // rxPin: Wi-SUN HAT rev 0.1 の場合は 36 にする
    _smartMeter = std::make_unique<SmartMeterClient>(Serial2, 26, 0, BROUTE_ID, BROUTE_PASSWORD);
    if (!_smartMeter->connect()) {
        Serial.println("ERROR: Failed to connect to the smart meter. Rebooting...");
        delay(5000);
        ESP.restart();
    }
}

void AppMeter::_loop() {
    auto changed = _measure() || _updateHistory();
    if (changed) {
        xSemaphoreTake(_lock, portMAX_DELAY);
        _updateDisplay();
        xSemaphoreGive(_lock);
    }
    delay(200);
}

/**
 * Update display
 */
void AppMeter::_updateDisplay() {
    M5.Display.fillScreen(BLACK);
    showDateTime(_measured->getTimestamp());
    showCurrent(_measured->getInstantaneous());
    if (_displayMode == 1) {
        auto cost = _getCostToday();
        if (cost != nullptr) {
            showCost(*cost);
        }
    } else {
        showIntegral(_measured->getCumulativeStr());
    }
}

/**
 * Get history
 */
std::unique_ptr<MeterValue> AppMeter::_getHistory(time_t timestamp) {
    for (const auto &h: _meterHistory) {
        if (h.getTimestamp() == timestamp) {
            return std::make_unique<MeterValue>(h);
        }
    }
    return nullptr;
}

/**
 * Get today's cost (yen)
 */
std::unique_ptr<int> AppMeter::_getCostToday() {
    auto now = time(nullptr);
    struct tm tm{};
    if (!localtime_r(&now, &tm)) {
        return nullptr;
    }
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    time_t timestamp = mktime(&tm);
    auto begin = _getHistory(timestamp);
    if (_measured == nullptr || begin == nullptr) {
        return nullptr;
    }
    auto kwh = _measured->getCumulative() - begin->getCumulative();
    return std::make_unique<int>(kwh * PRICE_YEN_PER_KWH);
}

/**
 * Measure
 */
bool AppMeter::_measure() {
    auto now = time(nullptr);
    if (_lastMeasureTime == 0 || _lastMeasureTime + MEASURE_INTERVAL < now) {
        auto measured = _smartMeter->getMeterValue();
        if (measured == nullptr) {
            delay(1000);
            this->_failure++;
            // Restart when failed to get repeatedly
            if (this->_failure > 4) {
                Serial.println("err");
                Serial.flush();
                ESP.restart();
            }
            Serial.println("Retrying");
            return true;
        }
        this->_failure = 0;

#if defined(MQTT_ENABLE)
        DynamicJsonDocument message{128};
        message["timestamp"] = measured->getTimestamp();
        message["instantaneous"] = measured->getInstantaneous();
        message["cumulative"] = measured->getCumulative();
        _mqtt->publish(MQTT_TOPIC, jsonEncode(message));
#endif // defined(MQTT_ENABLE)

        xSemaphoreTake(_lock, portMAX_DELAY);
        _measured = std::move(measured);
        xSemaphoreGive(_lock);
        _lastMeasureTime = now;

        return true;
    }
    return false;
}

/**
 * Update history
 */
bool AppMeter::_updateHistory() {
    auto now = time(nullptr);
    // get history for last 3 days
    if (_lastHistoryTime == 0 || _lastHistoryTime + 35 * 60 < now) {
        for (int i = _lastHistoryTime == 0 ? 3 : 0; i >= 0; i--) {
            auto history = _smartMeter->getMeterHistory(i);
            if (history != nullptr) {
                for (const auto &v: *history) {
                    if (_lastHistoryTime < v.getTimestamp()) {
                        _meterHistory.push_back(v);
                        _lastHistoryTime = v.getTimestamp();
                        if (_meterHistory.size() > 48 * 3) {  // max: 3 days
                            _meterHistory.erase(_meterHistory.begin());
                        }
                    }
                }
            }
        }
#if 1 // DEBUG: Log _meter
        for (const auto &v: _meterHistory) {
            auto t = v.getTimestamp();
            struct tm tm{};
            if (!localtime_r(&t, &tm)) {
                continue;
            }
            std::stringstream ss;
            ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            Serial.printf("%s: %s\n", ss.str().c_str(), v.getCumulativeStr().c_str());
        }
        return true;
#endif
    }
    return false;
}
