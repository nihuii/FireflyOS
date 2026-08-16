#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../core/EventBus.h"
#include "../core/StateStore.h"
#include "../hal/BlePeripheralDevice.h"
#include "../protocol/FrameCodec.h"
#include "StorageService.h"

namespace firefly {

enum class ConnectivityError : uint8_t {
    None,
    DecodeTooShort,
    DecodeBadMagic,
    DecodeBadVersion,
    DecodeTooLarge,
    DecodeCrcMismatch,
    UnknownMessageType,
    DuplicateSequence,
    FragmentInvalid,
    Unauthorized,
    ReceiveQueueFull,
    DispatchBusy,
    AckTimeout,
    InvalidAck
};

enum class PairingState : uint8_t {
    Idle,
    AwaitingUser,
    Securing,
    AwaitingPairConfirmAck,
    Paired,
    Failed,
    AwaitingUnpairConfirmation,
    AwaitingUnpairAck
};

struct PairingSnapshot {
    PairingState state = PairingState::Idle;
    uint32_t passkey = 0;
    char phone_name[33]{};
};

class MessageAuthenticator {
public:
    static constexpr size_t kAppTokenSize = 16;
    static constexpr size_t kAuthTagSize = 8;

    static bool isSensitive(protocol::MessageType type);
    static bool appendTag(protocol::Frame & frame,
                          const uint8_t token[kAppTokenSize]);
    static bool verifyAndStrip(protocol::Frame & frame,
                               const uint8_t token[kAppTokenSize]);

private:
    static bool calculate(const protocol::Frame & frame,
                          uint16_t body_length,
                          const uint8_t token[kAppTokenSize],
                          uint8_t output[kAuthTagSize]);
};

class ConnectivityService {
public:
    using RandomBytesCallback = void (*)(uint8_t *, size_t);

    static constexpr uint32_t kAckTimeoutMs = 2000;
    static constexpr uint8_t kMaxRetries = 3;
    static constexpr uint32_t kUnpairedFastAdvertisingMs = 60000;
    static constexpr uint32_t kPairedFastAdvertisingMs = 20000;
    static constexpr uint32_t kConnectionIdleMs = 30000;
    static constexpr uint16_t kFastAdvertisingIntervalMs = 100;
    static constexpr uint16_t kSlowAdvertisingIntervalMs = 1000;
    static constexpr uint8_t kReceiveQueueCapacity = 4;
    static constexpr uint8_t kDispatchQueueCapacity = 4;
    static constexpr uint8_t kMaxAuthenticationFailures = 5;

    ConnectivityService(BlePeripheralTransport & transport,
                        EventBus & events,
                        StateStore & state,
                        PairingStore & pairing_store);
    ~ConnectivityService();

    bool begin(const char * device_name, bool paired, uint32_t now_ms);
    void service(uint32_t now_ms);
    void setSyncSessionActive(bool active, uint32_t now_ms);
    void setRandomBytesCallback(RandomBytesCallback callback);

    bool send(const protocol::Frame & frame, uint32_t now_ms);
    bool enqueueReceived(const uint8_t * data, size_t length);
    bool takeReceivedFrame(protocol::Frame & frame);
    PairingSnapshot pairingSnapshot() const;
    bool paired() const;
    bool connected() const { return connected_; }
    uint16_t allocateOutgoingSequence();
    bool confirmPairing(bool allow, uint32_t now_ms);
    bool requestUnpairConfirmation(uint32_t now_ms);
    bool confirmUnpair(bool confirm, uint32_t now_ms);
    bool clearSensitiveState();

private:
    enum class PendingAckPurpose : uint8_t {
        None,
        PairConfirm,
        UnpairConfirm
    };

    struct ReceiveSlot {
        uint8_t bytes[protocol::kMaxAttChunk]{};
        uint16_t length = 0;
    };

    static void receiveThunk(const uint8_t * data, size_t length);
    static void securityThunk(bool success);
    bool takeQueued(ReceiveSlot & slot);
    void updateConnection(uint32_t now_ms);
    void updateAdvertising(uint32_t now_ms);
    void updateAckRetry(uint32_t now_ms);
    void handleAcknowledgement(uint16_t sequence, uint32_t now_ms);
    void processEncoded(const uint8_t * data, size_t length, uint32_t now_ms);
    void processFrame(protocol::Frame & frame, uint32_t now_ms);
    void processCompleteFrame(protocol::Frame & frame, uint32_t now_ms);
    bool processFragment(const protocol::Frame & frame, uint32_t now_ms);
    bool sequenceIsFresh(uint16_t sequence) const;
    bool authenticateAndStrip(protocol::Frame & frame, uint32_t now_ms);
    bool handlePairRequest(const protocol::Frame & frame, uint32_t now_ms);
    void handleSecurityResult(bool success, uint32_t now_ms);
    void finalizePairing(uint32_t now_ms);
    void rollbackPendingPairing(uint32_t now_ms);
    bool finalizeUnpair(uint32_t now_ms);
    void postPairingEvent(EventType type, uint32_t value, uint32_t now_ms);
    uint32_t generatePasskey();
    void fillRandom(uint8_t * output, size_t length);
    bool notifyOutboundFrame(const protocol::Frame & frame);
    bool publishFrame(const protocol::Frame & frame, uint32_t now_ms);
    void sendAck(uint16_t sequence, uint32_t now_ms);
    void sendProtocolError(protocol::MessageType failed_type,
                           protocol::WireErrorCode code,
                           uint32_t now_ms);
    void clearSessionBuffers();
    void publishError(ConnectivityError error, uint32_t now_ms);
    static ConnectivityError mapDecodeError(protocol::DecodeError error);
    static bool isStrictAckFrame(const protocol::Frame & frame);
    static bool isMalformedAckFrame(const protocol::Frame & frame);

    static ConnectivityService * active_;
    BlePeripheralTransport & transport_;
    EventBus & events_;
    StateStore & state_;
    PairingStore & pairing_store_;
    mutable portMUX_TYPE rx_mux_ = portMUX_INITIALIZER_UNLOCKED;
    mutable portMUX_TYPE dispatch_mux_ = portMUX_INITIALIZER_UNLOCKED;
    mutable portMUX_TYPE pairing_mux_ = portMUX_INITIALIZER_UNLOCKED;
    mutable portMUX_TYPE security_mux_ = portMUX_INITIALIZER_UNLOCKED;
    ReceiveSlot receive_queue_[kReceiveQueueCapacity]{};
    uint8_t receive_head_ = 0;
    uint8_t receive_tail_ = 0;
    uint8_t receive_count_ = 0;

    RandomBytesCallback random_bytes_callback_ = nullptr;
    protocol::Frame dispatch_queue_[kDispatchQueueCapacity]{};
    uint8_t dispatch_head_ = 0;
    uint8_t dispatch_tail_ = 0;
    uint8_t dispatch_count_ = 0;

    PairingRecord pairing_record_{};
    PairingState pairing_state_ = PairingState::Idle;
    uint32_t pairing_passkey_ = 0;
    char pending_phone_name_[33]{};
    bool security_result_pending_ = false;
    bool security_result_success_ = false;
    uint8_t authentication_failures_ = 0;
    uint16_t outgoing_sequence_ = 1;

    uint8_t reassembly_payload_[protocol::kMaxPayload]{};
    protocol::MessageType reassembly_type_ = protocol::MessageType::Error;
    uint16_t reassembly_sequence_ = 0;
    uint16_t reassembly_length_ = 0;
    uint8_t reassembly_count_ = 0;
    uint8_t reassembly_next_index_ = 0;
    bool reassembly_active_ = false;

    protocol::Frame pending_ack_frame_{};
    uint16_t pending_ack_sequence_ = 0;
    uint32_t pending_ack_sent_ms_ = 0;
    uint8_t pending_ack_retries_ = 0;
    bool pending_ack_active_ = false;
    PendingAckPurpose pending_ack_purpose_ = PendingAckPurpose::None;

    uint16_t latest_received_sequence_ = 0;
    bool has_received_sequence_ = false;
    uint32_t advertising_started_ms_ = 0;
    uint32_t last_activity_ms_ = 0;
    bool paired_ = false;
    bool connected_ = false;
    bool advertising_slow_ = false;
    bool sync_session_active_ = false;
    bool initialized_ = false;
};

}  // namespace firefly
