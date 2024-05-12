#if !defined(LIB_WISUN_H)
#define LIB_WISUN_H

class WiSUN {
public:
    /** Connect to smart meter */
    virtual bool connect(const String &brouteId, const String &broutePassword) = 0;

    /** Send ECHONET Lite data */
    virtual bool sendData(const uint8_t *data, size_t dataLen, int timeout) = 0;

    /** Receive ECHONET Lite data */
    virtual std::unique_ptr<std::vector<uint8_t>> receiveData(int timeout) = 0;
};

#endif // !defined(LIB_WISUN_H)
