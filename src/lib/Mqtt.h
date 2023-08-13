#if !defined(LIB_MQTT_H)
#define LIB_MQTT_H

#include <utility>
#include <vector>

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

class Mqtt {
public:
    explicit Mqtt(String host, int port, String clientId,
                  std::unique_ptr<String> caCert,
                  std::unique_ptr<String> certificate,
                  std::unique_ptr<String> privateKey)
            : _host(std::move(host)), _port(port), _clientId(std::move(clientId)),
              _caCert(std::move(caCert)),
              _certificate(std::move(certificate)),
              _privateKey(std::move(privateKey)) {};

    void subscribe(const String &topic, std::function<void(const String &)> listener);

    void dispatch();

    bool connect();

    bool publish(const String &topic, const String &payload);

private:
    String _host;
    int _port;
    String _clientId;
    std::unique_ptr<String> _caCert;
    std::unique_ptr<String> _certificate;
    std::unique_ptr<String> _privateKey;

    WiFiClientSecure _client;
    PubSubClient _mqttClient{_client};

    /// Enabled flag
    bool _enabled = false;

    /// Listeners
    std::vector<std::pair<String, std::function<void(const String &)>>> _listeners;

    void _setup();

    void _onReceive(const String &topic, const String &payload);
};

#endif // !defined(LIB_MQTT_H)
