#pragma once

#include <cstdint>
#include <mutex>
#include <utility>

class VoiceUploadGate {
public:
    class Lease {
    public:
        Lease() = default;
        Lease(Lease&& other) noexcept : acquired_(std::exchange(other.acquired_, false)) {}
        Lease& operator=(Lease&& other) noexcept {
            acquired_ = std::exchange(other.acquired_, false);
            return *this;
        }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        explicit operator bool() const { return acquired_; }

    private:
        friend class VoiceUploadGate;
        explicit Lease(bool acquired) : acquired_(acquired) {}

        bool acquired_ = false;
    };

    uint32_t CaptureGeneration() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return generation_;
    }

    bool CanQueue(uint32_t generation) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !suspended_ && generation == generation_;
    }

    Lease TryAcquire(uint32_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (suspended_ || generation != generation_) {
            return {};
        }
        // The lease records that this send was authorized before a later
        // suspension. It deliberately does not hold the gate mutex across the
        // potentially blocking network send.
        return Lease(true);
    }

    void Suspend() {
        std::lock_guard<std::mutex> lock(mutex_);
        suspended_ = true;
        ++generation_;
    }

    void Resume() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!suspended_) {
            return;
        }
        // Invalidate encode work created while uploads were suspended (for
        // example microphone frames captured during the local response).
        ++generation_;
        suspended_ = false;
    }

private:
    mutable std::mutex mutex_;
    uint32_t generation_ = 0;
    bool suspended_ = false;
};

template <typename PopPacket, typename AcquireLease, typename SendPacket>
void DrainVoiceUploadQueue(PopPacket&& pop_packet, AcquireLease&& acquire_lease,
                           SendPacket&& send_packet) {
    while (auto packet = pop_packet()) {
        auto upload_lease = acquire_lease(packet->voice_upload_generation);
        if (!upload_lease) {
            continue;
        }
        if (!send_packet(std::move(packet))) {
            // A failed send consumes the event that made this queue visible.
            // Clear the backlog so producers cannot stall behind a full queue.
            while (pop_packet()) {
            }
            break;
        }
    }
}
