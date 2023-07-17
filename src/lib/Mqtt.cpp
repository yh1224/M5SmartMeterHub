#include <Arduino.h>
#include <ArduinoJson.h>

#include <utility>

#include "lib/Mqtt.h"
#include "lib/utils.h"

void Mqtt::subscribe(const String &topic, std::function<void(const String &)> listener) {
    _listeners.emplace_back(topic, std::move(listener));
}

void Mqtt::dispatch() {
    _mqttClient.loop();
}

bool Mqtt::connect() {
    if (!_enabled) {
        _setup();
    }
    Serial.printf("Connecting: %s\n", _clientId.c_str());
    if (!_mqttClient.connect(_clientId.c_str())) {
        Serial.printf("failed: %d\n", _mqttClient.lastError());
        return false;
    }
    if (!_mqttClient.connected()) {
        Serial.println("failed.");
        return false;
    }
    Serial.println("connected.");
    for (const auto &listener: _listeners) {
        _mqttClient.subscribe(listener.first);
        Serial.println("Subscribed to " + listener.first);
    }
    return true;
}

bool Mqtt::publish(const String &topic, const String &payload) {
    if (!_mqttClient.connected()) {
        connect();
    }
    auto result = _mqttClient.publish(topic, payload);
    Serial.println("Publish to " + topic + ": " + payload);
    if (!result) {
        Serial.printf("Publish failed: %d\n", _mqttClient.lastError());
    }
    return result;
}

void Mqtt::_setup() {
    if (_caCert != nullptr) {
        _client.setCACert(_caCert->c_str());
    }
    if (_certificate != nullptr) {
        _client.setCertificate(_certificate->c_str());
    }
    if (_privateKey != nullptr) {
        _client.setPrivateKey(_privateKey->c_str());
    }
    _mqttClient.begin(_host.c_str(), _port, _client);
    _mqttClient.onMessage([&](String &topic, String &payload) {
        Serial.println("Received for " + topic + ": " + payload);
        _onReceive(topic, payload);
    });
    _enabled = true;
}

void Mqtt::_onReceive(const String &topic, const String &payload) {
    for (const auto &listener: _listeners) {
        if (listener.first == topic) {
            listener.second(payload);
        }
    }
}
