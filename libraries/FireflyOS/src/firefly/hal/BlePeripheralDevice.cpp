#include "BlePeripheralDevice.h"

#include <string.h>

#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>

#include "../protocol/ProtocolTypes.h"

namespace firefly {
namespace {

BlePeripheralDevice * active_device = nullptr;

uint16_t advertisingUnits(uint16_t interval_ms) {
    uint32_t units = (static_cast<uint32_t>(interval_ms) * 16U + 9U) / 10U;
    if(units < 0x20U) units = 0x20U;
    if(units > 0x4000U) units = 0x4000U;
    return static_cast<uint16_t>(units);
}

}  // namespace

class BleServerCallbacksAdapter final : public BLEServerCallbacks {
public:
    void onConnect(BLEServer *, esp_ble_gatts_cb_param_t * param) override {
        if(active_device && param) {
            active_device->handleConnected(param->connect.conn_id,
                                           param->connect.remote_bda);
        }
    }

    void onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *) override {
        if(active_device) active_device->handleDisconnected();
    }

    void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t * param) override {
        if(active_device && param) active_device->handleMtuChanged(param->mtu.mtu);
    }
};

class BleRxCallbacksAdapter final : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic * characteristic,
                 esp_ble_gatts_cb_param_t *) override {
        if(active_device && characteristic) {
            active_device->handleReceive(characteristic->getData(),
                                         characteristic->getLength());
        }
    }
};

class BleSecurityCallbacksAdapter final : public BLESecurityCallbacks {
public:
    uint32_t onPassKeyRequest() override {
        return active_device ? active_device->passkey() : 0;
    }

    void onPassKeyNotify(uint32_t) override {}

    bool onSecurityRequest() override {
        return active_device && active_device->pairingAuthorized();
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
        if(active_device) active_device->handleSecurityResult(result.success);
    }

    bool onConfirmPIN(uint32_t pin) override {
        return active_device && active_device->pairingAuthorized() &&
            pin == active_device->passkey();
    }
};

namespace {

BleServerCallbacksAdapter server_callbacks;
BleRxCallbacksAdapter rx_callbacks;
BleSecurityCallbacksAdapter security_callbacks;
BLE2902 tx_descriptor;
BLE2902 bulk_descriptor;
BLESecurity security_config;

}  // namespace

bool BlePeripheralDevice::begin(const char * device_name,
                                ReceiveCallback callback) {
    if(initialized_ || active_device != nullptr || device_name == nullptr ||
       device_name[0] == '\0' || callback == nullptr) {
        return false;
    }

    active_device = this;
    receive_callback_ = callback;
    BLEDevice::init(device_name);
    BLEDevice::setMTU(185);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLEDevice::setSecurityCallbacks(&security_callbacks);
    security_config.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    security_config.setCapability(ESP_IO_CAP_OUT);
    security_config.setKeySize(16);
    security_config.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK |
                                          ESP_BLE_ID_KEY_MASK);
    security_config.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK |
                                          ESP_BLE_ID_KEY_MASK);
    server_ = BLEDevice::createServer();
    if(server_ == nullptr) {
        active_device = nullptr;
        return false;
    }
    server_->setCallbacks(&server_callbacks);

    BLEService * service = server_->createService(protocol::kServiceUuid);
    if(service == nullptr) {
        active_device = nullptr;
        return false;
    }
    BLECharacteristic * rx = service->createCharacteristic(
        protocol::kCommandRxUuid,
        BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR
    );
    tx_characteristic_ = service->createCharacteristic(
        protocol::kEventTxUuid,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    BLECharacteristic * bulk = service->createCharacteristic(
        protocol::kBulkControlUuid,
        BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR |
            BLECharacteristic::PROPERTY_NOTIFY
    );
    if(rx == nullptr || tx_characteristic_ == nullptr || bulk == nullptr) {
        active_device = nullptr;
        return false;
    }
    rx->setCallbacks(&rx_callbacks);
    bulk->setCallbacks(&rx_callbacks);
    tx_characteristic_->addDescriptor(&tx_descriptor);
    bulk->addDescriptor(&bulk_descriptor);
    service->start();

    advertising_ = BLEDevice::getAdvertising();
    if(advertising_ == nullptr) {
        active_device = nullptr;
        return false;
    }
    advertising_->addServiceUUID(protocol::kServiceUuid);
    advertising_->setScanResponse(true);
    initialized_ = true;
    return true;
}

void BlePeripheralDevice::advertise(uint16_t interval_ms) {
    if(!initialized_ || advertising_ == nullptr || connected()) return;
    const uint16_t units = advertisingUnits(interval_ms);
    advertising_->stop();
    advertising_->setMinInterval(units);
    advertising_->setMaxInterval(static_cast<uint16_t>(units + units / 10U));
    advertising_->start();
}

void BlePeripheralDevice::stopAdvertising() {
    if(advertising_) advertising_->stop();
}

bool BlePeripheralDevice::notify(const uint8_t * data, size_t length) {
    if(data == nullptr || tx_characteristic_ == nullptr || !connected()) return false;
    if(length == 0 || length > protocol::attChunkLimit(negotiatedMtu())) return false;
    tx_characteristic_->setValue(const_cast<uint8_t *>(data), length);
    tx_characteristic_->notify();
    return true;
}

bool BlePeripheralDevice::connected() const {
    portENTER_CRITICAL(&mux_);
    const bool value = connected_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

uint16_t BlePeripheralDevice::negotiatedMtu() const {
    portENTER_CRITICAL(&mux_);
    const uint16_t value = negotiated_mtu_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

void BlePeripheralDevice::disconnect() {
    uint16_t connection_id = 0;
    portENTER_CRITICAL(&mux_);
    connection_id = connection_id_;
    const bool is_connected = connected_;
    portEXIT_CRITICAL(&mux_);
    if(is_connected && server_) server_->disconnect(connection_id);
}

void BlePeripheralDevice::setThroughputMode(bool enabled) {
    uint8_t remote_address[6]{};
    portENTER_CRITICAL(&mux_);
    if(!connected_ || throughput_mode_ == enabled) {
        portEXIT_CRITICAL(&mux_);
        return;
    }
    throughput_mode_ = enabled;
    memcpy(remote_address, remote_address_, sizeof(remote_address));
    portEXIT_CRITICAL(&mux_);
    if(!server_) return;
    if(enabled) server_->updateConnParams(remote_address, 12, 24, 0, 400);
    else server_->updateConnParams(remote_address, 80, 160, 4, 600);
}

void BlePeripheralDevice::setSecurityCallback(SecurityCallback callback) {
    portENTER_CRITICAL(&mux_);
    security_callback_ = callback;
    portEXIT_CRITICAL(&mux_);
}

void BlePeripheralDevice::authorizePairing(bool authorized) {
    portENTER_CRITICAL(&mux_);
    pairing_authorized_ = authorized;
    portEXIT_CRITICAL(&mux_);
}

bool BlePeripheralDevice::requestSecureBond(uint32_t passkey) {
    if(passkey < 100000 || passkey > 999999) return false;
    uint8_t remote_address[6]{};
    portENTER_CRITICAL(&mux_);
    if(!connected_ || !pairing_authorized_) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    passkey_ = passkey;
    memcpy(remote_address, remote_address_, sizeof(remote_address));
    portEXIT_CRITICAL(&mux_);
    security_config.setStaticPIN(passkey);
    security_config.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    security_config.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK |
                                          ESP_BLE_ID_KEY_MASK);
    return esp_ble_set_encryption(remote_address,
                                  ESP_BLE_SEC_ENCRYPT_MITM) == ESP_OK;
}

bool BlePeripheralDevice::requestEncryptedLink() {
    uint8_t remote_address[6]{};
    portENTER_CRITICAL(&mux_);
    if(!connected_) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    memcpy(remote_address, remote_address_, sizeof(remote_address));
    portEXIT_CRITICAL(&mux_);
    return esp_ble_set_encryption(remote_address,
                                  ESP_BLE_SEC_ENCRYPT_MITM) == ESP_OK;
}

bool BlePeripheralDevice::encrypted() const {
    portENTER_CRITICAL(&mux_);
    const bool value = encrypted_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

bool BlePeripheralDevice::clearBonds() {
    uint8_t guard = 0;
    while(esp_ble_get_bond_device_num() > 0 && guard < 16) {
        int count = esp_ble_get_bond_device_num();
        if(count <= 0) return true;
        const int batch_count = count > 4 ? 4 : count;
        int requested = batch_count;
        esp_ble_bond_dev_t devices[4]{};
        if(esp_ble_get_bond_device_list(&requested, devices) != ESP_OK) {
            return false;
        }
        if(requested <= 0) return false;
        for(int i = 0; i < requested; ++i) {
            if(esp_ble_remove_bond_device(devices[i].bd_addr) != ESP_OK) {
                return false;
            }
        }
        ++guard;
    }
    return esp_ble_get_bond_device_num() == 0;
}

void BlePeripheralDevice::handleConnected(uint16_t connection_id,
                                          const uint8_t * remote_address) {
    portENTER_CRITICAL(&mux_);
    connection_id_ = connection_id;
    negotiated_mtu_ = 23;
    connected_ = true;
    throughput_mode_ = true;
    encrypted_ = false;
    if(remote_address) memcpy(remote_address_, remote_address, sizeof(remote_address_));
    portEXIT_CRITICAL(&mux_);
}

void BlePeripheralDevice::handleDisconnected() {
    portENTER_CRITICAL(&mux_);
    connected_ = false;
    negotiated_mtu_ = 23;
    throughput_mode_ = false;
    pairing_authorized_ = false;
    encrypted_ = false;
    portEXIT_CRITICAL(&mux_);
}

void BlePeripheralDevice::handleMtuChanged(uint16_t mtu) {
    portENTER_CRITICAL(&mux_);
    negotiated_mtu_ = mtu < 23 ? 23 : mtu;
    portEXIT_CRITICAL(&mux_);
}

void BlePeripheralDevice::handleReceive(const uint8_t * data, size_t length) {
    ReceiveCallback callback = nullptr;
    portENTER_CRITICAL(&mux_);
    callback = receive_callback_;
    portEXIT_CRITICAL(&mux_);
    if(callback && data && length > 0) callback(data, length);
}

void BlePeripheralDevice::handleSecurityResult(bool success) {
    SecurityCallback callback = nullptr;
    portENTER_CRITICAL(&mux_);
    encrypted_ = success;
    pairing_authorized_ = false;
    callback = security_callback_;
    portEXIT_CRITICAL(&mux_);
    if(callback) callback(success);
}

uint32_t BlePeripheralDevice::passkey() const {
    portENTER_CRITICAL(&mux_);
    const uint32_t value = passkey_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

bool BlePeripheralDevice::pairingAuthorized() const {
    portENTER_CRITICAL(&mux_);
    const bool value = pairing_authorized_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

}  // namespace firefly
