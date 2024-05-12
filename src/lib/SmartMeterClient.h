#if !defined(LIB_SMART_METER_CLIENT_H)
#define LIB_SMART_METER_CLIENT_H

#include <map>
#include <utility>

#include "lib/MeterValue.h"
#include "lib/wisun/WiSUN.h"

class SmartMeterClient {
public:
    explicit SmartMeterClient(std::unique_ptr<WiSUN> wisun, String brouteId, String broutePassword)
            : _wisun(std::move(wisun)), _brouteId(std::move(brouteId)), _broutePassword(std::move(broutePassword)) {};

    bool connect();

    std::unique_ptr<MeterValue> getMeterValue();

    std::unique_ptr<std::vector<MeterValue>> getMeterHistory(int day);

private:
    std::unique_ptr<WiSUN> _wisun;
    String _brouteId;
    String _broutePassword;

    /// Cumulative power
    std::unique_ptr<int> _cumulativePow;

    /// TID
    uint16_t _tid = 0;

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
