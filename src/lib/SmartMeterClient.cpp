#include <iomanip>
#include <map>
#include <sstream>
#include <M5Unified.h>

#include "lib/SmartMeterClient.h"
#include "lib/utils.h"

/**
 * Connect to smart meter
 *
 * @return true:success, false:failure
 */
bool SmartMeterClient::connect() {
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
    _sendCommand("SKSETPWD C " + _broutePassword);
    if (!_waitResponse("OK", 5000)) {
        return false;
    }
    _sendCommand("SKSETRBID " + _brouteId);
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

    // Get Cumulative unit
    _cumulativePow = _getMeterCumulativePow();
    if (_cumulativePow == nullptr) {
        return false;
    }

    M5.Display.println("OK");
    return true;
}

/**
 * 積算電力量計測値履歴
 *
 * @param day 履歴日 (0:当日 / n:n日前)
 * @return 積算電力量計測値履歴
 */
std::unique_ptr<std::vector<MeterValue>> SmartMeterClient::getMeterHistory(int day) {
    struct tm tm{};
    if (!getLocalTime(&tm)) {
        return nullptr;
    }
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    tm.tm_mday -= day;
    time_t timestamp = mktime(&tm);

    // 積算電力量計測値履歴(1 日単位)取得
    std::vector<uint8_t> v = {(uint8_t) day};
    std::map<uint8_t, std::vector<uint8_t>> setProps = {
            {0xe5, v},  // 積算履歴収集日1
    };
    _setProperty(setProps);
    std::vector<uint8_t> getProps = {
            0xe2,  // 積算電力量計測値履歴1(正方向計測値)
    };
    auto getRes = _getProperty(getProps);
    if (getRes == nullptr) {
        return nullptr;
    }
    if (!(getRes->count(0xe2) > 0 && getRes->at(0xe2).size() == 194)) {
        return nullptr;
    }

    auto result = std::make_unique<std::vector<MeterValue>>();
    auto e2 = getRes->at(0xe2);
    int idx = 2;
    for (int i = 0; i < 48; i++) {
        uint32_t cumulative = e2[idx] << 24 | e2[idx + 1] << 16 | e2[idx + 2] << 8 | e2[idx + 3];
        if (cumulative == 0xfffffffe) {
            continue;
        }
        result->push_back(MeterValue(
                timestamp, nullptr,
                std::make_unique<double>(cumulative * pow(10, *_cumulativePow))));
        timestamp += 1800;
        idx += 4;
    }
    return result;
}

/**
 * 現在の計測値を取得
 *
 * @return 現在の計測値
 */
std::unique_ptr<MeterValue> SmartMeterClient::getMeterValue() {
    time_t timestamp;
    time(&timestamp);
    std::vector<uint8_t> props = {
            0xe7,  // 瞬時電力計測値 (W)
            0xe0,  // 積算電力量計測値(正方向計測値)
    };
    auto getRes = _getProperty(props);
    if (getRes == nullptr) {
        return nullptr;
    }
    if (!(getRes->count(0xe7) > 0 && getRes->at(0xe7).size() == 4)
        || !(getRes->count(0xe0) > 0 && getRes->at(0xe0).size() == 4)) {
        return nullptr;
    }

    auto e7 = getRes->at(0xe7);
    auto instantaneous = e7[0] << 24 | e7[1] << 16 | e7[2] << 8 | e7[3];
    auto e0 = getRes->at(0xe0);
    auto cumulative = e0[0] << 24 | e0[1] << 16 | e0[2] << 8 | e0[3];
    return std::make_unique<MeterValue>(
            timestamp, std::make_unique<uint32_t>(instantaneous),
            std::make_unique<double>(cumulative * pow(10, *_cumulativePow)));
}

/**
 * Discard buffer
 */
void SmartMeterClient::_discardBuffer() {
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
void SmartMeterClient::_sendCommand(const String &data) {
    Serial.println("> " + data);
    _bp35a1.print(data + "\r\n");
}

/**
 * Wait response
 *
 * @return true:received, false:error or timeout
 */
bool SmartMeterClient::_waitResponse(const char *expect, int timeout) {
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
 * @return response string (nullptr:timeout)
 */
std::unique_ptr<String> SmartMeterClient::_readLine(int timeout) {
    std::stringstream ss;
    auto start = millis();
    while (millis() - start < timeout) {
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
void SmartMeterClient::_sendData(const uint8_t *data, int len) {
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
std::unique_ptr<std::vector<uint8_t>> SmartMeterClient::_recvData(int timeout) {
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
std::unique_ptr<MeterEntry> SmartMeterClient::_getScanResult(int timeout) {
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
            return std::make_unique<MeterEntry>(addr, panId, channel);
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
bool SmartMeterClient::_getJoinResult(int timeout) {
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

/**
 * 積算電力量単位を取得
 *
 * @return 積算電力量単位
 */
std::unique_ptr<int> SmartMeterClient::_getMeterCumulativePow() {
    time_t timestamp;
    time(&timestamp);
    std::vector<uint8_t> props = {
            0xe1,  // 積算電力量単位(正方向、逆方向計測値)
    };
    auto getRes = _getProperty(props);
    if (getRes == nullptr) {
        return nullptr;
    }
    if (!(getRes->count(0xe1) > 0 && getRes->at(0xe1).size() == 1)) {
        return nullptr;
    }

    auto e1 = getRes->at(0xe1);
    auto cumulativeUnit = e1[0];
    int cumulativePow = 0;
    if (cumulativeUnit > 0 && cumulativeUnit <= 4) {
        cumulativePow = -cumulativeUnit;
    } else if (cumulativeUnit >= 10 && cumulativeUnit <= 13) {
        cumulativePow = cumulativeUnit - 9;
    }
    return std::make_unique<int>(cumulativePow);
}

/**
 * プロパティ値受信
 *
 * @param epc EPC
 * @return プロパティ値
 */
std::unique_ptr<std::map<uint8_t, std::vector<uint8_t>>>
SmartMeterClient::_recvProperty(uint8_t reqEsv, int timeout) {
    // Receive response
    std::unique_ptr<std::vector<uint8_t>> data = nullptr;
    auto start = millis();
    while (millis() - start < timeout) {
        data = _recvData(timeout - (int) (millis() - start));
        if (data == nullptr || data->size() < 12) {
            return nullptr;
        }
        uint16_t tid = data->at(2) << 8 | data->at(3);
        if (tid == _tid) {
            break;
        }
    }
    auto esv = data->at(10);
    if (esv != reqEsv) {
        return nullptr;
    }

    // Read response
    auto result = std::make_unique<std::map<uint8_t, std::vector<uint8_t>>>();
    int idx = 11;
    int opc = (int) data->at(idx++); // OPC
    for (int i = 0; i < opc; i++) {
        auto epc = data->at(idx++); // EPC
        auto pdc = (int) data->at(idx++); // PDC
        auto value = std::vector<uint8_t>();
        for (int j = 0; j < pdc; j++) {
            value.push_back(data->at(idx++));
        }
        result->emplace(epc, std::move(value));
    }
#if 0
    for (const auto &r: *result) {
        Serial.printf("Property: %02x, value: ", r.first);
        for (const auto &v: r.second) {
            Serial.printf("%02x", v);
        }
        Serial.println();
    }
#endif
    return result;
}

/**
 * プロパティ値読み出し
 *
 * @param epc EPC
 * @return EPC, プロパティ値
 */
std::unique_ptr<std::map<uint8_t, std::vector<uint8_t>>>
SmartMeterClient::_getProperty(const std::vector<uint8_t> &epcs) {
    static uint8_t header[] = {
            0x10,  // EHD1
            0x81,  // EHD2
            0x00, 0x00,   // TID
            0x05, 0xff, 0x01,  // SEOJ: Controller
            0x02, 0x88, 0x01,  // DEOJ: Smart meter
            0x62,  // ESV: Get
    };
    auto sendLen = sizeof(header) + 1 + epcs.size() * 2;
    auto sendBuf = std::unique_ptr<uint8_t>(new uint8_t[sendLen]);
    auto pBuf = sendBuf.get();
    memcpy(pBuf, header, sizeof(header));
    _tid++;
    pBuf[2] = (uint8_t)((_tid >> 8) & 0xff);
    pBuf[3] = (uint8_t)(_tid & 0xff);
    pBuf += sizeof(header);
    *(pBuf++) = epcs.size(); // OPC
    for (auto epc: epcs) {
        *(pBuf++) = epc;
        *(pBuf++) = 0x00;
    }
    _sendData(sendBuf.get(), (int) sendLen);
    if (!_waitResponse("OK", 5000)) {
        return nullptr;
    }

    // 0x72: Get_Res
    return _recvProperty(0x72, _getTimeout(epcs));
}

/**
 * プロパティ値書き込み
 *
 * @param epc EPC, プロパティ値
 * @return EPC, プロパティ値
 */
std::unique_ptr<std::map<uint8_t, std::vector<uint8_t>>>
SmartMeterClient::_setProperty(const std::map<uint8_t, std::vector<uint8_t>> &props) {
    static uint8_t header[] = {
            0x10,  // EHD1
            0x81,  // EHD2
            0x00, 0x00,   // TID
            0x05, 0xff, 0x01,  // SEOJ: Controller
            0x02, 0x88, 0x01,  // DEOJ: Smart meter
            0x61,  // ESV: SetC
    };
    auto sendLen = sizeof(header) + 1;
    for (const auto &prop: props) {
        sendLen += 2 + prop.second.size();
    }
    auto sendBuf = std::unique_ptr<uint8_t>(new uint8_t[sendLen]);
    auto pBuf = sendBuf.get();
    memcpy(pBuf, header, sizeof(header));
    _tid++;
    pBuf[2] = (uint8_t)((_tid >> 8) & 0xff);
    pBuf[3] = (uint8_t)(_tid & 0xff);
    pBuf += sizeof(header);
    *(pBuf++) = props.size(); // OPC
    for (const auto &prop: props) {
        *(pBuf++) = prop.first;
        *(pBuf++) = prop.second.size();
        for (const auto &v: prop.second) {
            *(pBuf++) = v;
        }
    }
    _sendData(sendBuf.get(), (int) sendLen);
    if (!_waitResponse("OK", 5000)) {
        return nullptr;
    }

    // 0x71: Set_Res
    std::vector<uint8_t> epcs;
    for (const auto &prop: props) {
        epcs.push_back(prop.first);
    }
    return _recvProperty(0x71, _getTimeout(epcs));
}

int SmartMeterClient::_getTimeout(const std::vector<uint8_t> &epcs) {
    if (epcs.size() > 1) {
        return 60000;
    }
    for (const auto epc: epcs) {
        if (epc == 0xe2 || epc == 0xe4 || epc == 0xec) {
            return 60000;
        }
    }
    return 20000;
}
