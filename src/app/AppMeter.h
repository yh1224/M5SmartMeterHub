#if !defined(APP_APP_METER_H)
#define APP_APP_METER_H

#include "lib/Mqtt.h"
#include "lib/SmartMeterClient.h"

class AppMeter {
public:
    void setup();

    void toggleDisplayMode();

    std::unique_ptr<MeterValue> getLatest();

    std::vector<MeterValue> getHistory();

private:
    std::unique_ptr<SmartMeterClient> _smartMeter;
    std::unique_ptr<Mqtt> _mqtt;

    TaskHandle_t _taskHandle;

    SemaphoreHandle_t _lock = xSemaphoreCreateMutex();

    /// Display mode
    int _displayMode = 0;

    /// Failed count
    int _failure = 0;

    /// Last measured time
    time_t _lastMeasureTime = 0;

    /// Meter History
    std::unique_ptr<MeterValue> _measured;

    /// Last history time
    time_t _lastHistoryTime = 0;

    /// Meter History
    std::vector<MeterValue> _meterHistory;

    void _setup();

    void _loop();

    void _updateDisplay();

    std::unique_ptr<MeterValue> _getHistory(time_t timestamp);

    std::unique_ptr<int> _getCostToday();

    bool _measure();

    bool _updateHistory();
};

#endif // !defined(APP_APP_METER_H)
