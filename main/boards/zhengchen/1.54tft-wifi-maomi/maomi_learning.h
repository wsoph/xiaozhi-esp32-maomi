#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace maomi {

struct LearningTime {
    int64_t epoch = 0;
    int32_t date = 0;
    int month = 0;
    int day = 0;
    bool Valid() const;
};

struct LearningWord {
    std::string word;
    std::string meaning;
};

struct WordProgress {
    std::string word;
    uint32_t meaning_hash = 0;
    int64_t due = 0;
    int32_t reward_date = 0;
    int32_t completed_date = 0;
    uint8_t stage = 0;
};

struct LearningProgress {
    uint16_t unlearned = 0;
    uint16_t consolidating = 0;
    uint16_t mastered = 0;
    uint16_t due = 0;
    int64_t next_review = 0;
};

struct LearningState {
    uint32_t revision = 0;
    std::string name;
    int64_t born = 0;
    int32_t birthday = 0;
    int64_t observed = 0;
    int64_t decay_at = 0;
    int64_t happy_until = 0;
    int32_t date = 0;
    uint32_t coins = 0;
    uint16_t food = 0;
    uint16_t litter = 0;
    uint16_t snacks = 0;
    uint8_t satiety = 100;
    uint8_t poop = 0;
    uint8_t meals = 0;
    uint8_t feeds_today = 0;
    bool cleaned_today = false;
    bool care_counted = false;
    uint32_t care_days = 0;
    uint8_t earned_today = 0;
    bool daily_paid = false;
    int book_slot = 0;
    uint32_t book_hash = 0;
    uint32_t session = 0;
    bool active = false;
    bool chinese_prompt = false;
    uint8_t question = 0;
    uint8_t target = 0;
    uint32_t session_coins = 0;
    // At most five first attempts followed by five corrections.
    std::vector<std::string> questions;
    std::vector<WordProgress> history;
};

struct LearningResult {
    bool ok = false;
    std::string error;
    uint32_t coins_added = 0;
};

class LearningStore {
public:
    virtual ~LearningStore() = default;
    // Empty state means an uninitialized device; corrupt/unreadable state must fail closed.
    virtual bool LoadState(std::vector<uint8_t>& out) = 0;
    virtual bool SaveState(const std::vector<uint8_t>& data) = 0;
    virtual bool LoadBook(int slot, std::vector<uint8_t>& out) = 0;
    virtual bool SaveBook(int slot, const std::vector<uint8_t>& data) = 0;
};

// Owned by the learning worker. No application, LVGL or audio calls from this class.
class LearningEngine {
public:
    explicit LearningEngine(LearningStore& store) : store_(store) {}
    bool Load();
    const LearningState& GetState() const { return state_; }
    const std::vector<LearningWord>& GetBook() const { return book_; }
    uint32_t AgeDays(LearningTime now) const;
    bool IsBirthday(LearningTime now) const;
    std::string Mood(LearningTime now) const;
    LearningResult Adopt(const std::string& name, LearningTime now, uint32_t revision);
    LearningResult Care(const std::string& action, LearningTime now, uint32_t revision);
    LearningResult Buy(const std::string& item, int quantity, LearningTime now, uint32_t revision);
    LearningResult Observe(LearningTime now);
    LearningResult Import(std::vector<LearningWord> book, LearningTime now, uint32_t revision);
    LearningResult Start(bool chinese_prompt, LearningTime now, uint32_t revision,
                         bool review_mastered = false);
    LearningResult Answer(uint32_t session, uint8_t question, const std::string& verdict,
                          LearningTime now, uint32_t legacy_revision = 0);
    LearningResult Stop(LearningTime now, uint32_t revision);
    const LearningWord* CurrentWord() const;
    // Spoken question only; the opposite side of the card is reserved for grading.
    std::string QuestionPrompt() const;
    LearningProgress GetProgress(LearningTime now) const;
    static bool NormalizeWord(std::string& word);
    static bool ValidText(const std::string& value, size_t limit);
    static uint32_t Checksum(const std::vector<uint8_t>& data);

private:
    LearningStore& store_;
    LearningState state_;
    std::vector<LearningWord> book_;
    bool writable_ = false;
    LearningResult Check(LearningTime now, uint32_t revision, bool adoption_required = true) const;
    LearningResult Commit(LearningState next, uint32_t award = 0);
    static void Advance(LearningState& state, LearningTime now);
    static WordProgress* Progress(LearningState& state, const std::string& word);
};

}  // namespace maomi
