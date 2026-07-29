#pragma once

#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>

class BLEAdvertising;
class BLECharacteristic;
class BLEServer;

namespace firefly {

class BleServerCallbacksAdapter;
class BleRxCallbacksAdapter;
class BleSecurityCallbacksAdapter;

class BlePeripheralTransport {
public:
    using ReceiveCallback = void (*)(const uint8_t *, size_t);
    using SecurityCallback = void (*)(bool);

    virtual ~BlePeripheralTransport() = default;
    virtual bool begin(const char * device_name, ReceiveCallback callback) = 0;
    virtual void advertise(uint16_t interval_ms) = 0;
    virtual void stopAdvertising() = 0;
    virtual bool notify(const uint8_t * data, size_t length) = 0;
    virtual bool connected() const = 0;
    virtual uint16_t negotiatedMtu() const = 0;
    virtual void disconnect() = 0;
    virtual void setThroughputMode(bool enabled) = 0;
    virtual void setSecurityCallback(SecurityCallback callback) = 0;
    virtual void authorizePairing(bool authorized) = 0;
    virtual bool requestSecureBond(uint32_t passkey) = 0;
    virtual bool requestEncryptedLink() = 0;
    virtual bool encrypted() const = 0;
    virtual bool clearBonds() = 0;
};

class BlePeripheralDevice final : public BlePeripheralTransport {
public:
    using ReceiveCallback = BlePeripheralTransport::ReceiveCallback;
    using SecurityCallback = BlePeripheralTransport::SecurityCallback;

    bool begin(const char * device_name, ReceiveCallback callback) override;
    void advertise(uint16_t interval_ms) override;
    void stopAdvertising() override;
    bool notify(const uint8_t * data, size_t length) override;
    bool connected() const override;
    uint16_t negotiatedMtu() const override;
    void disconnect() override;
    void setThroughputMode(bool enabled) override;
    void setSecurityCallback(SecurityCallback callback) override;
    void authorizePairing(bool authorized) override;
    bool requestSecureBond(uint32_t passkey) override;
    bool requestEncryptedLink() override;
    bool encrypted() const override;
    bool clearBonds() override;

private:
    friend class BleServerCallbacksAdapter;
    friend class BleRxCallbacksAdapter;
    friend class BleSecurityCallbacksAdapter;

    void handleConnected(uint16_t connection_id, const uint8_t * remote_address);
    void handleDisconnected();
    void handleMtuChanged(uint16_t mtu);
    void handleReceive(const uint8_t * data, size_t length);
    void handleSecurityResult(bool success);
    uint32_t passkey() const;
    bool pairingAuthorized() const;

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    ReceiveCallback receive_callback_ = nullptr;
    SecurityCallback security_callback_ = nullptr;
    BLEServer * server_ = nullptr;
    BLEAdvertising * advertising_ = nullptr;
    BLECharacteristic * tx_characteristic_ = nullptr;
    uint16_t connection_id_ = 0;
    uint16_t negotiated_mtu_ = 23;
    uint8_t remote_address_[6]{};
    bool connected_ = false;
    bool initialized_ = false;
    bool throughput_mode_ = false;
    bool pairing_authorized_ = false;
    bool encrypted_ = false;
    uint32_t passkey_ = 0;
};

}  // namespace firefly
