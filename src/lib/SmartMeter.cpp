#include "config.h"

#include <sstream>
#include <M5Unified.h>

#include "lib/SmartMeter.h"
#include "lib/utils.h"

/**
 * Connect to smart meter
 *
 * @return true:success, false:failure
 */
bool SmartMeter::connect() {
    _bp35a1.begin(115200, SERIAL_8N1, _rxPin, _txPin);
    delay(2000);
    _discardBuffer();

    M5.Display.setTextSize(2);
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Initializing");

    // Disable echo back
    M5.Display.printf(".");
    _sendCommand("SKSREG SFE 0");
    if (!_waitResponse("OK")) {
        return false;
    }

    // Check information
    M5.Display.printf(".");
    _sendCommand("SKINFO");
    if (!_waitResponse("OK")) {
        return false;
    }
    _sendCommand("SKVER");
    if (!_waitResponse("OK")) {
        return false;
    }
    _sendCommand("SKAPPVER");
    if (!_waitResponse("OK")) {
        return false;
    }

    // Configure option
    _sendCommand("ROPT");
    auto roptResult = _readLine();
    if (roptResult == nullptr || !roptResult->startsWith("OK ")) {
        return false;
    }
    if (roptResult->substring(3) == "00") {
        _sendCommand("WOPT 01"); // 何度も実行すると寿命が縮まるので注意!
        if (!_waitResponse("OK")) {
            return false;
        }
    }

    // Set B-route ID/password
    M5.Display.printf(".");
    _sendCommand("SKSETPWD C " + String(BROUTE_PASSWORD));
    if (!_waitResponse("OK")) {
        return false;
    }
    _sendCommand("SKSETRBID " + String(BROUTE_ID));
    if (!_waitResponse("OK")) {
        return false;
    }

    M5.Display.println("OK");
    M5.Display.printf("Scanning");

    // Scan
    for (int dur = 3; dur <= 6; dur++) {
        M5.Display.printf(".");
        _sendCommand("SKSCAN 3 FFFFFFFF " + (String) dur);
        if (!_waitResponse("OK")) {
            return false;
        }
        _meter = _getScanResult((1 << dur) * 1000);
        if (_meter != nullptr) {
            break;
        }
        if (dur == 6) {
            return false;
        }
        delay(1000);
    }

    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("Addr: " + _meter->addr + ", Channel: " + _meter->channel + ", Pan ID: " + _meter->panId);
    Serial.println("Addr: " + _meter->addr + ", Channel: " + _meter->channel + ", Pan ID: " + _meter->panId);

    M5.Display.printf("Connecting");

    // Convert MAC to IPv6 address
    M5.Display.printf(".");
    _sendCommand("SKLL64 " + _meter->addr);
    auto ipv6Addr = _readLine();
    if (ipv6Addr == nullptr) {
        return false;
    }
    _meter->ipv6Addr = *ipv6Addr;

    // Register channel
    M5.Display.printf(".");
    _sendCommand("SKSREG S2 " + _meter->channel);
    if (!_waitResponse("OK")) {
        return false;
    }

    // Register Pan ID
    M5.Display.printf(".");
    _sendCommand("SKSREG S3 " + _meter->panId);
    if (!_waitResponse("OK")) {
        return false;
    }

    // Join
    M5.Display.printf(".");
    _sendCommand("SKJOIN " + _meter->ipv6Addr);
    if (!_waitResponse("OK")) {
        return false;
    }
    if (!_getJoinResult(30000)) {
        M5.Display.println("failed");
        return false;
    }

    M5.Display.println("OK");
    return true;
}

/**
 * 瞬時電力計測値を取得
 *
 * @return 瞬時電力計測値
 */
std::unique_ptr<MeterValues> SmartMeter::getMeterValues() {
    time_t timestamp;
    static unsigned char sendBuf[] = {
            0x10,  // EHD1
            0x81,  // EHD2
            0x00, 0x01,  // TID
            0x05, 0xff, 0x01,  // SEOJ: Controller
            0x02, 0x88, 0x01,  // DEOJ: Smart meter
            0x62,  // ESV: Get
            0x03,  // OPC
            0xe7, 0x00,  // 瞬時電力計測値(W)
            0xe0, 0x00,  // 積算電力量計測値
            0xe1, 0x00,  // 積算電力量単位
    };

    time(&timestamp);
    _sendData(sendBuf, sizeof(sendBuf));
    if (!_waitResponse("OK")) {
        return nullptr;
    }
    auto data = _recvData(_readTimeout);
    if (data == nullptr || data->size() < 12) {
        return nullptr;
    }
    int idx = 11;
    int opc = (int) data->at(idx++); // OPC
    bool hasInstantaneous = false;
    bool hasCumulative = false;
    bool hasCumulativeUnit = false;
    uint32_t instantaneous = 0, cumulative = 0;
    int cumulativeUnit = 0;
    for (int i = 0; i < opc; i++) {
        auto epc = data->at(idx++); // EPC
        auto pdc = (int) data->at(idx++); // PDC
        if (epc == 0xe7 && pdc == 4) {
            // 瞬時電力計測値
            // Measured instantaneous electric energy
            hasInstantaneous = true;
            for (int j = 0; j < 4; j++) {
                instantaneous <<= 8;
                instantaneous |= data->at(idx++);
            }
        } else if (epc == 0xe0 && pdc == 4) {
            // 積算電力量計測値 (正方向計測値)
            // Measured cumulative amount of electric energy (normal direction))
            hasCumulative = true;
            for (int j = 0; j < 4; j++) {
                cumulative <<= 8;
                cumulative |= data->at(idx++);
            }
        } else if (epc == 0xe1 && pdc == 1) {
            // 積算電力量単位 (正方向、逆方向計測値)
            // Unit for cumulative amounts of electric energy (normal and reverse directions)
            hasCumulativeUnit = true;
            cumulativeUnit = (int) data->at(idx++);
        } else {
            idx += pdc;
        }
    }
    if (hasInstantaneous && hasCumulative && hasCumulativeUnit) {
        int cumulativePow = 0;
        if (cumulativeUnit > 0 && cumulativeUnit <= 4) {
            cumulativePow = -cumulativeUnit;
        } else if (cumulativeUnit >= 10 && cumulativeUnit <= 13) {
            cumulativePow = cumulativeUnit - 9;
        }
        return std::make_unique<MeterValues>(timestamp, instantaneous, cumulative, cumulativePow);
    }
    return nullptr;
}

/**
 * Discard buffer
 */
void SmartMeter::_discardBuffer() {
    delay(500);
    while (_bp35a1.available()) {
        _bp35a1.read();
    }
    _bp35a1.print("\r\n");
    delay(1000);
    while (_bp35a1.available()) {
        _bp35a1.read();
    }
}

/**
 * Send command
 *
 * @param data command string
 */
void SmartMeter::_sendCommand(const String &data) {
    Serial.println("> " + data);
    _bp35a1.print(data + "\r\n");
}

/**
 * Wait response
 *
 * @return true:received, false:error or timeout
 */
bool SmartMeter::_waitResponse(const char *expect) {
    while (true) {
        auto line = _readLine();
        if (line == nullptr || line->startsWith("FAIL ER")) {
            return false;
        } else if (line->startsWith(expect)) {
            return true;
        }
    }
}

/**
 * Read response
 *
 * @return response string (nullptr:timeout)
 */
std::unique_ptr<String> SmartMeter::_readLine() {
    auto start = millis();
    std::stringstream ss;
    while (millis() - start < _readTimeout) {
        auto s = _bp35a1.readStringUntil('\n');
        if (s.isEmpty()) {
            continue;
        }
        ss << s.c_str();
        auto line = ss.str();
        if (line.at(line.length() - 1) == '\r') {
            line.pop_back();
            Serial.printf("< %s\n", line.c_str());
            return std::make_unique<String>(line.c_str());
        }
    }
    return nullptr;
}

/**
 * Send ECHONET Lite data
 *
 * @param data ECHONET Lite data
 * @param len ECHONET Lite data length
 */
void SmartMeter::_sendData(const uint8_t *data, int len) {
    Serial.printf("> SKSENDTO 1 %s 0E1A 1 %04X ", _meter->ipv6Addr.c_str(), len);
    _bp35a1.printf("SKSENDTO 1 %s 0E1A 1 %04X ", _meter->ipv6Addr.c_str(), len);
    for (int i = 0; i < len; i++) {
        Serial.printf("%02X", *(data + i));
        _bp35a1.write(*(data + i));
    }
    Serial.println();
    _bp35a1.print("\r\n");
}

/**
 * Receive ECHONET Lite data
 *
 * @return ECHONET Lite data (nullptr:failure)
 */
std::unique_ptr<std::vector<uint8_t>> SmartMeter::_recvData(int timeout) {
    auto result = std::make_unique<std::vector<uint8_t>>();
    auto start = millis();
    while (millis() - start < timeout) {
        auto line = _readLine();
        if (line == nullptr) {
            continue;
        } else if (line->startsWith("ERXUDP ")) {
            auto tokens = splitString(line->c_str(), " ");
            auto data = tokens[8];
            for (int i = 0; i < data.length(); i += 2) {
                auto parsed = (uint8_t) strtoul(data.substr(i, 2).c_str(), nullptr, 16);
                result->push_back(parsed);
            }
            return result;
        }
    }
    return nullptr;
}

/**
 * Receive scan result
 *
 * @param timeout timeout (ms)
 * @return Scan result (nullptr:no entry or timeout)
 */
std::unique_ptr<ScanEntry> SmartMeter::_getScanResult(int timeout) {
    String channel;
    String panId;
    String addr;

    auto start = millis();
    while (millis() - start < timeout) {
        auto line = _readLine();
        if (line == nullptr) {
            continue;
        } else if (line->indexOf("Channel:") >= 0) {
            channel = line->substring(line->indexOf("Channel:") + 8);
        } else if (line->indexOf("Pan ID:") >= 0) {
            panId = line->substring(line->indexOf("Pan ID:") + 7);
        } else if (line->indexOf("Addr:") >= 0) {
            addr = line->substring(line->indexOf("Addr:") + 5);
        } else if (line->startsWith("EVENT 22 ")) {
            if (addr.isEmpty() || panId.isEmpty() || channel.isEmpty()) {
                return nullptr;
            }
            return std::make_unique<ScanEntry>(addr, panId, channel);
        }
    }
    _discardBuffer();
    return nullptr;
}

/**
 * Wait join result
 *
 * @return true:success, false:failure or timeout
 */
bool SmartMeter::_getJoinResult(int timeout) {
    auto start = millis();
    while (millis() - start < timeout) {
        auto line = _readLine();
        if (line == nullptr) {
            continue;
        }
        if (line->indexOf("EVENT 24") >= 0) {
            return false;
        } else if (line->indexOf("EVENT 25") >= 0) {
            return true;
        }
    }
    _discardBuffer();
    return false;
}
