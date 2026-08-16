#include "UpdateService.h"

#include "UpdateTrustAnchor.h"

#include <string.h>

namespace firefly {

UpdateService::UpdateService(ResourceGovernor & resources,
                             UpdateWriter & writer,
                             uint32_t current_build)
    : resources_(resources), writer_(writer), current_build_(current_build) {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    mbedtls_sha256_init(&sha_context_);
}

UpdateService::~UpdateService() {
    if(mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    cleanup(writer_started_);
    if(sha_started_) {
        mbedtls_sha256_free(&sha_context_);
        sha_started_ = false;
    } else {
        mbedtls_sha256_free(&sha_context_);
    }
    if(mutex_) xSemaphoreGive(mutex_);
}

bool UpdateService::isActive(UpdateState state) {
    return state == UpdateState::Available ||
           state == UpdateState::Downloading ||
           state == UpdateState::Verifying ||
           state == UpdateState::Writing;
}

bool UpdateService::isTerminal(UpdateState state) {
    return state == UpdateState::RebootPending ||
           state == UpdateState::Completed ||
           state == UpdateState::Failed ||
           state == UpdateState::RolledBack;
}

UpdateFailure UpdateService::preflightFailure(const UpdateRuntimeGate & gate) {
    if(!gate.charging &&
       (!gate.battery_valid || gate.battery_percent < 40)) {
        return UpdateFailure::LowPower;
    }
    if(gate.alarm_active) return UpdateFailure::AlarmActive;
    if(gate.music_active || gate.recording_active) {
        return UpdateFailure::AudioBusy;
    }
    if(gate.transfer_active) return UpdateFailure::TransferBusy;
    if(gate.ota_active) return UpdateFailure::OtaBusy;
    return UpdateFailure::None;
}

void UpdateService::setBlocked(UpdateFailure failure) {
    snapshot_ = {};
    snapshot_.state = UpdateState::Blocked;
    snapshot_.failure = failure;
}

bool UpdateService::offer(const UpdateManifest & manifest,
                          UpdateSource & source,
                          const UpdateRuntimeGate & gate,
                          uint32_t now_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if(isActive(snapshot_.state) || snapshot_.state == UpdateState::RebootPending ||
       snapshot_.state == UpdateState::BootChecking ||
       snapshot_.state == UpdateState::RollbackRequested) {
        xSemaphoreGive(mutex_);
        return false;
    }

    cleanup(writer_started_);
    snapshot_ = {};
    source_ = nullptr;
    manifest_ = {};

    UpdateFailure failure = UpdateFailure::None;
    if(manifest.schema != 1 || manifest.size == 0) {
        failure = UpdateFailure::ManifestInvalid;
    } else if(strcmp(manifest.product, "FireflyOS") != 0) {
        failure = UpdateFailure::WrongProduct;
    } else if(manifest.build <= current_build_) {
        failure = UpdateFailure::BuildNotNewer;
    } else if(manifest.min_build > current_build_) {
        failure = UpdateFailure::MinBuildMismatch;
    } else if(manifest.size > kOtaSlotBytes) {
        failure = UpdateFailure::PackageTooLarge;
    } else if(!UpdateManifestCodec::verifySignature(manifest,
                                                    kUpdatePublicKey)) {
        failure = UpdateFailure::SignatureInvalid;
    } else {
        failure = preflightFailure(gate);
    }

    if(failure == UpdateFailure::None) {
        if(resources_.held(ResourceKind::Ota)) {
            failure = UpdateFailure::OtaBusy;
        } else if(resources_.held(ResourceKind::AudioPlayback) ||
                  resources_.held(ResourceKind::AudioRecording)) {
            failure = UpdateFailure::AudioBusy;
        } else if(resources_.held(ResourceKind::WifiTransfer) ||
                  resources_.held(ResourceKind::SdWrite)) {
            failure = UpdateFailure::TransferBusy;
        } else if(!resources_.acquire(ResourceKind::Ota)) {
            failure = resources_.constrained()
                ? UpdateFailure::LowPower
                : UpdateFailure::OtaBusy;
        } else {
            resource_held_ = true;
        }
    }

    if(failure != UpdateFailure::None) {
        setBlocked(failure);
        xSemaphoreGive(mutex_);
        return false;
    }

    manifest_ = manifest;
    source_ = &source;
    snapshot_.state = UpdateState::Available;
    snapshot_.failure = UpdateFailure::None;
    snapshot_.build = manifest.build;
    snapshot_.size = manifest.size;
    strlcpy(snapshot_.version, manifest.version, sizeof(snapshot_.version));
    snapshot_.cancel_allowed = true;
    last_progress_ms_ = now_ms;
    xSemaphoreGive(mutex_);
    return true;
}

bool UpdateService::start(const UpdateRuntimeGate & gate, uint32_t now_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if(snapshot_.state != UpdateState::Available || !source_) {
        xSemaphoreGive(mutex_);
        return false;
    }
    const UpdateFailure gate_failure = preflightFailure(gate);
    if(gate_failure != UpdateFailure::None) {
        fail(gate_failure, false);
        xSemaphoreGive(mutex_);
        return false;
    }

    const UpdateIoResult opened = source_->open(manifest_);
    if(opened != UpdateIoResult::Ok) {
        fail(ioFailure(opened), false);
        xSemaphoreGive(mutex_);
        return false;
    }
    source_open_ = true;
    if(!writer_.begin(manifest_.size)) {
        fail(UpdateFailure::WriteFailed, false);
        xSemaphoreGive(mutex_);
        return false;
    }
    writer_started_ = true;
    if(mbedtls_sha256_starts_ret(&sha_context_, 0) != 0) {
        fail(UpdateFailure::WriteFailed, true);
        xSemaphoreGive(mutex_);
        return false;
    }
    sha_started_ = true;
    snapshot_.state = UpdateState::Downloading;
    snapshot_.cancel_allowed = true;
    last_progress_ms_ = now_ms;
    xSemaphoreGive(mutex_);
    return true;
}

void UpdateService::tick(uint32_t now_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if(snapshot_.state == UpdateState::Downloading) {
        const size_t remaining = manifest_.size - snapshot_.processed;
        uint8_t chunk[kChunkBytes]{};
        size_t received = 0;
        const size_t capacity = remaining < kChunkBytes ? remaining : kChunkBytes;
        const UpdateIoResult result = source_->read(chunk, capacity, received);
        if(result == UpdateIoResult::End) {
            fail(UpdateFailure::ShortPackage, true);
        } else if(result != UpdateIoResult::Ok) {
            fail(ioFailure(result), true);
        } else if(received > capacity) {
            fail(UpdateFailure::OversizedPackage, true);
        } else if(received == 0) {
            if(static_cast<uint32_t>(now_ms - last_progress_ms_) >=
               kStallTimeoutMs) {
                fail(UpdateFailure::Timeout, true);
            }
        } else if(!writer_.write(chunk, received) ||
                  mbedtls_sha256_update_ret(&sha_context_, chunk, received) != 0) {
            fail(UpdateFailure::WriteFailed, true);
        } else {
            snapshot_.processed += static_cast<uint32_t>(received);
            last_progress_ms_ = now_ms;
            updateProgress();
            if(snapshot_.processed == manifest_.size) {
                snapshot_.state = UpdateState::Verifying;
            }
        }
    } else if(snapshot_.state == UpdateState::Verifying) {
        uint8_t extra = 0;
        size_t received = 0;
        const UpdateIoResult result = source_->read(&extra, 1, received);
        if(result == UpdateIoResult::Ok && received > 0) {
            fail(UpdateFailure::OversizedPackage, true);
        } else if(result != UpdateIoResult::End &&
                  !(result == UpdateIoResult::Ok && received == 0)) {
            fail(ioFailure(result), true);
        } else if(result == UpdateIoResult::Ok) {
            if(static_cast<uint32_t>(now_ms - last_progress_ms_) >=
               kStallTimeoutMs) {
                fail(UpdateFailure::Timeout, true);
            }
        } else {
            uint8_t digest[32]{};
            if(mbedtls_sha256_finish_ret(&sha_context_, digest) != 0) {
                fail(UpdateFailure::HashMismatch, true);
            } else {
                sha_started_ = false;
                uint8_t difference = 0;
                for(size_t index = 0; index < sizeof(digest); ++index) {
                    difference |= static_cast<uint8_t>(
                        digest[index] ^ manifest_.sha256[index]);
                }
                if(difference != 0) {
                    fail(UpdateFailure::HashMismatch, true);
                } else {
                    snapshot_.state = UpdateState::Writing;
                    snapshot_.cancel_allowed = false;
                }
            }
        }
    } else if(snapshot_.state == UpdateState::Writing) {
        if(!writer_.finish() || !writer_.selectForNextBoot()) {
            fail(UpdateFailure::FinalizeFailed, true);
        } else {
            writer_started_ = false;
            snapshot_.state = UpdateState::RebootPending;
            snapshot_.failure = UpdateFailure::None;
            snapshot_.progress_percent = 100;
            snapshot_.cancel_allowed = false;
            cleanup(false);
        }
    }
    xSemaphoreGive(mutex_);
}

bool UpdateService::cancel(uint32_t now_ms) {
    (void)now_ms;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool allowed = snapshot_.state == UpdateState::Available ||
                         snapshot_.state == UpdateState::Downloading ||
                         snapshot_.state == UpdateState::Verifying;
    if(allowed) fail(UpdateFailure::Cancelled, writer_started_);
    xSemaphoreGive(mutex_);
    return allowed;
}

bool UpdateService::reset() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool allowed = !isActive(snapshot_.state) &&
                         snapshot_.state != UpdateState::RebootPending &&
                         snapshot_.state != UpdateState::BootChecking &&
                         snapshot_.state != UpdateState::RollbackRequested;
    if(allowed) snapshot_ = {};
    xSemaphoreGive(mutex_);
    return allowed;
}

bool UpdateService::reportFailure(UpdateFailure failure) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool allowed = failure != UpdateFailure::None &&
        !isActive(snapshot_.state) &&
        snapshot_.state != UpdateState::RebootPending &&
        snapshot_.state != UpdateState::BootChecking &&
        snapshot_.state != UpdateState::RollbackRequested;
    if(allowed) setBlocked(failure);
    xSemaphoreGive(mutex_);
    return allowed;
}

UpdateSnapshot UpdateService::snapshot() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const UpdateSnapshot copy = snapshot_;
    xSemaphoreGive(mutex_);
    return copy;
}

void UpdateService::fail(UpdateFailure failure, bool abort_writer) {
    if(isTerminal(snapshot_.state)) return;
    snapshot_.state = UpdateState::Failed;
    snapshot_.failure = failure;
    snapshot_.cancel_allowed = false;
    cleanup(abort_writer);
}

void UpdateService::cleanup(bool abort_writer) {
    if(abort_writer && writer_started_) writer_.abort();
    writer_started_ = false;
    if(source_open_ && source_) source_->close();
    source_open_ = false;
    if(resource_held_) resources_.release(ResourceKind::Ota);
    resource_held_ = false;
}

void UpdateService::updateProgress() {
    snapshot_.progress_percent = manifest_.size == 0 ? 0 :
        static_cast<uint8_t>((static_cast<uint64_t>(snapshot_.processed) * 100U) /
                             manifest_.size);
}

UpdateFailure UpdateService::ioFailure(UpdateIoResult result) const {
    if(result == UpdateIoResult::Timeout) return UpdateFailure::Timeout;
    if(result == UpdateIoResult::NoEndpoint) {
        return UpdateFailure::NoHttpsEndpoint;
    }
    return UpdateFailure::SourceUnavailable;
}

}  // namespace firefly
