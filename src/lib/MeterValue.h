#if !defined(LIB_METER_VALUE_H)
#define LIB_METER_VALUE_H

#include <memory>
#include <utility>
#include <Arduino.h>

/**
 * Smart meter measured value
 */
class MeterValue {
public:
    explicit MeterValue(
            time_t timestamp,
            std::shared_ptr<uint32_t> instantaneous,
            std::shared_ptr<double> cumulative
    ) : _timestamp(timestamp), _instantaneous(std::move(instantaneous)), _cumulative(std::move(cumulative)) {};

    time_t getTimestamp() const { return _timestamp; }

    std::shared_ptr<uint32_t> getInstantaneous() const { return _instantaneous; }

    std::shared_ptr<double> getCumulative() const { return _cumulative; }

private:
    time_t _timestamp;
    std::shared_ptr<uint32_t> _instantaneous;
    std::shared_ptr<double> _cumulative;
};

#endif // !defined(LIB_METER_VALUE_H)
