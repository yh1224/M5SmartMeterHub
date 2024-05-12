#include "config.h"

#include <iomanip>
#include <sstream>
#include <M5Unified.h>

#include "app/AppMeter.h"
#include "lib/spiffs.h"
#include "lib/utils.h"
#include "lib/wisun/BP35A.h"
#include "lib/wisun/BP35C.h"

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

/**
 * Toggle display mode
 */
void AppMeter::toggleDisplayMode() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _displayMode = static_cast<DisplayMode>((static_cast<int>(_displayMode) + 1) % DISPLAY_MODE_MAX);
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

void showDateTime(time_t now) {
    struct tm tm{};
    if (localtime_r(&now, &tm)) {
        std::stringstream dt;
        dt << std::put_time(&tm, "%Y/%m/%d %H:%M");
        M5.Display.setTextSize(2);
        M5.Display.setCursor(8, 8);
        M5.Display.println(dt.str().c_str());
    }
}

void showCurrent(uint32_t value) {
    if (value > 9999) {
        value = 9999;
    }
    M5.Display.setTextSize(3);
    M5.Display.setCursor(144, 32);
    M5.Display.printf("%4u", value);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(220, 40);
    M5.Display.print("W");
}

void showIntegral(double value) {
    M5.Display.setTextSize(3);
    M5.Display.setCursor(8, 32);
    M5.Display.printf("%4.1f", value);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(84, 40);
    M5.Display.print("kWh");
}

void showCost(int value) {
    M5.Display.setTextSize(3);
    M5.Display.setCursor(8, 32);
    M5.Display.printf("%4d", value);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(84, 40);
    M5.Display.print("Yen");
}

void showHistory(const std::vector<MeterValue> &history, int sx, int sy, int width, int height) {
    int nX = 24 * 2 * 2; // 48H
    int w = static_cast<int>(width / nX);

    std::vector<double> deltas;
    int start = std::max(0, (int) history.size() - nX - 1);
    double prev = *history[start].getCumulative();
    for (int i = start + 1; i < history.size(); i++) {
        double v = *history[i].getCumulative();
        deltas.push_back(v - prev);
        prev = v;
    }
    double maxDelta = *std::max_element(deltas.begin(), deltas.end());

    M5Canvas canvas(&M5.Display);
    canvas.createSprite(width, height);
    for (int i = 0; i < deltas.size(); i++) {
        auto y = static_cast<int>((deltas[i] / maxDelta) * (double) height);
        canvas.fillRect(i * w, height - y, w, y, TFT_WHITE);
    }
    M5.Display.startWrite();
    canvas.pushSprite(sx, sy);
    M5.Display.endWrite();
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

    std::unique_ptr<WiSUN> wisun;
    if (strcmp(WISUN_MODULE, "BP35C") == 0) {
        wisun = std::make_unique<BP35C>(Serial2, 36, 0);
    } else {
        // rxPin: Wi-SUN HAT rev 0.1 の場合は 36 にする
        wisun = std::make_unique<BP35A>(Serial2, 26, 0);
    }
    _smartMeter = std::make_unique<SmartMeterClient>(std::move(wisun), BROUTE_ID, BROUTE_PASSWORD);
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
    auto instantaneous = _measured->getInstantaneous();
    if (instantaneous != nullptr) {
        showCurrent(*instantaneous);
    }
    auto usage = _getUsageToday();
    if (usage != nullptr) {
        if (_displayMode == DISPLAY_MODE_COST) {
            showCost(static_cast<int>(*usage * PRICE_YEN_PER_KWH));
        } else {
            showIntegral(*usage);
        }
    }
    if (!_meterHistory.empty()) {
        showHistory(_meterHistory, 24, 80, 232, 40);
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
 * Get today's usage (kWh)
 */
std::unique_ptr<double> AppMeter::_getUsageToday() {
    auto now = time(nullptr);
    struct tm tm{};
    if (!localtime_r(&now, &tm)) {
        return nullptr;
    }
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    time_t timestamp = mktime(&tm);
    auto begin = _getHistory(timestamp);
    if (_measured == nullptr || _measured->getCumulative() == nullptr ||
        begin == nullptr || begin->getCumulative() == nullptr) {
        return nullptr;
    }
    auto kwh = *_measured->getCumulative() - *begin->getCumulative();
    return std::make_unique<double>(kwh);
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

        xSemaphoreTake(_lock, portMAX_DELAY);
        _measured = std::move(measured);
        xSemaphoreGive(_lock);
        _lastMeasureTime = now;

#if defined(MQTT_ENABLE)
        _publishMeasured();
#endif // defined(MQTT_ENABLE)

        return true;
    }
    return false;
}

/**
 * Update history
 */
bool AppMeter::_updateHistory() {
    auto now = time(nullptr);
    bool needPublish = false;

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
#if 1 // DEBUG: Log _meterHistory
        for (const auto &v: _meterHistory) {
            auto t = v.getTimestamp();
            struct tm tm{};
            if (!localtime_r(&t, &tm)) {
                continue;
            }
            std::stringstream ss;
            ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            Serial.printf("%s: %d\n", ss.str().c_str(), *v.getCumulative());
        }
#endif
        needPublish = true;
    } else if (!_meterHistory.empty() && _lastHistoryPublishTime + HISTORY_INTERVAL < now) {
        needPublish = true;
    }

    if (needPublish) {
#if defined(MQTT_ENABLE)
        _publishHistory();
#endif // defined(MQTT_ENABLE)
        _lastHistoryPublishTime = now;
        return true;
    }

    return false;
}

/**
 * Publish measured data
 */
void AppMeter::_publishMeasured() {
    DynamicJsonDocument message{128};
    message["timestamp"] = _measured->getTimestamp();
    message["instantaneous"] = *_measured->getInstantaneous();
    message["cumulative"] = *_measured->getCumulative();
#if !defined(MQTT_TOPIC_MEASURED) && defined(MQTT_TOPIC)
#define MQTT_TOPIC_MEASURED MQTT_TOPIC
#endif
    _mqtt->publish(MQTT_TOPIC_MEASURED, jsonEncode(message));
}

void AppMeter::_publishHistory() {
    DynamicJsonDocument message{8192};
    auto arr = message.to<JsonArray>();
    for (const auto &v: _meterHistory) {
        auto obj = arr.add();
        obj["timestamp"] = v.getTimestamp();
        obj["cumulative"] = *v.getCumulative();
    }
    _mqtt->publish(MQTT_TOPIC_HISTORY, jsonEncode(message));
}
