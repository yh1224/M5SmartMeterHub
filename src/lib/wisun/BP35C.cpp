#include <iomanip>
#include <M5Unified.h>

#include "lib/wisun/BP35C.h"
#include "lib/utils.h"

/**
 * Command header
 */
typedef struct {
    uint8_t uniqueCode[4];
    uint8_t commandCode[2];
    uint8_t messageLength[2];
    uint8_t headerChecksum[2];
    uint8_t dataChecksum[2];
} __attribute__((packed)) bp35c_command_header_t;

/**
 * Scan request
 */
typedef struct {
    uint8_t scanTime;
    uint8_t scanChannel[4];
    uint8_t idSetting;
    uint8_t pairingId[8];
} __attribute__((packed)) bp35c_scan_request_t;

/**
 * Scan result
 */
typedef struct {
    uint8_t scanResult;
    uint8_t channel;
    uint8_t numberOfScans;
    uint8_t macAddress[8];
    uint8_t panId[2];
    uint8_t rssi;
} __attribute__((packed)) bp35c_scan_result_t;

/**
 * Transmit data request header
 */
typedef struct {
    uint8_t destinationIPv6Address[16];
    uint8_t sourcePort[2];
    uint8_t destinationPort[2];
    uint8_t transmissionDataSize[2];
} __attribute__((packed)) bp35c_tx_request_header_t;

/**
 * Connect to smart meter
 *
 * @return true:success, false:failure
 */
bool BP35C::connect(const String &brouteId, const String &broutePassword) {
    // Reset hardware
    pinMode(26, OUTPUT);
    digitalWrite(26, LOW);
    delay(100);
    digitalWrite(26, HIGH);

    delay(2000);
    _discardBuffer();

    M5.Display.setTextSize(2);
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Initializing");

    // Reset
    M5.Display.printf(".");
    _sendCommand(0x00d9); // Reset hardware
    if (!_waitResponse(0x6019, nullptr, 0, 5000)) {
        return false;
    }

    // Check version
    M5.Display.printf(".");
    _sendCommand(0x006b); // Get version information
    if (!_waitResponse(0x206b, nullptr, 0, 5000)) {
        return false;
    }

    // Setup
    M5.Display.printf(".");
    uint8_t initialSettingsData[] = {
            0x05, // Operation mode: Dual (Route B and HAN)
            0x00, // HAN sleep function setting: Disable
            0x04, // Channel: 922.5
            0x00, // Transmission power: 20mW
    };
    uint8_t initialSettingsExpects = {0x01};
    _sendCommand(0x005f, initialSettingsData, sizeof(initialSettingsData)); // Initial settings
    if (!_waitResponse(0x205f, &initialSettingsExpects, sizeof(initialSettingsExpects), 5000)) {
        return false;
    }

    M5.Display.println("OK");
    M5.Display.printf("Scanning");

    // Scan
    for (uint8_t dur = 6; dur <= 8; dur++) {
        M5.Display.printf(".");
        bp35c_scan_request_t scanRequest = {
                .scanTime = dur, // 9.64ms×2^dur
                .scanChannel = {0x00, 0x03, 0xff, 0xf0}, // scan channel 4 to 17
                .idSetting = 0x01, // Paring ID set
                .pairingId = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        };
        // set the last eight characters of Route-B authentication ID to Pairing ID.
        memcpy(scanRequest.pairingId, brouteId.c_str() + 24, sizeof(scanRequest.pairingId));
        _sendCommand(0x0051, (uint8_t *) &scanRequest, sizeof(scanRequest)); // Execute Active Scan
        _meter = _getScanResult((1 << dur) * 1000);
        if (_meter != nullptr) {
            break;
        }
        if (dur == 8) {
            return false;
        }
        delay(1000);
    }

    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Addr: %s, Channel: %d, Pan ID: %s\n",
                      hexString(_meter->addr, sizeof(_meter->addr)).c_str(), _meter->channel,
                      hexString(_meter->panId, sizeof(_meter->panId)).c_str());
    Serial.printf("Addr: %s, Channel: %d, Pan ID: %s\n",
                  hexString(_meter->addr, sizeof(_meter->addr)).c_str(), _meter->channel,
                  hexString(_meter->panId, sizeof(_meter->panId)).c_str());

    M5.Display.printf("Connecting");

    // Setup
    M5.Display.printf(".");
    initialSettingsData[2] = _meter->channel;
    _sendCommand(0x005f, initialSettingsData, sizeof(initialSettingsData)); // Initial settings
    if (!_waitResponse(0x205f, &initialSettingsExpects, sizeof(initialSettingsExpects), 5000)) {
        return false;
    }

    // Set B-route ID/password
    M5.Display.printf(".");
    struct {
        uint8_t id[32];
        uint8_t password[12];
    } authData = {};
    memcpy(authData.id, brouteId.c_str(), sizeof(authData.id));
    memcpy(&authData.password, broutePassword.c_str(), sizeof(authData.password));
    _sendCommand(0x0054, (uint8_t *) &authData, sizeof(authData)); // Set Route-B PANA Authentication Information
    uint8_t authExpects = {0x01};
    if (!_waitResponse(0x2054, &authExpects, sizeof(authExpects), 5000)) {
        return false;
    }

    // Initiate connection
    M5.Display.printf(".");
    _sendCommand(0x0053, nullptr, 0); // Initiate Route-B Operation
    uint8_t initiateExpects = {0x01};
    if (!_waitResponse(0x2053, &initiateExpects, sizeof(initiateExpects), 5000)) {
        return false;
    }

    // Open UDP port
    M5.Display.printf(".");
    uint8_t openUdpData[] = {0x0e, 0x1a}; // port: 3610
    _sendCommand(0x0005, openUdpData, sizeof(openUdpData)); // Open UDP Port
    uint8_t openUdpExpects = {0x01};
    if (!_waitResponse(0x2005, &openUdpExpects, sizeof(openUdpExpects), 5000)) {
        return false;
    }

    // Authenticate
    M5.Display.printf(".");
    _sendCommand(0x0056, nullptr, 0); // Initiate Route-B PANA
    uint8_t panaExpects = {0x01};
    if (!_waitResponse(0x2056, &panaExpects, sizeof(panaExpects), 5000)) {
        return false;
    }
    if (!_waitResponse(0x6028, &panaExpects, sizeof(panaExpects), 5000)) {
        // Received Notify PANA Authentication Result
        return false;
    }

    M5.Display.println("OK");
    return true;
}

/**
 * Discard buffer
 */
void BP35C::_discardBuffer() {
    delay(1000);
    while (_serial.available()) {
        _serial.read();
    }
}

/**
 * Send command
 *
 * @param commandCode command code
 * @param data data
 * @param dataLen data length
 */
void BP35C::_sendCommand(uint16_t commandCode, const uint8_t *data, size_t dataLen) {
    bp35c_command_header_t header = {
            .uniqueCode = {0xd0, 0xea, 0x83, 0xfc}, // Request: 0xD0EA83FC
            .commandCode = {(uint8_t) (commandCode >> 8), (uint8_t) (commandCode & 0xff)},
            .messageLength = {(uint8_t) ((4 + dataLen) >> 8), (uint8_t) ((4 + dataLen) & 0xff)},
    };
    uint16_t hSum = 0, dSum = 0;
    for (int i = 0; i < 8; i++) hSum += *(((uint8_t *) &header) + i);
    if (data != nullptr) {
        for (int i = 0; i < dataLen; i++) dSum += *(data + i);
    }
    header.headerChecksum[0] = (uint8_t) (hSum >> 8);
    header.headerChecksum[1] = (uint8_t) (hSum & 0xff);
    header.dataChecksum[0] = (uint8_t) (dSum >> 8);
    header.dataChecksum[1] = (uint8_t) (dSum & 0xff);

    Serial.printf("> %s %s %s %s %s %s\n",
                  hexString((uint8_t *) header.uniqueCode, sizeof(header.uniqueCode)).c_str(),
                  hexString((uint8_t *) header.commandCode, sizeof(header.commandCode)).c_str(),
                  hexString((uint8_t *) header.messageLength, sizeof(header.messageLength)).c_str(),
                  hexString((uint8_t *) header.headerChecksum, sizeof(header.headerChecksum)).c_str(),
                  hexString((uint8_t *) header.dataChecksum, sizeof(header.dataChecksum)).c_str(),
                  hexString(data, dataLen).c_str());
    _serial.write((uint8_t *) &header, sizeof(header));
    if (dataLen > 0) {
        _serial.write(data, dataLen);
    }
}

/**
 * Wait response
 *
 * @return true:received, false:error or timeout
 */
bool BP35C::_waitResponse(uint16_t cmd, const uint8_t *expect, size_t expectLen, int timeout) {
    auto start = millis();
    while (millis() - start < timeout) {
        auto command = _readCommand(timeout - (int) (millis() - start));
        if (command == nullptr) {
            return false;
        } else {
            auto header = (bp35c_command_header_t *) command->data();
            auto data = (uint8_t *) (header + 1);
            if ((header->commandCode[0] << 8 | header->commandCode[1]) == cmd) {
                return expect == nullptr || memcmp(data, expect, expectLen) == 0;
            }
        }
    }
    return false;
}

/**
 * Read command
 *
 * @param timeout timeout (milliseconds)
 * @return response string (nullptr:timeout)
 */
std::unique_ptr<std::vector<uint8_t>> BP35C::_readCommand(int timeout) {
    static uint8_t buf[256];
    std::vector<uint8_t> ss;
    auto start = millis();
    size_t dataLen = 0;
    size_t recvLen = 0;

    // read header
    while (millis() - start < timeout) {
        auto len = _serial.readBytes(&buf[recvLen], sizeof(bp35c_command_header_t) - recvLen);
        if (len == 0) {
            continue;
        }
        recvLen += len;
        if (recvLen >= sizeof(bp35c_command_header_t)) {
            // find Response or Notification command
            auto *p = (uint8_t *) buf;
            while (!(*p == 0xd0 && *(p + 1) == 0xf9 && *(p + 2) == 0xee && *(p + 3) == 0x5d)) {
                p++;
                recvLen--;
                if (recvLen < 4) {
                    break;
                }
            }
            if (p == buf) {
                auto *header = (bp35c_command_header_t *) buf;
                dataLen = (header->messageLength[0] << 8) + header->messageLength[1] - 4;
                for (int i = 0; i < recvLen; i++) {
                    ss.push_back(buf[i]);
                }
                break;
            }
            memcpy(buf, p, recvLen);
            continue;
        }
    }

    // read data
    while (millis() - start < timeout) {
        size_t restLen = (sizeof(bp35c_command_header_t) + dataLen) - recvLen;
        if (restLen > 0) {
            auto len = _serial.readBytes(buf, min(restLen, sizeof(buf)));
            if (len == 0) {
                continue;
            }
            for (int i = 0; i < len; i++) {
                ss.push_back(buf[i]);
                recvLen++;
            }
        }
        if (recvLen >= sizeof(bp35c_command_header_t) + dataLen) {
            auto header = (bp35c_command_header_t *) ss.data();
            auto data = (uint8_t *) (header + 1);
            Serial.printf("< %s %s %s %s %s %s\n",
                          hexString((uint8_t *) header->uniqueCode, sizeof(header->uniqueCode)).c_str(),
                          hexString((uint8_t *) header->commandCode, sizeof(header->commandCode)).c_str(),
                          hexString((uint8_t *) header->messageLength, sizeof(header->messageLength)).c_str(),
                          hexString((uint8_t *) header->headerChecksum, sizeof(header->headerChecksum)).c_str(),
                          hexString((uint8_t *) header->dataChecksum, sizeof(header->dataChecksum)).c_str(),
                          hexString(data, dataLen).c_str());
            return std::make_unique<std::vector<uint8_t>>(ss);
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
bool BP35C::sendData(const uint8_t *data, size_t dataLen, int timeout) {
    uint8_t command[sizeof(bp35c_tx_request_header_t) + dataLen];
    bp35c_tx_request_header_t header = {
            .destinationIPv6Address = {0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
            .sourcePort = {0x0e, 0x1a},
            .destinationPort = {0x0e, 0x1a},
            .transmissionDataSize = {(uint8_t) (dataLen >> 8), (uint8_t) (dataLen & 0xff)},
    };
    memcpy(header.destinationIPv6Address + 8, _meter->addr, sizeof(_meter->addr));
    header.destinationIPv6Address[8] ^= 0x02; // invert the lower 2nd bit of the first 1 byte of the MAC address.
    memcpy(command, &header, sizeof(header));
    memcpy(&command[sizeof(header)], data, dataLen);
    _sendCommand(0x0008, command, sizeof(header) + dataLen); // Transmit Data
    uint8_t expects[] = {0x01, 0x00};
    return _waitResponse(0x2008, expects, sizeof(expects), timeout);
}

/**
 * Receive ECHONET Lite data
 *
 * @param timeout timeout (milliseconds)
 * @return ECHONET Lite data (nullptr:failure)
 */
std::unique_ptr<std::vector<uint8_t>> BP35C::receiveData(int timeout) {
    auto result = std::make_unique<std::vector<uint8_t>>();
    auto start = millis();
    while (millis() - start < timeout) {
        auto command = _readCommand(timeout - (int) (millis() - start));
        if (command == nullptr) {
            return nullptr;
        } else {
            auto header = (bp35c_command_header_t *) command->data();
            auto data = (uint8_t *) (header + 1);
            auto dataLen = (header->messageLength[0] << 8) + header->messageLength[1] - 4;
            if ((header->commandCode[0] << 8 | header->commandCode[1]) == 0x6018) { // Notify Data Reception
                for (int i = 27; i < dataLen; i++) {
                    result->push_back(*(data + i));
                }
                return result;
            }
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
std::unique_ptr<BP35CMeterEntry> BP35C::_getScanResult(int timeout) {
    std::unique_ptr<bp35c_scan_result_t> scanResult = nullptr;

    auto start = millis();
    while (millis() - start < timeout) {
        auto command = _readCommand(timeout - (int) (millis() - start));
        if (command == nullptr) {
            continue;
        } else {
            auto header = (bp35c_command_header_t *) command->data();
            auto data = (bp35c_scan_result_t *) (header + 1);
            auto commandCode = (header->commandCode[0] << 8 | header->commandCode[1]);
            if (commandCode == 0x4051 && data->scanResult == 0x00) { // Notification
                scanResult = std::make_unique<bp35c_scan_result_t>(*data);
            } else if (commandCode == 0x2051) { // Response
                if (scanResult == nullptr) {
                    return nullptr;
                }
                return std::make_unique<BP35CMeterEntry>(
                        scanResult->macAddress, scanResult->panId, scanResult->channel);
            }
        }
    }
    return nullptr;
}
