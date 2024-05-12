#if !defined(LIB_WISUN_BP35A_H)
#define LIB_WISUN_BP35A_H

#include "lib/wisun/WiSUN.h"

/**
 * Smart meter entry
 */
class BP35AMeterEntry {
public:
    explicit BP35AMeterEntry(String addr, String panId, String channel)
            : addr(std::move(addr)), panId(std::move(panId)), channel(std::move(channel)) {};
    String panId;
    String channel;
    String addr;
    String ipv6Addr;
};

/**
 * Wi-SUN module: BP35A
 */
class BP35A : public WiSUN {
public:
    explicit BP35A(const HardwareSerial &serial, int8_t rxPin, int8_t txPin)
            : _serial(serial), _rxPin(rxPin), _txPin(txPin) {
        _serial.begin(115200, SERIAL_8N1, _rxPin, _txPin);
    };

    bool connect(const String &brouteId, const String &broutePassword) override;

    bool sendData(const uint8_t *data, size_t dataLen, int timeout) override;

    std::unique_ptr <std::vector<uint8_t>> receiveData(int timeout) override;

private:
    HardwareSerial _serial;
    int8_t _rxPin;
    int8_t _txPin;

    /// Meter
    std::unique_ptr <BP35AMeterEntry> _meter;

    void _discardBuffer();

    void _sendCommand(const String &data);

    bool _waitResponse(const char *expect, int timeout);

    std::unique_ptr <String> _readLine(int timeout);

    std::unique_ptr <BP35AMeterEntry> _getScanResult(int timeout);

    bool _getJoinResult(int timeout);
};

#endif // !defined(LIB_WISUN_BP35A_H)
