#pragma once

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#ifdef CONFIG_MAOMI_LEARNING
#include "maomi_learning.h"
#include "maomi_pet_presentation.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <nvs.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

class McpServer;

namespace maomi {
struct LearningView {
    PetHomeView home;
    bool ready = false;
    bool adopted = false;
    bool active = false;
    bool birthday = false;
    uint32_t revision = 0;
    uint32_t care_days = 0;
    std::string word;
    std::string mood;
    std::string event;
};

class FlashLearningStore final : public LearningStore {
public:
    bool Initialize();
    bool LoadState(std::vector<uint8_t>& out) override;
    bool SaveState(const std::vector<uint8_t>& data) override;
    bool LoadBook(int slot, std::vector<uint8_t>& out) override;
    bool SaveBook(int slot, const std::vector<uint8_t>& data) override;

private:
    nvs_handle_t handle_ = 0;
    bool write_failed_ = false;
};

// One worker owns the engine/flash; the main task uses bounded asynchronous commands.
class LearningService {
public:
    bool Start();
    std::string Submit(const std::string& operation, const std::string& arguments);
    std::string Result(uint32_t id) const;
    std::string Status() const;
    LearningView View() const;
    void SetSuspended(bool value) { suspended_.store(value); }
    void RegisterTools(McpServer& server);

private:
    struct Command {
        uint32_t id;
        std::string operation;
        std::string arguments;
    };
    FlashLearningStore store_;
    LearningEngine engine_{store_};
    QueueHandle_t queue_ = nullptr;
    mutable std::mutex mutex_;
    uint32_t next_id_ = 0;
    uint32_t first_id_ = 0;
    std::deque<std::pair<uint32_t, std::string>> results_;
    uint32_t answer_attempts_ = 0;
    std::string last_answer_error_;
    std::string status_ = "{\"ok\":false,\"error\":\"initializing\"}";
    LearningView view_;
    bool published_time_valid_ = false;
    bool published_sleeping_ = false;
    std::atomic<bool> suspended_{false};
    std::vector<LearningWord> upload_;
    uint32_t upload_id_ = 0;
    uint32_t upload_crc_ = 0;
    uint32_t upload_revision_ = 0;
    size_t upload_count_ = 0;
    int64_t upload_expires_ = 0;
    void Worker();
    void SerialWorker();
    std::string Execute(const Command& command);
    void Publish(const std::string& event = "");
    static LearningTime Now();
};
}  // namespace maomi
#endif
