#include <Arduino.h>
#include <ESP32WebServer.h>

#include "app/AppMeter.h"
#include "app/AppServer.h"
#include "lib/utils.h"

void AppServer::setup() {
    static const char *headerKeys[] = {"Content-Type"};
    _httpServer.collectHeaders(headerKeys, 1);
    _httpServer.on("/", [&] { _onRoot(); });
    _httpServer.on("/latest", [&] { _onLatest(); });
    _httpServer.on("/history", [&] { _onHistory(); });
    _httpServer.onNotFound([&] { _onNotFound(); });
    _httpServer.begin();
}

void AppServer::loop() {
    _httpServer.handleClient();
}

void AppServer::_onRoot() {
    _httpServer.send(200, "text/plain", "OK");
}

void AppServer::_onLatest() {
    auto data = _meter->getLatest();
    if (data == nullptr) {
        _httpServer.send(404);
        return;
    }
    DynamicJsonDocument body(1024);
    body["timestamp"] = data->getTimestamp();
    body["instantaneous"] = data->getInstantaneous();
    body["cumulative"] = data->getCumulative();
    _httpServer.send(200, "text/plain", jsonEncode(body));
}

void AppServer::_onHistory() {
    auto history = _meter->getHistory();
    DynamicJsonDocument body(10240);
    for (const auto &v: history) {
        auto entry = body.createNestedObject();
        entry["timestamp"] = v.getTimestamp();
        entry["cumulative"] = v.getCumulative();
    }
    _httpServer.send(200, "text/plain", jsonEncode(body));
}

void AppServer::_onNotFound() {
    _httpServer.send(404);
}
