#include "audio/voice_upload_gate.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

void TestSuspendNeverBlocksBehindAnAuthorizedSend() {
    VoiceUploadGate gate;
    const auto generation = gate.CaptureGeneration();
    auto lease = gate.TryAcquire(generation);
    assert(lease);

    std::atomic<bool> suspend_started{false};
    std::atomic<bool> suspend_finished{false};
    std::thread suspender([&]() {
        suspend_started.store(true, std::memory_order_release);
        gate.Suspend();
        suspend_finished.store(true, std::memory_order_release);
    });

    while (!suspend_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    const auto deadline = std::chrono::steady_clock::now() + 50ms;
    while (!suspend_finished.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool completed_while_lease_was_alive = suspend_finished.load(std::memory_order_acquire);

    lease = {};
    suspender.join();
    assert(completed_while_lease_was_alive);
    assert(suspend_finished.load(std::memory_order_acquire));
    assert(!gate.TryAcquire(generation));
}

void TestResumeAcceptsOnlyTheNewGeneration() {
    VoiceUploadGate gate;
    const auto stale_generation = gate.CaptureGeneration();

    gate.Suspend();
    const auto suspended_generation = gate.CaptureGeneration();
    gate.Resume();

    assert(!gate.TryAcquire(stale_generation));
    assert(!gate.TryAcquire(suspended_generation));
    const auto current_generation = gate.CaptureGeneration();
    auto lease = gate.TryAcquire(current_generation);
    assert(lease);
}

void TestResumeWhileActivePreservesTheCurrentGeneration() {
    VoiceUploadGate gate;
    const auto current_generation = gate.CaptureGeneration();

    gate.Resume();

    assert(gate.CaptureGeneration() == current_generation);
    assert(gate.TryAcquire(current_generation));
}

void TestDrainDropsAllStalePacketsAndSendsTheCurrentGeneration() {
    struct Packet {
        uint32_t voice_upload_generation;
        int id;
    };

    VoiceUploadGate gate;
    gate.Suspend();
    const auto stale_generation = gate.CaptureGeneration();
    gate.Resume();
    const auto current_generation = gate.CaptureGeneration();

    std::deque<std::unique_ptr<Packet>> queue;
    queue.push_back(std::make_unique<Packet>(Packet{stale_generation, 1}));
    queue.push_back(std::make_unique<Packet>(Packet{stale_generation, 2}));
    queue.push_back(std::make_unique<Packet>(Packet{current_generation, 3}));
    std::vector<int> sent_ids;

    DrainVoiceUploadQueue(
        [&]() {
            if (queue.empty()) {
                return std::unique_ptr<Packet>{};
            }
            auto packet = std::move(queue.front());
            queue.pop_front();
            return packet;
        },
        [&](uint32_t generation) { return gate.TryAcquire(generation); },
        [&](std::unique_ptr<Packet> packet) {
            sent_ids.push_back(packet->id);
            return true;
        });

    assert(queue.empty());
    assert((sent_ids == std::vector<int>{3}));
}

}  // namespace

int main() {
    TestSuspendNeverBlocksBehindAnAuthorizedSend();
    TestResumeAcceptsOnlyTheNewGeneration();
    TestResumeWhileActivePreservesTheCurrentGeneration();
    TestDrainDropsAllStalePacketsAndSendsTheCurrentGeneration();
    std::cout << "voice_upload_gate tests passed" << std::endl;
    return 0;
}
