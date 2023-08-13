#if !defined(APP_APP_METER_H)
#define APP_APP_METER_H

#include "lib/Mqtt.h"
#include "lib/SmartMeterClient.h"

class AppMeter {
public:
    typedef enum {
        DISPLAY_MODE_NORMAL = 0,
        DISPLAY_MODE_COST,
        DISPLAY_MODE_MAX,
    } DisplayMode;

    void setup();

    void toggleDisplayMode();

    std::unique_ptr<MeterValue> getLatest();

    std::vector<MeterValue> getHistory();

private:
    std::unique_ptr<SmartMeterClient> _smartMeter;
#if defined(MQTT_ENABLE)
    std::unique_ptr<Mqtt> _mqtt;
#endif // defined(MQTT_ENABLE)

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

    /// Last history published time
    time_t _lastHistoryPublishTime = 0;

    /// Meter History
    std::vector<MeterValue> _meterHistory;

    void _setup();

    void _loop();

    void _updateDisplay();

    std::unique_ptr<MeterValue> _getHistory(time_t timestamp);

    std::unique_ptr<double> _getUsageToday();

    bool _measure();

    bool _updateHistory();

    void _publishMeasured();

    void _publishHistory();
};

#endif // !defined(APP_APP_METER_H)
