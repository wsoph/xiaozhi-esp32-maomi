// Desktop harness only. All game rules and save encoding come from the firmware.
#include "maomi_learning.h"

#include <ctime>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
class MemoryStore : public maomi::LearningStore {
public:
    std::vector<uint8_t> state;
    std::vector<uint8_t> books[2];
    bool LoadState(std::vector<uint8_t>& out) override {
        out = state;
        return true;
    }
    bool SaveState(const std::vector<uint8_t>& data) override {
        state = data;
        return true;
    }
    bool LoadBook(int slot, std::vector<uint8_t>& out) override {
        out = books[slot];
        return true;
    }
    bool SaveBook(int slot, const std::vector<uint8_t>& data) override {
        books[slot] = data;
        return true;
    }
};

std::string Unhex(const std::string& text) {
    if (text == "-")
        return {};
    if (text.size() % 2)
        throw std::runtime_error("invalid_hex");
    std::string result;
    auto digit = [](char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw std::runtime_error("invalid_hex");
    };
    for (size_t i = 0; i < text.size(); i += 2)
        result += static_cast<char>((digit(text[i]) << 4) | digit(text[i + 1]));
    return result;
}

std::string Quote(const std::string& text) {
    std::string result = "\"";
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '\"' || c == '\\') {
            result += '\\';
            result += c;
        } else if (c < 32) {
            result += "\\u00";
            result += digits[c >> 4];
            result += digits[c & 15];
        } else
            result += c;
    }
    return result + "\"";
}

maomi::LearningTime Time(int64_t epoch) {
    const auto value = static_cast<std::time_t>(epoch);
    const auto local = *std::localtime(&value);
    return {epoch, (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday,
            local.tm_mon + 1, local.tm_mday};
}

void Print(const maomi::LearningEngine& engine, maomi::LearningTime now,
           const maomi::LearningResult& result) {
    const auto& s = engine.GetState();
    std::cout << "{\"ok\":" << (result.ok ? "true" : "false")
              << ",\"error\":" << Quote(result.error);
    auto number = [](const char* key, int64_t value) {
        std::cout << ',' << Quote(key) << ':' << value;
    };
    auto text = [](const char* key, const std::string& value) {
        std::cout << ',' << Quote(key) << ':' << Quote(value);
    };
    text("name", s.name);
    text("mood", engine.Mood(now));
    text("mode", s.chinese_prompt ? "zh_en" : "en_zh");
    text("growth", s.care_days >= 21 ? "成年猫" : s.care_days >= 7 ? "少年猫" : "幼猫");
    number("epoch", now.epoch);
    number("revision", s.revision);
    number("birthday", s.birthday);
    number("age_days", engine.AgeDays(now));
    number("birthday_today", engine.IsBirthday(now));
    number("care_days", s.care_days);
    number("satiety", s.satiety);
    number("poop", s.poop);
    number("coins", s.coins);
    number("food", s.food);
    number("litter", s.litter);
    number("snacks", s.snacks);
    number("earned_today", s.earned_today);
    number("coins_added", result.coins_added);
    number("word_count", engine.GetBook().size());
    const auto progress = engine.GetProgress(now);
    number("unlearned_count", progress.unlearned);
    number("consolidating_count", progress.consolidating);
    number("mastered_count", progress.mastered);
    number("due_count", progress.due);
    number("next_review", progress.next_review);
    number("active", s.active);
    number("session", s.session);
    number("question", s.question);
    number("target", s.target);
    number("session_coins", s.session_coins);
    number("correction", s.active && s.question >= s.target);
    if (const auto* word = engine.CurrentWord()) {
        text("word", word->word);
        text("meaning", word->meaning);
        text("question_prompt", engine.QuestionPrompt());
    }
    std::cout << "}" << std::endl;
}
}  // namespace

int main() {
    MemoryStore store;
    maomi::LearningEngine engine(store);
    if (!engine.Load())
        return 1;
    int64_t epoch = std::time(nullptr);
    for (std::string line; std::getline(std::cin, line);) {
        try {
            if (line.size() > 400000)
                throw std::runtime_error("request_too_large");
            std::istringstream input(line);
            std::vector<std::string> args;
            for (std::string token; input >> token;)
                args.push_back(Unhex(token));
            const auto& op = args.at(0);
            const auto now = Time(epoch);
            const auto& s = engine.GetState();
            maomi::LearningResult result{true, {}, 0};
            if (op == "adopt")
                result = engine.Adopt(args.at(1), now, s.revision);
            else if (op == "care")
                result = engine.Care(args.at(1), now, s.revision);
            else if (op == "buy")
                result = engine.Buy(args.at(1), std::stoi(args.at(2)), now, s.revision);
            else if (op == "start")
                result = engine.Start(args.at(1) == "zh_en", now, s.revision,
                                      args.size() > 2 && args[2] == "review_mastered");
            else if (op == "answer")
                result = engine.Answer(s.session, s.question, args.at(1), now, s.revision);
            else if (op == "stop")
                result = engine.Stop(now, s.revision);
            else if (op == "restart")
                result.ok = engine.Load();
            else if (op == "reset") {
                store = {};
                result.ok = engine.Load();
                epoch = std::time(nullptr);
            } else if (op == "advance") {
                const int hours = std::stoi(args.at(1));
                if (hours < 0 || hours > 8760 || !Time(epoch + int64_t(hours) * 3600).Valid())
                    throw std::runtime_error("invalid_time");
                epoch += int64_t(hours) * 3600;
                if (!s.name.empty())
                    result = engine.Observe(Time(epoch));
            } else if (op == "import") {
                if (args.size() % 2 != 1)
                    throw std::runtime_error("invalid_book");
                std::vector<maomi::LearningWord> book;
                for (size_t i = 1; i < args.size(); i += 2)
                    book.push_back({args[i], args[i + 1]});
                result = engine.Import(std::move(book), now, s.revision);
            } else if (op != "status")
                throw std::runtime_error("invalid_command");
            Print(engine, Time(epoch), result);
        } catch (const std::exception&) {
            std::cout << "{\"ok\":false,\"error\":\"invalid_simulator_command\"}" << std::endl;
        }
    }
}
