#if !defined(APP_SMART_METER_H)
#define APP_SMART_METER_H

#include <utility>

#include "lib/SmartMeter.h"

class ScanEntry {
public:
    explicit ScanEntry(String addr, String panId, String channel)
            : addr(std::move(addr)), panId(std::move(panId)), channel(std::move(channel)) {};
    String panId;
    String channel;
    String addr;
    String ipv6Addr;
};

class MeterValues {
public:
    explicit MeterValues(time_t timestamp, uint32_t instantaneous, uint32_t cumulative, int cumulativePow)
            : _timestamp(timestamp), _instantaneous(instantaneous), _cumulative(cumulative),
              _cumulativeExp10(cumulativePow) {};

    time_t getTimestamp() const { return _timestamp; }

    uint32_t getInstantaneous() const { return _instantaneous; }

    double getCumulative() const { return _cumulative * pow(10, _cumulativeExp10); }

    String getCumulativeStr() const {
        int d = (int) pow(10, abs(_cumulativeExp10));
        if (_cumulativeExp10 > 0) {
            return String(_cumulative * d);
        } else {
            return String(_cumulative / d) + "." + String(_cumulative % d);
        }
    }

private:
    time_t _timestamp;
    uint32_t _instantaneous;
    uint32_t _cumulative;
    int _cumulativeExp10;
};

class SmartMeter {
public:
    explicit SmartMeter(const HardwareSerial &serial, int8_t rxPin, int8_t txPin)
            : _bp35a1(serial), _rxPin(rxPin), _txPin(txPin) {};

    bool connect();

    std::unique_ptr<MeterValues> getMeterValues();

private:
    HardwareSerial _bp35a1;
    int8_t _rxPin;
    int8_t _txPin;

    /// read timeout (ms)
    int _readTimeout = 5000;

    /// Meter
    std::unique_ptr<ScanEntry> _meter;

    void _discardBuffer();

    void _sendCommand(const String &data);

    bool _waitResponse(const char *expect);

    std::unique_ptr<String> _readLine();

    void _sendData(const uint8_t *data, int len);

    std::unique_ptr<std::vector<uint8_t>> _recvData(int timeout);

    std::unique_ptr<ScanEntry> _getScanResult(int timeout);

    bool _getJoinResult(int timeout);
};

#endif // !defined(APP_SMART_METER_H)
