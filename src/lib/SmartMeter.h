#if !defined(LIB_SMART_METER_H)
#define LIB_SMART_METER_H

#include <map>
#include <utility>
#include <Arduino.h>

/**
 * Smart meter entry
 */
class MeterEntry {
public:
    explicit MeterEntry(String addr, String panId, String channel)
            : addr(std::move(addr)), panId(std::move(panId)), channel(std::move(channel)) {};
    String panId;
    String channel;
    String addr;
    String ipv6Addr;
};

/**
 * Smart meter measured value
 */
class MeterValue {
public:
    explicit MeterValue(time_t timestamp, uint32_t instantaneous, uint32_t cumulative, int cumulativePow)
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

#endif // !defined(LIB_SMART_METER_H)
