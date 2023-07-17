#if !defined(APP_APP_H)
#define APP_APP_H

#include <utility>
#include <M5Unified.h>

#include "lib/Mqtt.h"
#include "lib/SmartMeter.h"

class App {
public:
    void setup();

    void loop();

private:
    std::unique_ptr<SmartMeter> _meter;
    std::unique_ptr<Mqtt> _mqtt;

    /// Failed count
    int _failure = 0;
};

#endif // !defined(APP_APP_H)
