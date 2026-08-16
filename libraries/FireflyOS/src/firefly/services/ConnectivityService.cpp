#include "ConnectivityService.h"

#include <string.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

namespace firefly {

bool MessageAuthenticator::isSensitive(protocol::MessageType type) {
    switch(type) {
        case protocol::MessageType::DeviceState:
        case protocol::MessageType::SettingsGet:
        case protocol::MessageType::SettingsSet:
        case protocol::MessageType::NotificationPush:
        case protocol::MessageType::NotificationDismiss:
        case protocol::MessageType::WeatherUpdate:
        case protocol::MessageType::CalendarUpdate:
        case protocol::MessageType::MediaCommand:
        case protocol::MessageType::FindPhone:
        case protocol::MessageType::FindWatch:
        case protocol::MessageType::WifiProvision:
        case protocol::MessageType::BulkTransfer:
        case protocol::MessageType::OtaControl:
        case protocol::MessageType::UnpairRequest:
        case protocol::MessageType::UnpairConfirm:
            return true;
        default:
            return false;
    }
}

bool MessageAuthenticator::appendTag(
    protocol::Frame & frame,
    const uint8_t token[kAppTokenSize]) {
    if(!isSensitive(frame.type)) return true;
    if(frame.payload_length > protocol::kMaxPayload - kAuthTagSize) return false;
    uint8_t tag[kAuthTagSize]{};
    if(!calculate(frame, frame.payload_length, token, tag)) return false;
    memcpy(frame.payload + frame.payload_length, tag, sizeof(tag));
    frame.payload_length = static_cast<uint16_t>(frame.payload_length + sizeof(tag));
    return true;
}

bool MessageAuthenticator::verifyAndStrip(
    protocol::Frame & frame,
    const uint8_t token[kAppTokenSize]) {
    if(!isSensitive(frame.type)) return true;
    if(frame.payload_length < kAuthTagSize) return false;
    const uint16_t body_length = static_cast<uint16_t>(
        frame.payload_length - kAuthTagSize
    );
    uint8_t expected[kAuthTagSize]{};
    if(!calculate(frame, body_length, token, expected)) return false;
    uint8_t difference = 0;
    for(size_t i = 0; i < kAuthTagSize; ++i) {
        difference |= static_cast<uint8_t>(
            expected[i] ^ frame.payload[body_length + i]
        );
    }
    if(difference != 0) return false;
    memset(frame.payload + body_length, 0, kAuthTagSize);
    frame.payload_length = body_length;
    return true;
}

bool MessageAuthenticator::calculate(
    const protocol::Frame & frame,
    uint16_t body_length,
    const uint8_t token[kAppTokenSize],
    uint8_t output[kAuthTagSize]) {
    if(token == nullptr || output == nullptr || body_length > frame.payload_length ||
       body_length > protocol::kMaxPayload) {
        return false;
    }
    uint8_t header[7]{};
    header[0] = protocol::kProtocolVersion;
    header[1] = static_cast<uint8_t>(frame.type);
    header[2] = frame.flags;
    header[3] = static_cast<uint8_t>(frame.sequence & 0xFF);
    header[4] = static_cast<uint8_t>((frame.sequence >> 8) & 0xFF);
    header[5] = static_cast<uint8_t>(body_length & 0xFF);
    header[6] = static_cast<uint8_t>((body_length >> 8) & 0xFF);

    uint8_t inner_pad[64]{};
    uint8_t outer_pad[64]{};
    for(size_t i = 0; i < sizeof(inner_pad); ++i) {
        const uint8_t key = i < kAppTokenSize ? token[i] : 0;
        inner_pad[i] = static_cast<uint8_t>(key ^ 0x36);
        outer_pad[i] = static_cast<uint8_t>(key ^ 0x5C);
    }
    uint8_t inner_digest[32]{};
    uint8_t digest[32]{};
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    int result = mbedtls_sha256_starts_ret(&context, 0);
    if(result == 0) {
        result = mbedtls_sha256_update_ret(&context, inner_pad,
                                           sizeof(inner_pad));
    }
    if(result == 0) {
        result = mbedtls_sha256_update_ret(&context, header, sizeof(header));
    }
    if(result == 0 && body_length > 0) {
        result = mbedtls_sha256_update_ret(&context, frame.payload, body_length);
    }
    if(result == 0) {
        result = mbedtls_sha256_finish_ret(&context, inner_digest);
    }
    if(result == 0) result = mbedtls_sha256_starts_ret(&context, 0);
    if(result == 0) {
        result = mbedtls_sha256_update_ret(&context, outer_pad,
                                           sizeof(outer_pad));
    }
    if(result == 0) {
        result = mbedtls_sha256_update_ret(&context, inner_digest,
                                           sizeof(inner_digest));
    }
    if(result == 0) result = mbedtls_sha256_finish_ret(&context, digest);
    mbedtls_sha256_free(&context);
    if(result == 0) memcpy(output, digest, kAuthTagSize);
    memset(inner_pad, 0, sizeof(inner_pad));
    memset(outer_pad, 0, sizeof(outer_pad));
    memset(inner_digest, 0, sizeof(inner_digest));
    memset(digest, 0, sizeof(digest));
    return result == 0;
}

ConnectivityService * ConnectivityService::active_ = nullptr;

ConnectivityService::ConnectivityService(BlePeripheralTransport & transport,
                                         EventBus & events,
                                         StateStore & state,
                                         PairingStore & pairing_store)
    : transport_(transport),
      events_(events),
      state_(state),
      pairing_store_(pairing_store) {}

ConnectivityService::~ConnectivityService() {
    if(active_ == this) active_ = nullptr;
}

bool ConnectivityService::isStrictAckFrame(const protocol::Frame & frame) {
    return frame.type == protocol::MessageType::Ack &&
        frame.flags == protocol::FrameFlag::IsAck &&
        frame.payload_length == 0;
}

bool ConnectivityService::isMalformedAckFrame(const protocol::Frame & frame) {
    const bool has_ack_type = frame.type == protocol::MessageType::Ack;
    const bool has_ack_flag =
        (frame.flags & protocol::FrameFlag::IsAck) != 0;
    return (has_ack_type || has_ack_flag) && !isStrictAckFrame(frame);
}

bool ConnectivityService::begin(const char * device_name,
                                bool paired,
                                uint32_t now_ms) {
    if(initialized_ || active_ != nullptr) return false;
    (void)paired;
    PairingRecord stored{};
    if(!pairing_store_.loadPairing(stored)) stored = PairingRecord{};
    const bool provisional_pairing = stored.valid && !stored.confirmed;
    pairing_record_ = provisional_pairing ? PairingRecord{} : stored;
    paired_ = stored.valid && stored.confirmed;
    pairing_state_ = paired_ ? PairingState::Paired : PairingState::Idle;
    active_ = this;
    transport_.setSecurityCallback(&ConnectivityService::securityThunk);
    if(!transport_.begin(device_name, &ConnectivityService::receiveThunk)) {
        active_ = nullptr;
        return false;
    }
    if(provisional_pairing) {
        const bool bonds_cleared = transport_.clearBonds();
        const bool record_cleared =
            bonds_cleared && pairing_store_.clearPairing();
        if(!record_cleared) pairing_state_ = PairingState::Failed;
    }
    advertising_started_ms_ = now_ms;
    last_activity_ms_ = now_ms;
    advertising_slow_ = false;
    transport_.advertise(kFastAdvertisingIntervalMs);
    initialized_ = true;
    return true;
}

void ConnectivityService::service(uint32_t now_ms) {
    if(!initialized_) return;
    updateConnection(now_ms);
    updateAdvertising(now_ms);

    ReceiveSlot slot{};
    while(takeQueued(slot)) {
        last_activity_ms_ = now_ms;
        transport_.setThroughputMode(true);
        processEncoded(slot.bytes, slot.length, now_ms);
    }

    bool security_pending = false;
    bool security_success = false;
    portENTER_CRITICAL(&security_mux_);
    security_pending = security_result_pending_;
    security_success = security_result_success_;
    security_result_pending_ = false;
    portEXIT_CRITICAL(&security_mux_);
    if(security_pending) handleSecurityResult(security_success, now_ms);

    if(connected_ && !sync_session_active_ &&
       static_cast<uint32_t>(now_ms - last_activity_ms_) >= kConnectionIdleMs) {
        transport_.setThroughputMode(false);
    }
    updateAckRetry(now_ms);
}

void ConnectivityService::setSyncSessionActive(bool active, uint32_t now_ms) {
    sync_session_active_ = active;
    last_activity_ms_ = now_ms;
    if(connected_) transport_.setThroughputMode(active);
}

void ConnectivityService::setRandomBytesCallback(RandomBytesCallback callback) {
    random_bytes_callback_ = callback;
}

bool ConnectivityService::send(const protocol::Frame & frame, uint32_t now_ms) {
    if(!connected_) return false;
    if(isMalformedAckFrame(frame)) return false;
    const bool is_ack = isStrictAckFrame(frame);
    if(!is_ack &&
       (frame.flags & protocol::FrameFlag::AckRequired) != 0 &&
       pending_ack_active_) {
        return false;
    }
    protocol::Frame authenticated = frame;
    if(MessageAuthenticator::isSensitive(authenticated.type)) {
        uint8_t token[MessageAuthenticator::kAppTokenSize]{};
        portENTER_CRITICAL(&pairing_mux_);
        const bool has_pairing = paired_ && pairing_record_.valid;
        memcpy(token, pairing_record_.app_token, sizeof(token));
        portEXIT_CRITICAL(&pairing_mux_);
        const bool valid = has_pairing && transport_.encrypted() &&
            MessageAuthenticator::appendTag(authenticated, token);
        memset(token, 0, sizeof(token));
        if(!valid) {
            return false;
        }
    }
    if(!notifyOutboundFrame(authenticated)) return false;
    last_activity_ms_ = now_ms;
    if((authenticated.flags & protocol::FrameFlag::AckRequired) != 0) {
        pending_ack_frame_ = authenticated;
        pending_ack_sequence_ = authenticated.sequence;
        pending_ack_sent_ms_ = now_ms;
        pending_ack_retries_ = 0;
        pending_ack_active_ = true;
    }
    return true;
}

uint16_t ConnectivityService::allocateOutgoingSequence() {
    const uint16_t sequence = outgoing_sequence_;
    ++outgoing_sequence_;
    if(outgoing_sequence_ == 0) outgoing_sequence_ = 1;
    return sequence;
}

bool ConnectivityService::enqueueReceived(const uint8_t * data, size_t length) {
    if(data == nullptr || length == 0 || length > protocol::kMaxAttChunk) return false;
    portENTER_CRITICAL(&rx_mux_);
    if(receive_count_ >= kReceiveQueueCapacity) {
        portEXIT_CRITICAL(&rx_mux_);
        return false;
    }
    ReceiveSlot & slot = receive_queue_[receive_tail_];
    memcpy(slot.bytes, data, length);
    slot.length = static_cast<uint16_t>(length);
    receive_tail_ = static_cast<uint8_t>((receive_tail_ + 1) % kReceiveQueueCapacity);
    ++receive_count_;
    portEXIT_CRITICAL(&rx_mux_);
    return true;
}

bool ConnectivityService::takeReceivedFrame(protocol::Frame & frame) {
    portENTER_CRITICAL(&dispatch_mux_);
    if(dispatch_count_ == 0) {
        portEXIT_CRITICAL(&dispatch_mux_);
        return false;
    }
    frame = dispatch_queue_[dispatch_head_];
    dispatch_queue_[dispatch_head_] = protocol::Frame{};
    dispatch_head_ = static_cast<uint8_t>(
        (dispatch_head_ + 1) % kDispatchQueueCapacity
    );
    --dispatch_count_;
    portEXIT_CRITICAL(&dispatch_mux_);
    return true;
}

PairingSnapshot ConnectivityService::pairingSnapshot() const {
    PairingSnapshot snapshot{};
    portENTER_CRITICAL(&pairing_mux_);
    snapshot.state = pairing_state_;
    snapshot.passkey = pairing_passkey_;
    const char * name = paired_
        ? pairing_record_.phone_name
        : pending_phone_name_;
    strlcpy(snapshot.phone_name, name, sizeof(snapshot.phone_name));
    portEXIT_CRITICAL(&pairing_mux_);
    return snapshot;
}

bool ConnectivityService::paired() const {
    portENTER_CRITICAL(&pairing_mux_);
    const bool value = paired_;
    portEXIT_CRITICAL(&pairing_mux_);
    return value;
}

bool ConnectivityService::confirmPairing(bool allow, uint32_t now_ms) {
    portENTER_CRITICAL(&pairing_mux_);
    if(pairing_state_ != PairingState::AwaitingUser) {
        portEXIT_CRITICAL(&pairing_mux_);
        return false;
    }
    const uint32_t passkey = pairing_passkey_;
    pairing_state_ = allow ? PairingState::Securing : PairingState::Failed;
    portEXIT_CRITICAL(&pairing_mux_);

    transport_.authorizePairing(allow);
    if(!allow) {
        postPairingEvent(EventType::PairingResult, 0, now_ms);
        transport_.disconnect();
        return true;
    }
    if(!transport_.requestSecureBond(passkey)) {
        portENTER_CRITICAL(&pairing_mux_);
        pairing_state_ = PairingState::Failed;
        portEXIT_CRITICAL(&pairing_mux_);
        postPairingEvent(EventType::PairingResult, 0, now_ms);
        transport_.disconnect();
        return false;
    }
    return true;
}

bool ConnectivityService::requestUnpairConfirmation(uint32_t now_ms) {
    portENTER_CRITICAL(&pairing_mux_);
    if(!paired_ || pairing_state_ == PairingState::AwaitingUnpairConfirmation) {
        portEXIT_CRITICAL(&pairing_mux_);
        return false;
    }
    pairing_state_ = PairingState::AwaitingUnpairConfirmation;
    portEXIT_CRITICAL(&pairing_mux_);
    postPairingEvent(EventType::UnpairConfirmationRequested, 0, now_ms);
    return true;
}

bool ConnectivityService::confirmUnpair(bool confirm, uint32_t now_ms) {
    portENTER_CRITICAL(&pairing_mux_);
    if(pairing_state_ != PairingState::AwaitingUnpairConfirmation) {
        portEXIT_CRITICAL(&pairing_mux_);
        return false;
    }
    if(!confirm) {
        pairing_state_ = PairingState::Paired;
        portEXIT_CRITICAL(&pairing_mux_);
        return true;
    }
    portEXIT_CRITICAL(&pairing_mux_);

    protocol::Frame confirmation{};
    confirmation.type = protocol::MessageType::UnpairConfirm;
    confirmation.flags = protocol::FrameFlag::AckRequired;
    confirmation.sequence = allocateOutgoingSequence();
    if(!send(confirmation, now_ms)) {
        portENTER_CRITICAL(&pairing_mux_);
        pairing_state_ = PairingState::Paired;
        portEXIT_CRITICAL(&pairing_mux_);
        return false;
    }
    portENTER_CRITICAL(&pairing_mux_);
    pairing_state_ = PairingState::AwaitingUnpairAck;
    portEXIT_CRITICAL(&pairing_mux_);
    pending_ack_purpose_ = PendingAckPurpose::UnpairConfirm;
    return true;
}

void ConnectivityService::receiveThunk(const uint8_t * data, size_t length) {
    if(active_ && !active_->enqueueReceived(data, length)) {
        active_->publishError(ConnectivityError::ReceiveQueueFull,
                              active_->last_activity_ms_);
    }
}

void ConnectivityService::securityThunk(bool success) {
    if(!active_) return;
    portENTER_CRITICAL(&active_->security_mux_);
    active_->security_result_success_ = success;
    active_->security_result_pending_ = true;
    portEXIT_CRITICAL(&active_->security_mux_);
}

bool ConnectivityService::takeQueued(ReceiveSlot & slot) {
    portENTER_CRITICAL(&rx_mux_);
    if(receive_count_ == 0) {
        portEXIT_CRITICAL(&rx_mux_);
        return false;
    }
    slot = receive_queue_[receive_head_];
    receive_head_ = static_cast<uint8_t>((receive_head_ + 1) % kReceiveQueueCapacity);
    --receive_count_;
    portEXIT_CRITICAL(&rx_mux_);
    return true;
}

void ConnectivityService::updateConnection(uint32_t now_ms) {
    const bool is_connected = transport_.connected();
    if(is_connected == connected_) return;
    connected_ = is_connected;
    state_.setPhoneConnected(is_connected);
    events_.post(SystemEvent(EventType::PhoneConnectionChanged,
                             is_connected ? 1U : 0U, now_ms,
                             EventPriority::Normal));
    if(is_connected) {
        transport_.stopAdvertising();
        last_activity_ms_ = now_ms;
        transport_.setThroughputMode(true);
        if(paired()) {
            transport_.authorizePairing(true);
            transport_.requestEncryptedLink();
        }
        return;
    }

    authentication_failures_ = 0;
    portENTER_CRITICAL(&pairing_mux_);
    const bool pairing_interrupted =
        pairing_state_ == PairingState::Securing ||
        pairing_state_ == PairingState::AwaitingPairConfirmAck;
    const bool unpair_interrupted =
        pairing_state_ == PairingState::AwaitingUnpairAck;
    if(pairing_interrupted) pairing_state_ = PairingState::Failed;
    else if(unpair_interrupted) pairing_state_ = PairingState::Paired;
    portEXIT_CRITICAL(&pairing_mux_);
    if(pairing_interrupted) {
        rollbackPendingPairing(now_ms);
    } else if(unpair_interrupted) {
        postPairingEvent(EventType::PairingUnbound, 0, now_ms);
    }
    clearSessionBuffers();
    advertising_started_ms_ = now_ms;
    advertising_slow_ = false;
    transport_.advertise(kFastAdvertisingIntervalMs);
}

void ConnectivityService::updateAdvertising(uint32_t now_ms) {
    if(connected_ || advertising_slow_) return;
    const uint32_t fast_window = paired()
        ? kPairedFastAdvertisingMs
        : kUnpairedFastAdvertisingMs;
    if(static_cast<uint32_t>(now_ms - advertising_started_ms_) >= fast_window) {
        transport_.advertise(kSlowAdvertisingIntervalMs);
        advertising_slow_ = true;
    }
}

void ConnectivityService::updateAckRetry(uint32_t now_ms) {
    if(!pending_ack_active_ || !connected_) return;
    if(static_cast<uint32_t>(now_ms - pending_ack_sent_ms_) < kAckTimeoutMs) return;
    if(pending_ack_retries_ >= kMaxRetries) {
        const PendingAckPurpose purpose = pending_ack_purpose_;
        pending_ack_active_ = false;
        pending_ack_purpose_ = PendingAckPurpose::None;
        pending_ack_frame_ = protocol::Frame{};
        publishError(ConnectivityError::AckTimeout, now_ms);
        if(purpose == PendingAckPurpose::PairConfirm) {
            rollbackPendingPairing(now_ms);
        } else if(purpose == PendingAckPurpose::UnpairConfirm) {
            portENTER_CRITICAL(&pairing_mux_);
            pairing_state_ = PairingState::Paired;
            portEXIT_CRITICAL(&pairing_mux_);
            postPairingEvent(EventType::PairingUnbound, 0, now_ms);
        }
        return;
    }
    notifyOutboundFrame(pending_ack_frame_);
    ++pending_ack_retries_;
    pending_ack_sent_ms_ = now_ms;
}

void ConnectivityService::handleAcknowledgement(uint16_t sequence,
                                                uint32_t now_ms) {
    if(!pending_ack_active_ || sequence != pending_ack_sequence_) return;
    const PendingAckPurpose purpose = pending_ack_purpose_;
    pending_ack_active_ = false;
    pending_ack_purpose_ = PendingAckPurpose::None;
    pending_ack_frame_ = protocol::Frame{};
    if(purpose == PendingAckPurpose::PairConfirm) {
        finalizePairing(now_ms);
    } else if(purpose == PendingAckPurpose::UnpairConfirm) {
        finalizeUnpair(now_ms);
    }
}

void ConnectivityService::processEncoded(const uint8_t * data,
                                         size_t length,
                                         uint32_t now_ms) {
    protocol::Frame frame{};
    const protocol::DecodeError error = protocol::FrameCodec::decode(data, length, frame);
    if(error != protocol::DecodeError::None) {
        publishError(mapDecodeError(error), now_ms);
        return;
    }
    if(!protocol::isKnownMessageType(data[3])) {
        publishError(ConnectivityError::UnknownMessageType, now_ms);
        sendProtocolError(
            static_cast<protocol::MessageType>(data[3]),
            protocol::WireErrorCode::InvalidPayload,
            now_ms);
        return;
    }
    processFrame(frame, now_ms);
}

void ConnectivityService::processFrame(protocol::Frame & frame,
                                       uint32_t now_ms) {
    if((frame.flags & protocol::FrameFlag::Fragment) != 0) {
        processFragment(frame, now_ms);
        return;
    }
    if(reassembly_active_) {
        reassembly_active_ = false;
        publishError(ConnectivityError::FragmentInvalid, now_ms);
    }
    processCompleteFrame(frame, now_ms);
}

void ConnectivityService::processCompleteFrame(protocol::Frame & frame,
                                               uint32_t now_ms) {
    if(isMalformedAckFrame(frame)) {
        publishError(ConnectivityError::InvalidAck, now_ms);
        sendProtocolError(frame.type, protocol::WireErrorCode::InvalidPayload,
                          now_ms);
        return;
    }
    if(isStrictAckFrame(frame)) {
        handleAcknowledgement(frame.sequence, now_ms);
        return;
    }

    const bool ack_required = frame.type == protocol::MessageType::Hello ||
        (frame.flags & protocol::FrameFlag::AckRequired) != 0;
    const bool sensitive = MessageAuthenticator::isSensitive(frame.type);
    if(sensitive && !authenticateAndStrip(frame, now_ms)) return;
    if(!sequenceIsFresh(frame.sequence)) {
        publishError(ConnectivityError::DuplicateSequence, now_ms);
        if(ack_required) sendAck(frame.sequence, now_ms);
        return;
    }

    if(frame.type == protocol::MessageType::PairRequest) {
        if(!handlePairRequest(frame, now_ms)) {
            publishError(ConnectivityError::Unauthorized, now_ms);
            return;
        }
        latest_received_sequence_ = frame.sequence;
        has_received_sequence_ = true;
        if(ack_required) sendAck(frame.sequence, now_ms);
        return;
    }

    if(!sensitive && !authenticateAndStrip(frame, now_ms)) return;
    if(frame.type == protocol::MessageType::UnpairRequest) {
        if(!requestUnpairConfirmation(now_ms)) return;
        latest_received_sequence_ = frame.sequence;
        has_received_sequence_ = true;
        if(ack_required) sendAck(frame.sequence, now_ms);
        return;
    }
    if(!publishFrame(frame, now_ms)) return;
    latest_received_sequence_ = frame.sequence;
    has_received_sequence_ = true;
    if(ack_required) sendAck(frame.sequence, now_ms);
}

bool ConnectivityService::processFragment(const protocol::Frame & frame,
                                          uint32_t now_ms) {
    if(frame.payload_length < protocol::kFragmentMetadataSize) {
        publishError(ConnectivityError::FragmentInvalid, now_ms);
        reassembly_active_ = false;
        return false;
    }
    const uint8_t index = frame.payload[0];
    const uint8_t count = frame.payload[1];
    const uint16_t data_length = static_cast<uint16_t>(
        frame.payload_length - protocol::kFragmentMetadataSize
    );
    if(count == 0 || count > protocol::kMaxFragmentCount || index >= count) {
        publishError(ConnectivityError::FragmentInvalid, now_ms);
        reassembly_active_ = false;
        return false;
    }
    if(index == 0) {
        reassembly_active_ = true;
        reassembly_type_ = frame.type;
        reassembly_sequence_ = frame.sequence;
        reassembly_count_ = count;
        reassembly_next_index_ = 0;
        reassembly_length_ = 0;
    }
    const bool metadata_matches = reassembly_active_ &&
        frame.type == reassembly_type_ &&
        frame.sequence == reassembly_sequence_ &&
        count == reassembly_count_ && index == reassembly_next_index_;
    const bool is_last =
        (frame.flags & protocol::FrameFlag::LastFragment) != 0;
    if(!metadata_matches || is_last != (index + 1 == count) ||
       static_cast<size_t>(reassembly_length_) + data_length > protocol::kMaxPayload) {
        publishError(ConnectivityError::FragmentInvalid, now_ms);
        reassembly_active_ = false;
        return false;
    }
    if(data_length > 0) {
        memcpy(reassembly_payload_ + reassembly_length_,
               frame.payload + protocol::kFragmentMetadataSize,
               data_length);
        reassembly_length_ = static_cast<uint16_t>(reassembly_length_ + data_length);
    }
    ++reassembly_next_index_;
    if(!is_last) return true;

    protocol::Frame complete{};
    complete.type = reassembly_type_;
    complete.flags = static_cast<uint8_t>(
        frame.flags & ~(protocol::FrameFlag::Fragment |
                        protocol::FrameFlag::LastFragment)
    );
    complete.sequence = reassembly_sequence_;
    complete.payload_length = reassembly_length_;
    if(reassembly_length_ > 0) {
        memcpy(complete.payload, reassembly_payload_, reassembly_length_);
    }
    reassembly_active_ = false;
    processCompleteFrame(complete, now_ms);
    return true;
}

bool ConnectivityService::sequenceIsFresh(uint16_t sequence) const {
    if(!has_received_sequence_) return true;
    const uint16_t delta = static_cast<uint16_t>(sequence - latest_received_sequence_);
    return delta != 0 && delta < 0x8000;
}

bool ConnectivityService::authenticateAndStrip(protocol::Frame & frame,
                                               uint32_t now_ms) {
    if(MessageAuthenticator::isSensitive(frame.type)) {
        uint8_t token[MessageAuthenticator::kAppTokenSize]{};
        portENTER_CRITICAL(&pairing_mux_);
        const bool has_pairing = paired_ && pairing_record_.valid;
        memcpy(token, pairing_record_.app_token, sizeof(token));
        portEXIT_CRITICAL(&pairing_mux_);
        const bool valid = has_pairing && transport_.encrypted() &&
            MessageAuthenticator::verifyAndStrip(frame, token);
        memset(token, 0, sizeof(token));
        if(valid) {
            authentication_failures_ = 0;
            return true;
        }
        if(authentication_failures_ < UINT8_MAX) ++authentication_failures_;
        publishError(ConnectivityError::Unauthorized, now_ms);
        sendProtocolError(frame.type, protocol::WireErrorCode::Unauthorized,
                          now_ms);
        if(authentication_failures_ >= kMaxAuthenticationFailures) {
            transport_.disconnect();
        }
        return false;
    }
    switch(frame.type) {
        case protocol::MessageType::Hello:
        case protocol::MessageType::PairConfirm:
        case protocol::MessageType::Error:
            return true;
        default:
            publishError(ConnectivityError::Unauthorized, now_ms);
            sendProtocolError(frame.type, protocol::WireErrorCode::Unauthorized,
                              now_ms);
            return false;
    }
}

bool ConnectivityService::handlePairRequest(const protocol::Frame & frame,
                                            uint32_t now_ms) {
    if(paired() || !connected_ || frame.payload_length == 0) return false;
    char phone_name[33]{};
    const size_t copy_length = frame.payload_length < sizeof(phone_name) - 1
        ? frame.payload_length
        : sizeof(phone_name) - 1;
    for(size_t i = 0; i < copy_length; ++i) {
        const uint8_t value = frame.payload[i];
        phone_name[i] = value >= 0x20 && value != 0x7F
            ? static_cast<char>(value)
            : '?';
    }
    phone_name[copy_length] = '\0';
    if(phone_name[0] == '\0') return false;
    const uint32_t generated_passkey = generatePasskey();

    portENTER_CRITICAL(&pairing_mux_);
    if(pairing_state_ == PairingState::AwaitingUser ||
       pairing_state_ == PairingState::Securing) {
        portEXIT_CRITICAL(&pairing_mux_);
        return false;
    }
    strlcpy(pending_phone_name_, phone_name, sizeof(pending_phone_name_));
    pairing_passkey_ = generated_passkey;
    pairing_state_ = PairingState::AwaitingUser;
    const uint32_t passkey = pairing_passkey_;
    portEXIT_CRITICAL(&pairing_mux_);
    postPairingEvent(EventType::PairingRequested, passkey, now_ms);
    return true;
}

void ConnectivityService::handleSecurityResult(bool success,
                                               uint32_t now_ms) {
    portENTER_CRITICAL(&pairing_mux_);
    const PairingState state = pairing_state_;
    portEXIT_CRITICAL(&pairing_mux_);
    if(state != PairingState::Securing) {
        if(!success && paired()) transport_.disconnect();
        return;
    }
    if(!success || !transport_.encrypted()) {
        portENTER_CRITICAL(&pairing_mux_);
        pairing_state_ = PairingState::Failed;
        portEXIT_CRITICAL(&pairing_mux_);
        postPairingEvent(EventType::PairingResult, 0, now_ms);
        transport_.disconnect();
        return;
    }

    PairingRecord record{};
    record.valid = true;
    record.confirmed = false;
    fillRandom(record.app_token, sizeof(record.app_token));
    portENTER_CRITICAL(&pairing_mux_);
    strlcpy(record.phone_name, pending_phone_name_, sizeof(record.phone_name));
    portEXIT_CRITICAL(&pairing_mux_);
    if(!pairing_store_.savePairing(record)) {
        memset(record.app_token, 0, sizeof(record.app_token));
        transport_.clearBonds();
        portENTER_CRITICAL(&pairing_mux_);
        pairing_state_ = PairingState::Failed;
        portEXIT_CRITICAL(&pairing_mux_);
        postPairingEvent(EventType::PairingResult, 0, now_ms);
        transport_.disconnect();
        return;
    }

    protocol::Frame confirmation{};
    confirmation.type = protocol::MessageType::PairConfirm;
    confirmation.flags = protocol::FrameFlag::AckRequired;
    confirmation.sequence = allocateOutgoingSequence();
    confirmation.payload_length = sizeof(record.app_token);
    memcpy(confirmation.payload, record.app_token, sizeof(record.app_token));
    if(!send(confirmation, now_ms)) {
        const bool bonds_cleared = transport_.clearBonds();
        if(bonds_cleared) pairing_store_.clearPairing();
        memset(record.app_token, 0, sizeof(record.app_token));
        portENTER_CRITICAL(&pairing_mux_);
        pairing_state_ = PairingState::Failed;
        portEXIT_CRITICAL(&pairing_mux_);
        postPairingEvent(EventType::PairingResult, 0, now_ms);
        transport_.disconnect();
        return;
    }

    portENTER_CRITICAL(&pairing_mux_);
    pairing_record_ = record;
    paired_ = false;
    pairing_state_ = PairingState::AwaitingPairConfirmAck;
    portEXIT_CRITICAL(&pairing_mux_);
    pending_ack_purpose_ = PendingAckPurpose::PairConfirm;
    memset(record.app_token, 0, sizeof(record.app_token));
}

void ConnectivityService::finalizePairing(uint32_t now_ms) {
    PairingRecord confirmed_record{};
    portENTER_CRITICAL(&pairing_mux_);
    if(pairing_state_ != PairingState::AwaitingPairConfirmAck ||
       !pairing_record_.valid) {
        portEXIT_CRITICAL(&pairing_mux_);
        return;
    }
    confirmed_record = pairing_record_;
    confirmed_record.confirmed = true;
    portEXIT_CRITICAL(&pairing_mux_);
    if(!pairing_store_.savePairing(confirmed_record)) {
        memset(confirmed_record.app_token, 0,
               sizeof(confirmed_record.app_token));
        rollbackPendingPairing(now_ms);
        return;
    }
    portENTER_CRITICAL(&pairing_mux_);
    pairing_record_ = confirmed_record;
    paired_ = true;
    pairing_state_ = PairingState::Paired;
    pairing_passkey_ = 0;
    memset(pending_phone_name_, 0, sizeof(pending_phone_name_));
    portEXIT_CRITICAL(&pairing_mux_);
    memset(confirmed_record.app_token, 0,
           sizeof(confirmed_record.app_token));
    postPairingEvent(EventType::PairingResult, 1, now_ms);
}

void ConnectivityService::rollbackPendingPairing(uint32_t now_ms) {
    const bool bonds_cleared = transport_.clearBonds();
    if(bonds_cleared) pairing_store_.clearPairing();
    portENTER_CRITICAL(&pairing_mux_);
    pairing_record_ = PairingRecord{};
    paired_ = false;
    pairing_state_ = PairingState::Failed;
    pairing_passkey_ = 0;
    memset(pending_phone_name_, 0, sizeof(pending_phone_name_));
    portEXIT_CRITICAL(&pairing_mux_);
    postPairingEvent(EventType::PairingResult, 0, now_ms);
    transport_.disconnect();
}

bool ConnectivityService::finalizeUnpair(uint32_t now_ms) {
    portENTER_CRITICAL(&pairing_mux_);
    const bool awaiting =
        pairing_state_ == PairingState::AwaitingUnpairAck;
    portEXIT_CRITICAL(&pairing_mux_);
    if(!awaiting) return false;
    const bool bonds_cleared = transport_.clearBonds();
    if(!bonds_cleared) {
        portENTER_CRITICAL(&pairing_mux_);
        pairing_state_ = PairingState::Paired;
        portEXIT_CRITICAL(&pairing_mux_);
        postPairingEvent(EventType::PairingUnbound, 0, now_ms);
        return false;
    }
    if(!pairing_store_.clearPairing()) {
        portENTER_CRITICAL(&pairing_mux_);
        pairing_state_ = PairingState::Paired;
        portEXIT_CRITICAL(&pairing_mux_);
        postPairingEvent(EventType::PairingUnbound, 0, now_ms);
        return false;
    }
    portENTER_CRITICAL(&pairing_mux_);
    pairing_record_ = PairingRecord{};
    memset(pending_phone_name_, 0, sizeof(pending_phone_name_));
    pairing_passkey_ = 0;
    paired_ = false;
    pairing_state_ = PairingState::Idle;
    portEXIT_CRITICAL(&pairing_mux_);
    authentication_failures_ = 0;
    postPairingEvent(EventType::PairingUnbound, 1U, now_ms);
    transport_.disconnect();
    return true;
}

bool ConnectivityService::clearSensitiveState() {
    const bool bonds_cleared = transport_.clearBonds();
    const bool record_cleared = pairing_store_.clearPairing();
    transport_.disconnect();
    clearSessionBuffers();

    portENTER_CRITICAL(&pairing_mux_);
    pairing_record_ = PairingRecord{};
    pairing_state_ = PairingState::Idle;
    pairing_passkey_ = 0;
    memset(pending_phone_name_, 0, sizeof(pending_phone_name_));
    paired_ = false;
    portEXIT_CRITICAL(&pairing_mux_);
    authentication_failures_ = 0;
    connected_ = false;
    return bonds_cleared && record_cleared;
}

void ConnectivityService::postPairingEvent(EventType type,
                                           uint32_t value,
                                           uint32_t now_ms) {
    events_.post(SystemEvent(type, value, now_ms, EventPriority::Critical));
}

uint32_t ConnectivityService::generatePasskey() {
    uint8_t random[4]{};
    fillRandom(random, sizeof(random));
    const uint32_t value = static_cast<uint32_t>(random[0]) |
        (static_cast<uint32_t>(random[1]) << 8) |
        (static_cast<uint32_t>(random[2]) << 16) |
        (static_cast<uint32_t>(random[3]) << 24);
    return 100000U + value % 900000U;
}

void ConnectivityService::fillRandom(uint8_t * output, size_t length) {
    if(!output || length == 0) return;
    if(random_bytes_callback_) random_bytes_callback_(output, length);
    else esp_fill_random(output, length);
}

bool ConnectivityService::notifyOutboundFrame(const protocol::Frame & frame) {
    const size_t att_limit =
        protocol::attChunkLimit(transport_.negotiatedMtu());
    if(att_limit < protocol::kHeaderSize ||
       att_limit > protocol::kMaxAttChunk ||
       frame.payload_length > protocol::kMaxPayload) {
        return false;
    }

    const size_t logical_length =
        protocol::kHeaderSize + frame.payload_length;
    if(logical_length <= att_limit) {
        uint8_t encoded_chunk[protocol::kMaxAttChunk]{};
        const size_t encoded = protocol::FrameCodec::encode(
            frame, encoded_chunk, sizeof(encoded_chunk)
        );
        if(encoded == 0 || encoded > att_limit) return false;
        return transport_.notify(encoded_chunk, encoded);
    }

    if(att_limit <= protocol::kHeaderSize +
                       protocol::kFragmentMetadataSize) {
        return false;
    }
    const size_t data_capacity =
        att_limit - protocol::kHeaderSize - protocol::kFragmentMetadataSize;
    const size_t required =
        (frame.payload_length + data_capacity - 1) / data_capacity;
    if(required == 0 || required > protocol::kMaxFragmentCount) return false;

    const uint8_t base_flags = static_cast<uint8_t>(
        frame.flags & ~(protocol::FrameFlag::Fragment |
                        protocol::FrameFlag::LastFragment)
    );
    for(size_t index = 0; index < required; ++index) {
        const size_t offset = static_cast<size_t>(index) * data_capacity;
        const size_t remaining = frame.payload_length - offset;
        const size_t data_length =
            remaining < data_capacity ? remaining : data_capacity;
        protocol::Frame fragment{};
        fragment.type = frame.type;
        fragment.flags = static_cast<uint8_t>(
            base_flags | protocol::FrameFlag::Fragment |
            (index + 1 == required
                ? protocol::FrameFlag::LastFragment
                : 0)
        );
        fragment.sequence = frame.sequence;
        fragment.payload_length = static_cast<uint16_t>(
            protocol::kFragmentMetadataSize + data_length
        );
        fragment.payload[0] = static_cast<uint8_t>(index);
        fragment.payload[1] = static_cast<uint8_t>(required);
        memcpy(fragment.payload + protocol::kFragmentMetadataSize,
               frame.payload + offset, data_length);
        uint8_t encoded_chunk[protocol::kMaxAttChunk]{};
        const size_t encoded = protocol::FrameCodec::encode(
            fragment, encoded_chunk, sizeof(encoded_chunk)
        );
        if(encoded == 0 || encoded > att_limit ||
           !transport_.notify(encoded_chunk, encoded)) return false;
    }
    return true;
}

bool ConnectivityService::publishFrame(const protocol::Frame & frame,
                                       uint32_t now_ms) {
    portENTER_CRITICAL(&dispatch_mux_);
    if(dispatch_count_ >= kDispatchQueueCapacity) {
        portEXIT_CRITICAL(&dispatch_mux_);
        publishError(ConnectivityError::DispatchBusy, now_ms);
        return false;
    }
    dispatch_queue_[dispatch_tail_] = frame;
    dispatch_tail_ = static_cast<uint8_t>(
        (dispatch_tail_ + 1) % kDispatchQueueCapacity
    );
    ++dispatch_count_;
    portEXIT_CRITICAL(&dispatch_mux_);
    const uint32_t value =
        (static_cast<uint32_t>(static_cast<uint8_t>(frame.type)) << 16) |
        frame.sequence;
    if(!events_.post(SystemEvent(EventType::BleMessageReceived, value, now_ms,
                                 EventPriority::Normal))) {
        portENTER_CRITICAL(&dispatch_mux_);
        dispatch_tail_ = static_cast<uint8_t>(
            (dispatch_tail_ + kDispatchQueueCapacity - 1) %
            kDispatchQueueCapacity
        );
        dispatch_queue_[dispatch_tail_] = protocol::Frame{};
        --dispatch_count_;
        portEXIT_CRITICAL(&dispatch_mux_);
        publishError(ConnectivityError::DispatchBusy, now_ms);
        return false;
    }
    return true;
}

void ConnectivityService::sendAck(uint16_t sequence, uint32_t now_ms) {
    protocol::Frame ack{};
    ack.type = protocol::MessageType::Ack;
    ack.flags = protocol::FrameFlag::IsAck;
    ack.sequence = sequence;
    send(ack, now_ms);
}

void ConnectivityService::sendProtocolError(
    protocol::MessageType failed_type,
    protocol::WireErrorCode code,
    uint32_t now_ms) {
    protocol::Frame error{};
    error.type = protocol::MessageType::Error;
    error.sequence = allocateOutgoingSequence();
    error.payload[0] = 1;
    error.payload[1] = static_cast<uint8_t>(failed_type);
    error.payload[2] = static_cast<uint8_t>(code);
    error.payload_length = 3;
    send(error, now_ms);
}

void ConnectivityService::clearSessionBuffers() {
    portENTER_CRITICAL(&rx_mux_);
    receive_head_ = 0;
    receive_tail_ = 0;
    receive_count_ = 0;
    portEXIT_CRITICAL(&rx_mux_);
    portENTER_CRITICAL(&dispatch_mux_);
    for(uint8_t index = 0; index < kDispatchQueueCapacity; ++index) {
        dispatch_queue_[index] = protocol::Frame{};
    }
    dispatch_head_ = 0;
    dispatch_tail_ = 0;
    dispatch_count_ = 0;
    portEXIT_CRITICAL(&dispatch_mux_);
    reassembly_active_ = false;
    pending_ack_active_ = false;
    pending_ack_frame_ = protocol::Frame{};
    pending_ack_purpose_ = PendingAckPurpose::None;
    has_received_sequence_ = false;
    sync_session_active_ = false;
}

void ConnectivityService::publishError(ConnectivityError error,
                                       uint32_t now_ms) {
    events_.post(SystemEvent(EventType::BleProtocolError,
                             static_cast<uint32_t>(error), now_ms,
                             EventPriority::Normal));
}

ConnectivityError ConnectivityService::mapDecodeError(protocol::DecodeError error) {
    switch(error) {
        case protocol::DecodeError::TooShort:
            return ConnectivityError::DecodeTooShort;
        case protocol::DecodeError::BadMagic:
            return ConnectivityError::DecodeBadMagic;
        case protocol::DecodeError::BadVersion:
            return ConnectivityError::DecodeBadVersion;
        case protocol::DecodeError::TooLarge:
            return ConnectivityError::DecodeTooLarge;
        case protocol::DecodeError::CrcMismatch:
            return ConnectivityError::DecodeCrcMismatch;
        case protocol::DecodeError::None:
        default:
            return ConnectivityError::None;
    }
}

}  // namespace firefly
