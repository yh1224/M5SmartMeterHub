#include <utility>

#include <Arduino.h>

#include "lib/Mqtt.h"

/// buffer size
static const size_t BUFFER_SIZE = 16 * 1024;

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
        Serial.println("failed.");
        return false;
    }
    if (!_mqttClient.connected()) {
        Serial.println("failed.");
        return false;
    }
    Serial.println("connected.");
    for (const auto &listener: _listeners) {
        _mqttClient.subscribe(listener.first.c_str());
        Serial.println("Subscribed to " + listener.first);
    }
    return true;
}

bool Mqtt::publish(const String &topic, const String &payload) {
    if (!_mqttClient.connected()) {
        connect();
    }
    auto result = _mqttClient.publish(topic.c_str(), payload.c_str());
    Serial.println("Publish to " + topic + ": " + payload);
    if (!result) {
        Serial.printf("Publish failed: %d\n", result);
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
    _mqttClient.setServer(_host.c_str(), _port);
    _mqttClient.setBufferSize(BUFFER_SIZE);
    _mqttClient.setCallback([&](const char *topic, byte *payload, unsigned int length) {
        _onReceive(topic, String(payload, length));
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
