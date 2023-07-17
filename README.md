# M5SmartMeterHub

## Summary

Collects electricity usage continuously from a low-voltage smart electric energy meter and send it to MQTT server.

## Prerequisites

- A low-voltage smart electric energy meter
- [M5StickC](https://shop.m5stack.com/products/stick-c) or [M5StickC Plus](https://shop.m5stack.com/products/m5stickc-plus-esp32-pico-mini-iot-development-kit) 
- [Wi-SUN HAT Kit](https://www.switch-science.com/products/7612)
- [BP35A1 - Wi-SUN Compatible Wireless Module](https://www.rohm.com/products/wireless-communication/specified-low-power-radio-modules/bp35a1-product)
- [PlatformIO](https://platformio.org/)

## References

- [ECHONET Lite Specification](https://echonet.jp/spec_g/)

## Build

 1. Create src/config.h from src/config.h.template
 
 2. [OPTIONAL] Place certificates and a private key to data/ folder

    - data/ca.pem.crt (CA certificate)
    - data/certificate.pem.crt (Device certificate)
    - data/private.pem.key (Private key)

    and upload it to the device filesystem.

    ```shell
    pio run -t uploadfs
    ```

 3. Build and upload the firmware to the device.

    ```shell
    pio run -t upload
    ```

## Message to publish

Example

```json
{
  "timestamp": 1689554292,
  "instantaneous": 1124,
  "cumulative": 18754.5
}
```

- timestamp : unix epoch time
- instantaneous : instantaneous electric energy [W]
- cumulative : cumulative amounts of electric energy [kWh]
