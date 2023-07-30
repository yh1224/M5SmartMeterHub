#if !defined(APP_APP_H)
#define APP_APP_H

#include <utility>
#include <M5Unified.h>

#include "app/AppMeter.h"
#include "app/AppServer.h"
#include "lib/Mqtt.h"
#include "lib/SmartMeterClient.h"

class App {
public:
    explicit App(std::shared_ptr<AppMeter> meter, std::shared_ptr<AppServer> server)
            : _meter(std::move(meter)), _server(std::move(server)) {};

    void setup();

    void loop();

private:
    std::shared_ptr<AppMeter> _meter;
    std::shared_ptr<AppServer> _server;

    void _onButtonA();
};

#endif // !defined(APP_APP_H)
