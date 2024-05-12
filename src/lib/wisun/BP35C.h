#if !defined(LIB_WISUN_BP35C_H)
#define LIB_WISUN_BP35C_H

#include "lib/wisun/WiSUN.h"

/**
 * Smart meter entry
 */
class BP35CMeterEntry {
public:
    explicit BP35CMeterEntry(uint8_t addr[8], uint8_t panId[2], uint8_t channel) : channel(channel) {
        memcpy(this->panId, panId, sizeof(this->panId));
        memcpy(this->addr, addr, sizeof(this->addr));
    };
    uint8_t panId[2];
    uint8_t channel;
    uint8_t addr[8];
};

/**
 * Wi-SUN module: BP35C
 */
class BP35C : public WiSUN {
public:
    explicit BP35C(const HardwareSerial &serial, int8_t rxPin, int8_t txPin)
            : _serial(serial), _rxPin(rxPin), _txPin(txPin) {
        _serial.begin(115200, SERIAL_8N1, _rxPin, _txPin);
    };

    bool connect(const String &brouteId, const String &broutePassword) override;

    bool sendData(const uint8_t *data, size_t dataLen, int timeout) override;

    std::unique_ptr<std::vector<uint8_t>> receiveData(int timeout) override;

private:
    HardwareSerial _serial;
    int8_t _rxPin;
    int8_t _txPin;

    /// Meter
    std::unique_ptr<BP35CMeterEntry> _meter;

    void _discardBuffer();

    void _sendCommand(uint16_t commandCode, const uint8_t *data, size_t dataLen);

    void _sendCommand(uint16_t cmd) {
        _sendCommand(cmd, nullptr, 0);
    }

    bool _waitResponse(uint16_t cmd, const uint8_t *expect, size_t expectLen, int timeout);

    std::unique_ptr<std::vector<uint8_t>> _readCommand(int timeout);

    std::unique_ptr<BP35CMeterEntry> _getScanResult(int timeout);
};

#endif // !defined(LIB_WISUN_BP35C_H)
