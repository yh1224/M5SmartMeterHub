#if !defined(LIB_METER_ENTRY_H)
#define LIB_METER_ENTRY_H

#include <memory>
#include <utility>
#include <Arduino.h>

/**
 * Smart meter entry
 */
class MeterEntry {
public:
    explicit MeterEntry(String addr, String panId, String channel)
            : addr(std::move(addr)), panId(std::move(panId)), channel(std::move(channel)) {};
    String panId;
    String channel;
    String addr;
    String ipv6Addr;
};

#endif // !defined(LIB_METER_ENTRY_H)
