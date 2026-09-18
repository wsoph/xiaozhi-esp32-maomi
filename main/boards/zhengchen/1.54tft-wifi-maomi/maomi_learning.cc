#include "maomi_learning.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace maomi {
namespace {
constexpr int64_t kDay = 86400;
// Existing stages 3..5 already represent at least three spaced independent successes.
// Keep the persisted layout and legacy stage range readable.
constexpr uint8_t kMasteredStage = 3;
constexpr uint32_t kMagic = 0x4d4c0001;
constexpr uint32_t kLifeMagic = 0x4d4c0002;

struct Writer {
    std::vector<uint8_t> data;
    void Number(uint64_t n, size_t bytes = 8) {
        for (size_t i = 0; i < bytes; ++i)
            data.push_back(static_cast<uint8_t>(n >> (i * 8)));
    }
    void Text(const std::string& s) {
        Number(s.size(), 2);
        data.insert(data.end(), s.begin(), s.end());
    }
    std::vector<uint8_t> Finish() {
        Number(LearningEngine::Checksum(data), 4);
        return std::move(data);
    }
};
struct Reader {
    const std::vector<uint8_t>& data;
    size_t offset = 0;
    uint64_t Number(size_t bytes = 8) {
        if (offset + bytes > data.size())
            throw std::runtime_error("truncated_save");
        uint64_t n = 0;
        for (size_t i = 0; i < bytes; ++i)
            n |= uint64_t(data[offset++]) << (i * 8);
        return n;
    }
    std::string Text(size_t limit) {
        const auto size = Number(2);
        if (size > limit || offset + size > data.size())
            throw std::runtime_error("invalid_text");
        std::string s(data.begin() + offset, data.begin() + offset + size);
        offset += size;
        return s;
    }
    void Finish() {
        if (offset + 4 != data.size())
            throw std::runtime_error("invalid_save_size");
        std::vector<uint8_t> payload(data.begin(), data.end() - 4);
        if (Number(4) != LearningEngine::Checksum(payload))
            throw std::runtime_error("bad_checksum");
    }
};

std::vector<uint8_t> Encode(const LearningState& s) {
    Writer w;
    w.Number(kLifeMagic, 4);
    w.Number(s.revision, 4);
    w.Text(s.name);
    for (auto n : {s.born, s.observed, s.decay_at, s.happy_until})
        w.Number(n);
    for (auto n : {uint32_t(s.birthday), uint32_t(s.date), s.coins, s.care_days, s.book_hash,
                   s.session, s.session_coins})
        w.Number(n, 4);
    for (auto n : {s.food, s.litter, s.snacks})
        w.Number(n, 2);
    for (int n : std::initializer_list<int>{
             s.satiety, s.poop, s.meals, s.feeds_today, int(s.cleaned_today), int(s.care_counted),
             s.earned_today, int(s.daily_paid), s.book_slot, int(s.active), int(s.chinese_prompt),
             s.question, s.target})
        w.Number(n, 1);
    w.Number(s.questions.size(), 1);
    for (const auto& q : s.questions)
        w.Text(q);
    const auto& life = s.life;
    for (auto n :
         {life.updated_at, life.sleep_until, life.awake_until, life.last_play, life.last_pet})
        w.Number(n);
    for (auto n : {uint32_t(life.supply_date), uint32_t(life.last_pet_date),
                   uint32_t(life.last_play_date), life.pet_days, life.play_days, life.owned})
        w.Number(n, 4);
    for (auto n :
         {life.happiness, life.energy, life.hydration, life.health, life.outfit, life.room})
        w.Number(n, 1);
    w.Number(s.history.size(), 2);
    for (const auto& p : s.history) {
        w.Text(p.word);
        w.Number(p.meaning_hash, 4);
        w.Number(p.due);
        w.Number(p.reward_date, 4);
        w.Number(p.completed_date, 4);
        w.Number(p.stage, 1);
    }
    return w.Finish();
}

LearningState Decode(const std::vector<uint8_t>& data) {
    Reader r{data};
    LearningState s;
    const auto schema = r.Number(4);
    if (schema != kMagic && schema != kLifeMagic)
        throw std::runtime_error("unsupported_schema");
    s.revision = r.Number(4);
    s.name = r.Text(48);
    s.born = r.Number();
    s.observed = r.Number();
    s.decay_at = r.Number();
    s.happy_until = r.Number();
    s.birthday = r.Number(4);
    s.date = r.Number(4);
    s.coins = r.Number(4);
    s.care_days = r.Number(4);
    s.book_hash = r.Number(4);
    s.session = r.Number(4);
    s.session_coins = r.Number(4);
    s.food = r.Number(2);
    s.litter = r.Number(2);
    s.snacks = r.Number(2);
    s.satiety = r.Number(1);
    s.poop = r.Number(1);
    s.meals = r.Number(1);
    s.feeds_today = r.Number(1);
    s.cleaned_today = r.Number(1);
    s.care_counted = r.Number(1);
    s.earned_today = r.Number(1);
    s.daily_paid = r.Number(1);
    s.book_slot = r.Number(1);
    s.active = r.Number(1);
    s.chinese_prompt = r.Number(1);
    s.question = r.Number(1);
    s.target = r.Number(1);
    auto count = r.Number(1);
    if (count > 10)
        throw std::runtime_error("invalid_session");
    while (count--)
        s.questions.push_back(r.Text(32));
    auto& life = s.life;
    life.updated_at = s.observed;
    if (schema == kLifeMagic) {
        life.updated_at = r.Number();
        life.sleep_until = r.Number();
        life.awake_until = r.Number();
        life.last_play = r.Number();
        life.last_pet = r.Number();
        life.supply_date = r.Number(4);
        life.last_pet_date = r.Number(4);
        life.last_play_date = r.Number(4);
        life.pet_days = r.Number(4);
        life.play_days = r.Number(4);
        life.owned = r.Number(4);
        life.happiness = r.Number(1);
        life.energy = r.Number(1);
        life.hydration = r.Number(1);
        life.health = r.Number(1);
        life.outfit = r.Number(1);
        life.room = r.Number(1);
        if (life.updated_at < 0 || life.updated_at > s.observed || life.sleep_until < 0 ||
            life.awake_until < 0 || life.last_play < 0 || life.last_pet < 0 ||
            life.happiness > 100 || life.energy > 100 || life.hydration > 100 || life.health < 40 ||
            life.health > 100 || life.owned > 63 || life.outfit > 2 || life.room > 2 ||
            (life.outfit && !(life.owned & (life.outfit == 1 ? 4 : 8))) ||
            (life.room && !(life.owned & (life.room == 1 ? 16 : 32))))
            throw std::runtime_error("invalid_life_state");
    }
    count = r.Number(2);
    if (count > 2000)
        throw std::runtime_error("invalid_history");
    while (count--) {
        WordProgress p;
        p.word = r.Text(32);
        p.meaning_hash = r.Number(4);
        p.due = r.Number();
        p.reward_date = r.Number(4);
        p.completed_date = r.Number(4);
        p.stage = r.Number(1);
        if (p.stage > 5 || !LearningEngine::NormalizeWord(p.word))
            throw std::runtime_error("invalid_progress");
        s.history.push_back(std::move(p));
    }
    r.Finish();
    if (s.book_slot > 1 || s.satiety > 100 || s.poop > 3 || s.meals > 1 || s.feeds_today > 2 ||
        s.earned_today > 40 || s.target > 5 || s.question > s.questions.size() ||
        (s.active && s.question >= s.questions.size()) || s.born < 0 || s.observed < s.born ||
        s.food > 999 || s.litter > 999 || s.snacks > 999)
        throw std::runtime_error("invalid_state");
    return s;
}

std::vector<uint8_t> EncodeBook(const std::vector<LearningWord>& book) {
    Writer w;
    w.Number(kMagic, 4);
    w.Number(book.size(), 2);
    for (const auto& word : book) {
        w.Text(word.word);
        w.Text(word.meaning);
    }
    return w.Finish();
}
std::vector<LearningWord> DecodeBook(const std::vector<uint8_t>& data) {
    Reader r{data};
    if (r.Number(4) != kMagic)
        throw std::runtime_error("unsupported_book");
    auto count = r.Number(2);
    if (!count || count > 1000)
        throw std::runtime_error("invalid_book_size");
    std::vector<LearningWord> book;
    while (count--) {
        LearningWord word{r.Text(32), r.Text(120)};
        if (!LearningEngine::NormalizeWord(word.word) ||
            !LearningEngine::ValidText(word.meaning, 120))
            throw std::runtime_error("invalid_word");
        book.push_back(std::move(word));
    }
    r.Finish();
    return book;
}
uint32_t TextHash(const std::string& text) {
    return LearningEngine::Checksum(std::vector<uint8_t>(text.begin(), text.end()));
}
uint32_t BookHash(const std::vector<uint8_t>& data) {
    if (data.size() < 4)
        return 0;
    return LearningEngine::Checksum(std::vector<uint8_t>(data.begin(), data.end() - 4));
}
LearningResult Error(const char* error) { return {false, error, 0}; }
}  // namespace

bool LearningTime::Valid() const {
    return epoch >= 1704067200 && epoch < 4102444800 && date >= 20240101 && date <= 20991231 &&
           month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour >= 0 && hour <= 23;
}

uint32_t LearningEngine::Checksum(const std::vector<uint8_t>& data) {
    uint32_t crc = 0xffffffff;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    return ~crc;
}

bool LearningEngine::ValidText(const std::string& text, size_t limit) {
    if (text.empty() || text.size() > limit)
        return false;
    // Strict UTF-8, no controls, embedded NUL, overlong encodings or surrogates.
    for (size_t i = 0; i < text.size();) {
        uint32_t c = static_cast<uint8_t>(text[i++]);
        if (c < 0x20 || c == 0x7f)
            return false;
        if (c < 0x80)
            continue;
        int count;
        uint32_t minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            count = 1;
            minimum = 0x80;
            c &= 0x1f;
        } else if (c >= 0xe0 && c <= 0xef) {
            count = 2;
            minimum = 0x800;
            c &= 0xf;
        } else if (c >= 0xf0 && c <= 0xf4) {
            count = 3;
            minimum = 0x10000;
            c &= 7;
        } else
            return false;
        while (count--) {
            if (i >= text.size())
                return false;
            uint8_t next = text[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            c = (c << 6) | (next & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            return false;
    }
    return text.find_first_not_of(' ') != std::string::npos;
}

bool LearningEngine::NormalizeWord(std::string& word) {
    auto first = word.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return false;
    word = word.substr(first, word.find_last_not_of(" \t\r\n") - first + 1);
    if (word.size() > 32)
        return false;
    for (auto& c : word) {
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (!(c >= 'a' && c <= 'z') && c != '-' && c != '\'')
            return false;
    }
    return word.front() >= 'a' && word.front() <= 'z' && word.back() >= 'a' && word.back() <= 'z';
}

bool LearningEngine::Load() {
    writable_ = false;
    try {
        std::vector<uint8_t> data;
        if (!store_.LoadState(data))
            return false;
        LearningState next;
        if (!data.empty())
            next = Decode(data);
        std::vector<LearningWord> book;
        if (!next.history.empty()) {
            if (!store_.LoadBook(next.book_slot, data) || BookHash(data) != next.book_hash)
                return false;
            book = DecodeBook(data);
        }
        state_ = std::move(next);
        book_ = std::move(book);
        writable_ = true;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

LearningResult LearningEngine::Check(LearningTime now, uint32_t revision, bool adopted) const {
    if (!writable_)
        return Error("storage_unavailable");
    if (revision != state_.revision)
        return Error("stale_revision");
    if (!now.Valid())
        return Error("time_unavailable");
    if (adopted && state_.name.empty())
        return Error("not_adopted");
    return {true, {}, 0};
}

LearningResult LearningEngine::Commit(LearningState next, uint32_t award) {
    if (state_.revision == std::numeric_limits<int32_t>::max())
        return Error("revision_exhausted");
    next.revision = state_.revision + 1;
    if (!store_.SaveState(Encode(next)))
        return Error("storage_write_failed");
    state_ = std::move(next);
    return {true, {}, award};
}

void LearningEngine::Advance(LearningState& s, LearningTime now) {
    if (!now.Valid() || s.name.empty())
        return;
    AdvancePetLife(s.life, now.epoch, now.hour, s.satiety, s.decay_at, s.poop);
    s.observed = std::max(s.observed, now.epoch);
    constexpr int64_t step = kDay / 50;
    const auto elapsed = std::min<int64_t>(3 * kDay, std::max<int64_t>(0, now.epoch - s.decay_at));
    const auto loss = elapsed / step;
    if (loss) {
        s.satiety = static_cast<uint8_t>(std::max<int64_t>(0, s.satiety - loss));
        s.decay_at = now.epoch - elapsed % step;
    }
    if (now.date > s.date) {
        s.date = now.date;
        s.feeds_today = 0;
        s.cleaned_today = false;
        s.care_counted = false;
        s.earned_today = 0;
        s.daily_paid = false;
    }
}

uint32_t LearningEngine::AgeDays(LearningTime now) const {
    if (state_.name.empty())
        return 0;
    const auto observed = now.Valid() ? std::max(state_.observed, now.epoch) : state_.observed;
    return static_cast<uint32_t>((observed - state_.born) / kDay);
}

bool LearningEngine::IsBirthday(LearningTime now) const {
    if (!now.Valid() || state_.name.empty() || now.date / 10000 <= state_.birthday / 10000)
        return false;
    int anniversary = state_.birthday % 10000;
    const int year = now.date / 10000;
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (anniversary == 229 && !leap)
        anniversary = 228;
    return now.month * 100 + now.day == anniversary;
}

std::string LearningEngine::Mood(LearningTime now) const {
    auto current = state_;
    Advance(current, now);
    if (current.life.health < 60)
        return "sick";
    if (IsSleeping(now))
        return "sleeping";
    if (current.satiety < 25)
        return "hungry";
    if (current.poop)
        return "dirty";
    if (current.life.hydration < 25)
        return "thirsty";
    if (current.life.energy < 25)
        return "tired";
    if (current.life.happiness < 30)
        return "bored";
    if (now.Valid() && now.epoch < current.happy_until)
        return "happy";
    return "content";
}

PetLifeState LearningEngine::GetLife(LearningTime now) const {
    auto current = state_;
    Advance(current, now);
    return current.life;
}

bool LearningEngine::IsSleeping(LearningTime now) const {
    if (state_.name.empty() || !now.Valid())
        return false;
    return PetSleeping(state_.life, std::max(now.epoch, state_.observed), now.hour);
}

LearningResult LearningEngine::Observe(LearningTime now) {
    auto check = Check(now, state_.revision);
    if (!check.ok)
        return check;
    auto next = state_;
    Advance(next, now);
    // Persist at most hourly when idle; every real operation includes the latest time.
    if (next.date == state_.date && next.observed - state_.observed < 3600 &&
        (next.observed - next.born) / kDay == (state_.observed - state_.born) / kDay)
        return {true, {}, 0};
    return Commit(std::move(next));
}

LearningResult LearningEngine::Adopt(const std::string& name, LearningTime now, uint32_t revision) {
    auto check = Check(now, revision, false);
    if (!check.ok)
        return check;
    if (!state_.name.empty())
        return Error("already_adopted");
    if (!ValidText(name, 48))
        return Error("invalid_name");
    auto next = state_;
    next.name = name;
    next.born = now.epoch;
    next.birthday = now.date;
    next.observed = now.epoch;
    next.decay_at = now.epoch;
    next.life.updated_at = now.epoch;
    next.date = now.date;
    next.food = 6;
    next.litter = 3;
    return Commit(std::move(next));
}

LearningResult LearningEngine::Care(const std::string& action, LearningTime now,
                                    uint32_t revision) {
    auto check = Check(now, revision);
    if (!check.ok)
        return check;
    auto next = state_;
    Advance(next, now);
    auto& life = next.life;
    if (action == "feed" || action == "snack") {
        auto& stock = action == "feed" ? next.food : next.snacks;
        if (!stock)
            return Error("out_of_stock");
        if (next.satiety == 100)
            return Error("already_full");
        --stock;
        next.satiety = std::min(100, next.satiety + (action == "feed" ? 25 : 5));
        if (action == "feed") {
            next.feeds_today = std::min(2, next.feeds_today + 1);
            if (++next.meals == 2) {
                next.meals = 0;
                next.poop = std::min(3, next.poop + 1);
            }
        }
    } else if (action == "clean") {
        if (!next.poop)
            return Error("already_clean");
        if (!next.litter)
            return Error("out_of_stock");
        --next.litter;
        next.poop = 0;
        next.cleaned_today = true;
    } else if (action == "water") {
        if (life.hydration == 100)
            return Error("not_thirsty");
        life.hydration = 100;
    } else if (action == "doctor") {
        if (life.health == 100)
            return Error("already_healthy");
        life.health = 100;
    } else if (action == "sleep") {
        life.sleep_until = next.observed + 8 * 3600;
        life.awake_until = 0;
    } else if (action == "wake") {
        life.sleep_until = 0;
        life.awake_until = next.observed + 2 * 3600;
    } else if (action == "supplies") {
        if (life.supply_date >= next.date)
            return Error("supplies_claimed");
        if (next.food >= 2 && next.litter >= 1)
            return Error("already_stocked");
        next.food = std::max<uint16_t>(next.food, 2);
        next.litter = std::max<uint16_t>(next.litter, 1);
        life.supply_date = next.date;
    } else if (action == "pet" || action == "play") {
        if (life.last_pet && next.observed - life.last_pet < 300)
            return {true, {}, 0};
        life.last_pet = next.observed;
        life.happiness = PetMeter(life.happiness + 10);
        if (life.last_pet_date < next.date) {
            life.last_pet_date = next.date;
            ++life.pet_days;
        }
    } else
        return Error("invalid_action");
    next.happy_until = std::max(next.observed, now.epoch) + 600;
    if (next.feeds_today == 2 && next.cleaned_today && !next.care_counted) {
        next.care_counted = true;
        ++next.care_days;
    }
    return Commit(std::move(next));
}

LearningResult LearningEngine::Buy(const std::string& item, int quantity, LearningTime now,
                                   uint32_t revision) {
    auto check = Check(now, revision);
    if (!check.ok)
        return check;
    if (quantity < 1 || quantity > 99)
        return Error("invalid_quantity");
    auto next = state_;
    Advance(next, now);
    const auto* product = FindPetItem(item);
    if (!product)
        return Error("unknown_item");
    if (next.care_days < product->care_days)
        return Error("growth_locked");
    if (product->mask) {
        if (quantity != 1)
            return Error("durable_quantity_one");
        if (next.life.owned & product->mask)
            return Error("already_owned");
        if (next.coins < product->price)
            return Error("insufficient_coins");
        next.coins -= product->price;
        next.life.owned |= product->mask;
        return Commit(std::move(next));
    }
    uint16_t* stock = nullptr;
    int price = 0;
    if (item == "food") {
        stock = &next.food;
        price = 4;
    } else if (item == "litter") {
        stock = &next.litter;
        price = 2;
    } else if (item == "snack") {
        stock = &next.snacks;
        price = 12;
    } else
        return Error("unknown_item");
    if (*stock + quantity > 999)
        return Error("inventory_full");
    if (next.coins < static_cast<uint32_t>(quantity * price))
        return Error("insufficient_coins");
    next.coins -= quantity * price;
    *stock += quantity;
    return Commit(std::move(next));
}

LearningResult LearningEngine::Use(const std::string& item, LearningTime now, uint32_t revision) {
    auto check = Check(now, revision);
    if (!check.ok)
        return check;
    auto next = state_;
    Advance(next, now);
    auto& life = next.life;
    if (item == "no_outfit")
        life.outfit = 0;
    else if (item == "default_room")
        life.room = 0;
    else {
        const auto* product = FindPetItem(item);
        if (!product || !product->mask)
            return Error("unknown_item");
        if (!(life.owned & product->mask))
            return Error("not_owned");
        if (product->kind == PetItemKind::kToy) {
            if (IsSleeping(now))
                return Error("pet_sleeping");
            if (life.energy < 10)
                return Error("too_tired");
            if (life.last_play && next.observed - life.last_play < 300)
                return Error("play_cooldown");
            life.energy -= 5;
            life.happiness = PetMeter(life.happiness + (product->code == 1 ? 15 : 25));
            life.last_play = next.observed;
            next.happy_until = next.observed + 600;
            if (life.last_play_date < next.date) {
                life.last_play_date = next.date;
                ++life.play_days;
            }
        } else if (product->kind == PetItemKind::kOutfit)
            life.outfit = product->code;
        else if (product->kind == PetItemKind::kRoom)
            life.room = product->code;
    }
    return Commit(std::move(next));
}

WordProgress* LearningEngine::Progress(LearningState& state, const std::string& word) {
    for (auto& p : state.history)
        if (p.word == word)
            return &p;
    return nullptr;
}

LearningResult LearningEngine::Import(std::vector<LearningWord> book, LearningTime now,
                                      uint32_t revision) {
    auto check = Check(now, revision, false);
    if (!check.ok)
        return check;
    if (state_.active)
        return Error("study_active");
    if (book.empty() || book.size() > 1000)
        return Error("invalid_book_size");
    auto next = state_;
    Advance(next, now);
    std::vector<std::string> seen;
    for (auto& word : book) {
        if (!NormalizeWord(word.word) || !ValidText(word.meaning, 120))
            return Error("invalid_word");
        if (std::find(seen.begin(), seen.end(), word.word) != seen.end())
            return Error("duplicate_word");
        seen.push_back(word.word);
        auto* p = Progress(next, word.word);
        if (!p) {
            if (next.history.size() >= 2000)
                return Error("history_full");
            next.history.push_back({word.word});
            p = &next.history.back();
        }
        const auto hash = TextHash(word.meaning);
        if (p->meaning_hash != hash) {
            p->meaning_hash = hash;
            p->stage = 0;
            p->due = 0;
        }
    }
    const auto encoded = EncodeBook(book);
    next.book_slot = 1 - state_.book_slot;
    next.book_hash = BookHash(encoded);
    if (!store_.SaveBook(next.book_slot, encoded))
        return Error("book_write_failed");
    auto result = Commit(std::move(next));
    if (result.ok)
        book_ = std::move(book);
    return result;
}

const LearningWord* LearningEngine::CurrentWord() const {
    if (!state_.active || state_.question >= state_.questions.size())
        return nullptr;
    for (const auto& w : book_)
        if (w.word == state_.questions[state_.question])
            return &w;
    return nullptr;
}

std::string LearningEngine::QuestionPrompt() const {
    const auto* word = CurrentWord();
    if (!word)
        return {};
    return state_.chinese_prompt ? word->meaning + "，这个用英语怎么说？"
                                 : word->word + "，这个是什么意思？";
}

LearningProgress LearningEngine::GetProgress(LearningTime now) const {
    LearningProgress result;
    const auto observed = now.Valid() ? std::max(now.epoch, state_.observed) : state_.observed;
    for (const auto& word : book_) {
        const auto p = std::find_if(state_.history.begin(), state_.history.end(),
                                    [&](const auto& p) { return p.word == word.word; });
        if (p != state_.history.end() && p->stage >= kMasteredStage) {
            ++result.mastered;
        } else if (p == state_.history.end() || !p->due) {
            ++result.unlearned;
        } else {
            ++result.consolidating;
            if (p->due <= observed)
                ++result.due;
            if (!result.next_review || p->due < result.next_review)
                result.next_review = p->due;
        }
    }
    return result;
}

LearningResult LearningEngine::Start(bool chinese_prompt, LearningTime now, uint32_t revision,
                                     bool review_mastered) {
    auto check = Check(now, revision);
    if (!check.ok)
        return check;
    if (state_.active)
        return IsSleeping(now) ? Care("wake", now, revision) : LearningResult{true, {}, 0};
    if (book_.empty())
        return Error("no_words");
    auto next = state_;
    Advance(next, now);
    std::vector<WordProgress*> candidates;
    for (const auto& w : book_) {
        auto* p = Progress(next, w.word);
        const bool mastered = p->stage >= kMasteredStage;
        if (review_mastered ? mastered : (!mastered && (!p->due || p->due <= next.observed)))
            candidates.push_back(p);
    }
    if (candidates.empty())
        return Error(review_mastered                             ? "no_mastered_words"
                     : GetProgress(now).mastered == book_.size() ? "all_mastered"
                                                                 : "no_words_due");
    std::stable_sort(candidates.begin(), candidates.end(), [&](auto* a, auto* b) {
        if (review_mastered)
            return a->completed_date < b->completed_date;
        if (bool(a->due) != bool(b->due))
            return bool(a->due);  // Due reviews precede new words in CSV order.
        return a->due < b->due;
    });
    next.questions.clear();
    next.target = std::min<size_t>(5, candidates.size());
    for (size_t i = 0; i < next.target; ++i)
        next.questions.push_back(candidates[i]->word);
    ++next.session;
    next.question = 0;
    next.active = true;
    next.chinese_prompt = chinese_prompt;
    next.session_coins = 0;
    next.life.sleep_until = 0;
    next.life.awake_until = next.observed + 2 * 3600;
    return Commit(std::move(next));
}

LearningResult LearningEngine::Answer(uint32_t session, uint8_t question,
                                      const std::string& verdict, LearningTime now,
                                      uint32_t /*legacy_revision*/) {
    // Session + question identify a single answer transaction. Global revision can
    // change at Start(), during care, or during hourly maintenance while listening.
    auto check = Check(now, state_.revision);
    if (!check.ok)
        return check;
    if (!state_.active || session != state_.session || question != state_.question)
        return Error("stale_question");
    if (IsSleeping(now))
        return Error("pet_sleeping");
    if (verdict == "unclear")
        return {true, {}, 0};
    if (verdict != "correct" && verdict != "hinted" && verdict != "wrong" &&
        verdict != "corrected" && verdict != "skip")
        return Error("invalid_verdict");
    auto next = state_;
    Advance(next, now);
    const auto& word = next.questions[next.question];
    auto* p = Progress(next, word);
    const bool correction = next.question >= next.target;
    if ((correction && (verdict == "correct" || verdict == "hinted")) ||
        (!correction && verdict == "corrected"))
        return Error("invalid_verdict_for_question");
    uint32_t award = 0;
    if (verdict == "wrong") {
        p->stage = 0;
        p->due = next.observed + kDay;
        if (!correction)
            next.questions.push_back(word);
    } else if (verdict != "skip") {
        p->completed_date = next.date;
        if (!correction && verdict == "correct") {
            constexpr int intervals[] = {1, 3};
            if (p->stage < kMasteredStage && (!p->due || p->due <= next.observed)) {
                ++p->stage;
                // Keep due nonzero to distinguish learned words from new words in old saves.
                p->due = next.observed +
                         (p->stage < kMasteredStage ? intervals[p->stage - 1] * kDay : 0);
            }
        } else {
            p->stage = 0;
            p->due = next.observed + kDay;
        }
        if (p->reward_date != next.date && !correction && verdict != "corrected") {
            p->reward_date = next.date;
            award += verdict == "correct" ? 2 : 1;
        }
    }
    size_t completed = 0;
    for (const auto& w : book_)
        if (Progress(next, w.word)->completed_date == next.date)
            ++completed;
    if (!next.daily_paid && completed >= std::min<size_t>(5, book_.size())) {
        next.daily_paid = true;
        award += 10;
    }
    award = std::min<uint32_t>(award, 40 - next.earned_today);
    next.earned_today += award;
    next.coins += award;
    next.session_coins += award;
    if (++next.question >= next.questions.size())
        next.active = false;
    return Commit(std::move(next), award);
}

LearningResult LearningEngine::Stop(LearningTime now, uint32_t revision) {
    auto check = Check(now, revision);
    if (!check.ok)
        return check;
    if (!state_.active)
        return {true, {}, 0};
    auto next = state_;
    Advance(next, now);
    next.active = false;
    return Commit(std::move(next));
}
}  // namespace maomi
