#include <iomanip>
#include <map>
#include <sstream>
#include <M5Unified.h>

#include "lib/wisun/BP35A.h"
#include "lib/utils.h"

/**
 * Connect to smart meter
 *
 * @return true:success, false:failure
 */
bool BP35A::connect(const String &brouteId, const String &broutePassword) {
    delay(2000);
    _discardBuffer();

    M5.Display.setTextSize(2);
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Initializing");

    // Disable echo back
    M5.Display.printf(".");
    _sendCommand("SKSREG SFE 0");
    if (!_waitResponse("OK", 5000)) {
        return false;
    }

    // Check information
    M5.Display.printf(".");
    _sendCommand("SKINFO");
    if (!_waitResponse("OK", 5000)) {
        return false;
    }
    _sendCommand("SKVER");
    if (!_waitResponse("OK", 5000)) {
        return false;
    }
    _sendCommand("SKAPPVER");
    if (!_waitResponse("OK", 5000)) {
        return false;
    }

    // Configure option
    _sendCommand("ROPT");
    auto roptResult = _readLine(5000);
    if (roptResult == nullptr || !roptResult->startsWith("OK ")) {
        return false;
    }
    if (roptResult->substring(3) == "00") {
        _sendCommand("WOPT 01"); // 何度も実行すると寿命が縮まるので注意!
        if (!_waitResponse("OK", 5000)) {
            return false;
        }
    }

    // Set B-route ID/password
    M5.Display.printf(".");
    _sendCommand("SKSETPWD C " + broutePassword);
    if (!_waitResponse("OK", 5000)) {
        return false;
    }
    _sendCommand("SKSETRBID " + brouteId);
    if (!_waitResponse("OK", 5000)) {
        return false;
    }

    M5.Display.println("OK");
    M5.Display.printf("Scanning");

    // Scan
    for (int dur = 3; dur <= 6; dur++) {
        M5.Display.printf(".");
        _sendCommand("SKSCAN 3 FFFFFFFF " + (String) dur);
        if (!_waitResponse("OK", 5000)) {
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
    auto ipv6Addr = _readLine(5000);
    if (ipv6Addr == nullptr) {
        return false;
    }
    _meter->ipv6Addr = *ipv6Addr;

    // Register channel
    M5.Display.printf(".");
    _sendCommand("SKSREG S2 " + _meter->channel);
    if (!_waitResponse("OK", 5000)) {
        return false;
    }

    // Register Pan ID
    M5.Display.printf(".");
    _sendCommand("SKSREG S3 " + _meter->panId);
    if (!_waitResponse("OK", 5000)) {
        return false;
    }

    // Join
    M5.Display.printf(".");
    _sendCommand("SKJOIN " + _meter->ipv6Addr);
    if (!_waitResponse("OK", 5000)) {
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
 * Discard buffer
 */
void BP35A::_discardBuffer() {
    delay(500);
    while (_serial.available()) {
        _serial.read();
    }
    _serial.print("\r\n");
    delay(1000);
    while (_serial.available()) {
        _serial.read();
    }
}

/**
 * Send command
 *
 * @param data command string
 */
void BP35A::_sendCommand(const String &data) {
    Serial.println("> " + data);
    _serial.print(data + "\r\n");
}

/**
 * Wait response
 *
 * @return true:received, false:error or timeout
 */
bool BP35A::_waitResponse(const char *expect, int timeout) {
    auto start = millis();
    while (millis() - start < timeout) {
        auto line = _readLine(timeout - (int) (millis() - start));
        if (line == nullptr || line->startsWith("FAIL ER")) {
            return false;
        } else if (line->startsWith(expect)) {
            return true;
        }
    }
    return false;
}

/**
 * Read response
 *
 * @param timeout timeout (milliseconds)
 * @return response string (nullptr:timeout)
 */
std::unique_ptr<String> BP35A::_readLine(int timeout) {
    std::stringstream ss;
    auto start = millis();
    while (millis() - start < timeout) {
        auto s = _serial.readStringUntil('\n');
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
 * @param dataLen ECHONET Lite data length
 * @param timeout timeout (milliseconds)
 */
bool BP35A::sendData(const uint8_t *data, size_t dataLen, int timeout) {
    Serial.printf("> SKSENDTO 1 %s 0E1A 1 %04X ", _meter->ipv6Addr.c_str(), dataLen);
    _serial.printf("SKSENDTO 1 %s 0E1A 1 %04X ", _meter->ipv6Addr.c_str(), dataLen);
    for (int i = 0; i < dataLen; i++) {
        Serial.printf("%02X", *(data + i));
        _serial.write(*(data + i));
    }
    Serial.println();
    _serial.print("\r\n");
    return _waitResponse("OK", timeout);
}

/**
 * Receive ECHONET Lite data
 *
 * @param timeout timeout (milliseconds)
 * @return ECHONET Lite data (nullptr:failure)
 */
std::unique_ptr<std::vector<uint8_t>> BP35A::receiveData(int timeout) {
    auto result = std::make_unique<std::vector<uint8_t>>();
    auto start = millis();
    while (millis() - start < timeout) {
        auto line = _readLine(timeout - (int) (millis() - start));
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
std::unique_ptr<BP35AMeterEntry> BP35A::_getScanResult(int timeout) {
    String channel;
    String panId;
    String addr;

    auto start = millis();
    while (millis() - start < timeout) {
        auto line = _readLine(timeout - (int) (millis() - start));
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
            return std::make_unique<BP35AMeterEntry>(addr, panId, channel);
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
bool BP35A::_getJoinResult(int timeout) {
    auto start = millis();
    while (millis() - start < timeout) {
        auto line = _readLine(timeout - (int) (millis() - start));
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
