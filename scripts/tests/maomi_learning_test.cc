#include "maomi_learning.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x)                                      \
    do {                                              \
        if (!(x)) {                                   \
            std::cerr << __LINE__ << ": " #x << '\n'; \
            std::exit(1);                             \
        }                                             \
    } while (0)

class MemoryStore : public maomi::LearningStore {
public:
    std::vector<uint8_t> state;
    std::vector<uint8_t> books[2];
    bool fail = false;
    bool fail_state_only = false;
    bool LoadState(std::vector<uint8_t>& out) override {
        out = state;
        return true;
    }
    bool SaveState(const std::vector<uint8_t>& data) override {
        if (fail || fail_state_only)
            return false;
        state = data;
        return true;
    }
    bool LoadBook(int slot, std::vector<uint8_t>& out) override {
        out = books[slot];
        return true;
    }
    bool SaveBook(int slot, const std::vector<uint8_t>& data) override {
        if (fail)
            return false;
        books[slot] = data;
        return true;
    }
};

int main() {
    // Starting a session changes revision. A reply using the previous life snapshot
    // must still advance the current question, while duplicate replies cannot earn twice.
    {
        MemoryStore store;
        maomi::LearningEngine engine(store);
        const maomi::LearningTime now{1800000000, 20270115, 1, 15};
        CHECK(engine.Load());
        CHECK(engine.Adopt("Test", now, 0).ok);
        CHECK(engine.Import({{"stamp", "邮票"}, {"prize", "奖品"}}, now, engine.GetState().revision)
                  .ok);
        const auto previous_revision = engine.GetState().revision;
        CHECK(engine.Start(false, now, previous_revision).ok);
        const auto session = engine.GetState().session;
        CHECK(engine.CurrentWord()->word == "stamp");
        CHECK(engine.Answer(session, 0, "correct", now, previous_revision).ok);
        CHECK(engine.CurrentWord()->word == "prize");
        maomi::LearningEngine resumed(store);
        CHECK(resumed.Load());
        CHECK(resumed.GetState().question == 1 && resumed.CurrentWord()->word == "prize");
        const auto coins = engine.GetState().coins;
        CHECK(engine.Answer(session, 0, "correct", now, previous_revision).error ==
              "stale_question");
        CHECK(engine.GetState().coins == coins);
        CHECK(engine.Care("pet", now, engine.GetState().revision).ok);
        CHECK(engine.Answer(session, 1, "correct", now, previous_revision).ok);
        CHECK(!engine.GetState().active);
        CHECK(engine.Answer(session, 1, "correct", now, previous_revision).error ==
              "stale_question");
        auto later = now;
        later.epoch += 86400;
        ++later.date;
        CHECK(engine.Start(false, later, engine.GetState().revision).ok);
        CHECK(engine.Answer(session, 0, "correct", later, previous_revision).error ==
              "stale_question");
        CHECK(engine.GetState().question == 0);
    }
    MemoryStore store;
    maomi::LearningEngine pet(store);
    CHECK(pet.Load());
    maomi::LearningTime t{1800000000, 20270115, 1, 15};
    CHECK(pet.Adopt("Mimi", {}, 0).error == "time_unavailable");
    CHECK(pet.Adopt("Mimi", t, 0).ok);
    CHECK(pet.GetState().coins == 0 && pet.GetState().food == 6);
    CHECK(pet.AgeDays(t) == 0);
    auto later = t;
    later.epoch += 60;
    later.date++;
    CHECK(pet.AgeDays(later) == 0);
    later.epoch = t.epoch + 86400;
    CHECK(pet.AgeDays(later) == 1);
    CHECK(pet.Adopt("Again", later, pet.GetState().revision).error == "already_adopted");
    CHECK(pet.Care("feed", t, pet.GetState().revision).error == "already_full");
    CHECK(pet.Care("feed", later, pet.GetState().revision).ok);
    CHECK(pet.GetState().food == 5 && pet.GetState().satiety == 75);
    const auto revision = pet.GetState().revision;
    store.fail = true;
    CHECK(!pet.Care("feed", later, revision).ok);
    CHECK(pet.GetState().food == 5 && pet.GetState().revision == revision);
    store.fail = false;
    CHECK(pet.Care("feed", later, revision).ok);
    CHECK(pet.Care("feed", later, revision).error == "stale_revision");
    CHECK(pet.GetState().poop == 1);
    CHECK(pet.Care("clean", later, pet.GetState().revision).ok);
    CHECK(pet.GetState().care_days == 1 && pet.GetState().litter == 2);
    CHECK(!pet.Buy("food", 1, later, pet.GetState().revision).ok);
    maomi::LearningEngine restored(store);
    CHECK(restored.Load());
    CHECK(restored.GetState().care_days == 1 && restored.GetState().food == 4);
    CHECK(restored.AgeDays(t) == 1);
    std::vector<maomi::LearningWord> book{
        {"Apple", "苹果"}, {"cat", "猫"}, {"dog", "狗"}, {"fish", "鱼"}, {"milk", "牛奶"}};
    CHECK(restored.Import(book, later, restored.GetState().revision).ok);
    CHECK(restored.GetBook()[0].word == "apple");
    CHECK(restored.Start(false, later, restored.GetState().revision).ok);
    const auto session = restored.GetState().session;
    for (int i = 0; i < 5; ++i) {
        CHECK(restored.CurrentWord() != nullptr);
        CHECK(restored.Answer(session, i, "correct", later, restored.GetState().revision).ok);
    }
    CHECK(!restored.GetState().active && restored.GetState().coins == 20);
    CHECK(restored.GetState().history[0].due == later.epoch + 86400);
    CHECK(restored.Buy("food", 2, later, restored.GetState().revision).ok);
    CHECK(restored.GetState().coins == 12 && restored.GetState().food == 6);
    CHECK(restored.Import(book, later, restored.GetState().revision).ok);
    maomi::LearningEngine after_import(store);
    CHECK(after_import.Load());
    CHECK(after_import.Start(true, later, after_import.GetState().revision).error ==
          "no_words_due");
    for (int i = 0; i < 5; ++i)
        CHECK(after_import
                  .Answer(after_import.GetState().session, i, "correct", later,
                          after_import.GetState().revision)
                  .error == "stale_question");
    CHECK(after_import.GetState().history[0].stage == 1);
    auto tomorrow = later;
    tomorrow.date++;
    tomorrow.epoch += 86400;
    CHECK(after_import.Start(false, tomorrow, after_import.GetState().revision).ok);
    CHECK(after_import
              .Answer(after_import.GetState().session, 0, "unclear", tomorrow,
                      after_import.GetState().revision)
              .ok);
    CHECK(after_import.GetState().question == 0);
    for (int i = 0; i < 5; ++i)
        CHECK(after_import
                  .Answer(after_import.GetState().session, i, "wrong", tomorrow,
                          after_import.GetState().revision)
                  .ok);
    CHECK(after_import.GetState().questions.size() == 10);
    CHECK(after_import
              .Answer(after_import.GetState().session, 5, "correct", tomorrow,
                      after_import.GetState().revision)
              .error == "invalid_verdict_for_question");
    for (int i = 5; i < 10; ++i)
        CHECK(after_import
                  .Answer(after_import.GetState().session, i, "corrected", tomorrow,
                          after_import.GetState().revision)
                  .ok);
    CHECK(after_import.GetState().coins == 22);
    CHECK(after_import.GetState().history[0].stage == 0);
    const auto stable_hash = after_import.GetState().book_hash;
    book[0].meaning = "苹果水果";
    store.fail = true;
    CHECK(!after_import.Import(book, tomorrow, after_import.GetState().revision).ok);
    CHECK(after_import.GetState().book_hash == stable_hash);
    store.fail = false;
    store.fail_state_only = true;
    CHECK(!after_import.Import(book, tomorrow, after_import.GetState().revision).ok);
    maomi::LearningEngine interrupted_import(store);
    CHECK(interrupted_import.Load());
    CHECK(interrupted_import.GetState().book_hash == stable_hash);
    CHECK(interrupted_import.GetBook()[0].meaning == "苹果");
    store.fail_state_only = false;
    CHECK(after_import.Import(book, tomorrow, after_import.GetState().revision).ok);
    CHECK(after_import.GetState().book_hash != stable_hash);
    CHECK(
        !after_import.Import({{"a", "甲"}, {"A", "乙"}}, tomorrow, after_import.GetState().revision)
             .ok);
    std::string invalid = "$(secret)";
    CHECK(!maomi::LearningEngine::NormalizeWord(invalid));
    CHECK(!maomi::LearningEngine::ValidText(std::string("a\0b", 3), 120));
    CHECK(!maomi::LearningEngine::ValidText("\xc0\x80", 120));
    store.state.back() ^= 1;
    maomi::LearningEngine corrupt(store);
    CHECK(!corrupt.Load());
    CHECK(!corrupt.Adopt("Lost", t, 0).ok);
    MemoryStore limits;
    maomi::LearningEngine limited(limits);
    CHECK(limited.Load());
    CHECK(limited.Adopt("Limit", t, 0).ok);
    std::vector<maomi::LearningWord> many;
    for (char c = 'a'; c <= 't'; ++c)
        many.push_back({std::string("word") + c, "词"});
    CHECK(limited.Import(many, t, limited.GetState().revision).ok);
    for (int batch = 0; batch < 4; ++batch) {
        CHECK(limited.Start(false, t, limited.GetState().revision).ok);
        for (int i = 0; i < 5; ++i)
            CHECK(limited
                      .Answer(limited.GetState().session, i, "correct", t,
                              limited.GetState().revision)
                      .ok);
    }
    CHECK(limited.GetState().coins == 40);
    auto rollback = t;
    rollback.date--;
    rollback.epoch -= 86400;
    CHECK(limited.Start(false, rollback, limited.GetState().revision).error == "no_words_due");
    for (int i = 0; i < 5; ++i)
        CHECK(limited
                  .Answer(limited.GetState().session, i, "correct", rollback,
                          limited.GetState().revision)
                  .error == "stale_question");
    CHECK(limited.GetState().coins == 40);
    CHECK(limited.Buy("food", 99, t, limited.GetState().revision).error == "insufficient_coins");
    auto absent = t;
    absent.epoch += 100 * 86400;
    absent.date = 20270425;
    CHECK(limited.Observe(absent).ok);
    CHECK(limited.AgeDays(absent) == 100 && limited.GetState().satiety == 0);
    CHECK(limited.GetState().coins == 40 && limited.GetState().name == "Limit");
    CHECK(limited.Care("feed", absent, limited.GetState().revision).ok);
    CHECK(limited.GetState().satiety == 25);
    auto birthday = t;
    birthday.date = 20280115;
    birthday.epoch += 365 * 86400;
    CHECK(limited.IsBirthday(birthday));
    MemoryStore leap_store;
    maomi::LearningEngine leap(leap_store);
    CHECK(leap.Load());
    maomi::LearningTime leap_born{1835395200, 20280229, 2, 29};
    CHECK(leap.Adopt("Leap", leap_born, 0).ok);
    CHECK(leap.IsBirthday({1867017600, 20290228, 2, 28}));
    CHECK(!leap.IsBirthday(leap_born));
    MemoryStore capacity_store;
    maomi::LearningEngine capacity(capacity_store);
    CHECK(capacity.Load());
    std::vector<maomi::LearningWord> full_book;
    for (int i = 0; i < 1000; ++i) {
        std::string word(32, 'a');
        word[29] += i / 676;
        word[30] += (i / 26) % 26;
        word[31] += i % 26;
        full_book.push_back({word, std::string(120, 'x')});
    }
    CHECK(capacity.Import(full_book, t, 0).ok);
    CHECK(capacity_store.books[1].size() < 200000);
    maomi::LearningEngine capacity_reload(capacity_store);
    CHECK(capacity_reload.Load());
    CHECK(capacity_reload.GetBook().size() == 1000);
    for (auto& word : full_book)
        word.word[0] = 'b';
    CHECK(capacity_reload.Import(full_book, t, capacity_reload.GetState().revision).ok);
    CHECK(capacity_store.state.size() < 220000);
    CHECK(capacity_reload.GetState().history.size() == 2000);
    CHECK(capacity_reload.Import({{"extra", "extra"}}, t, capacity_reload.GetState().revision)
              .error == "history_full");
    // Version-1 saves used stages up to five. They remain readable without losing progress.
    MemoryStore legacy_store;
    maomi::LearningEngine legacy(legacy_store);
    CHECK(legacy.Load());
    CHECK(legacy.Adopt("Legacy", t, 0).ok);
    CHECK(legacy.Import({{"cat", "猫"}}, t, legacy.GetState().revision).ok);
    CHECK(legacy.Start(false, t, legacy.GetState().revision).ok);
    CHECK(legacy.Answer(legacy.GetState().session, 0, "correct", t, legacy.GetState().revision).ok);
    legacy_store.state[legacy_store.state.size() - 5] = 5;
    std::vector<uint8_t> payload(legacy_store.state.begin(), legacy_store.state.end() - 4);
    const auto checksum = maomi::LearningEngine::Checksum(payload);
    for (size_t i = 0; i < 4; ++i)
        legacy_store.state[legacy_store.state.size() - 4 + i] = checksum >> (i * 8);
    maomi::LearningEngine legacy_restored(legacy_store);
    CHECK(legacy_restored.Load());
    CHECK(legacy_restored.GetState().name == "Legacy" && legacy_restored.GetState().coins == 12);
    CHECK(legacy_restored.GetProgress(t).mastered == 1);
    CHECK(legacy_restored.Start(false, t, legacy_restored.GetState().revision).error ==
          "all_mastered");
    CHECK(legacy_restored.Start(false, t, legacy_restored.GetState().revision, true).ok);
    legacy_store.fail = true;
    CHECK(!legacy_restored
               .Answer(legacy_restored.GetState().session, 0, "wrong", t,
                       legacy_restored.GetState().revision)
               .ok);
    CHECK(legacy_restored.GetProgress(t).mastered == 1);
    legacy_store.fail = false;
    CHECK(legacy_restored
              .Answer(legacy_restored.GetState().session, 0, "wrong", t,
                      legacy_restored.GetState().revision)
              .ok);
    CHECK(legacy_restored.GetProgress(t).mastered == 0);
    std::cout << "learning contracts passed\n";
}
