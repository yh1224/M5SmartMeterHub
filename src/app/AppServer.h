#if !defined(APP_SERVER_H)
#define APP_SERVER_H

#include <ESP32WebServer.h>

#include <utility>

#include "app/AppMeter.h"
#include "app/AppServer.h"

class AppServer {
public:
    explicit AppServer(std::shared_ptr<AppMeter> meter) : _meter(std::move(meter)) {};

    void setup();

    void loop();

private:
    std::shared_ptr<AppMeter> _meter;

    ESP32WebServer _httpServer{80};

    void _onRoot();

    void _onLatest();

    void _onHistory();

    void _onNotFound();
};

#endif // !defined(APP_SERVER_H)
