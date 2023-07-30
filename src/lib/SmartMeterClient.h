#if !defined(LIB_SMART_METER_CLIENT_H)
#define LIB_SMART_METER_CLIENT_H

#include <map>
#include <utility>

#include "lib/SmartMeter.h"
#include "lib/SmartMeterClient.h"

class SmartMeterClient {
public:
    explicit SmartMeterClient(
            const HardwareSerial &serial, int8_t rxPin, int8_t txPin,
            String brouteId, String broutePassword)
            : _bp35a1(serial), _rxPin(rxPin), _txPin(txPin),
              _brouteId(brouteId), _broutePassword(broutePassword) {};

    bool connect();

    std::unique_ptr<MeterValue> getMeterValue();

    std::unique_ptr<std::vector<MeterValue>> getMeterHistory(int day);

private:
    HardwareSerial _bp35a1;
    int8_t _rxPin;
    int8_t _txPin;
    String _brouteId;
    String _broutePassword;

    /// Meter
    std::unique_ptr<MeterEntry> _meter;

    /// Cumulative power
    std::unique_ptr<int> _cumulativePow;

    /// TID
    uint16_t _tid = 0;

    void _discardBuffer();

    void _sendCommand(const String &data);

    bool _waitResponse(const char *expect, int timeout);

    std::unique_ptr<String> _readLine(int timeout);

    void _sendData(const uint8_t *data, int len);

    std::unique_ptr<std::vector<uint8_t>> _recvData(int timeout);

    std::unique_ptr<MeterEntry> _getScanResult(int timeout);

    bool _getJoinResult(int timeout);

    std::unique_ptr<int> _getMeterCumulativePow();

    std::unique_ptr<std::map<uint8_t, std::vector<uint8_t>>>
    _recvProperty(uint8_t reqEsv, int timeout);

    std::unique_ptr<std::map<uint8_t, std::vector<uint8_t>>>
    _getProperty(const std::vector<uint8_t> &epcs);

    std::unique_ptr<std::map<uint8_t, std::vector<uint8_t>>>
    _setProperty(const std::map<uint8_t, std::vector<uint8_t>> &props);

    int _getTimeout(const std::vector<uint8_t> &epcs);
};

#endif // !defined(LIB_SMART_METER_CLIENT_H)
