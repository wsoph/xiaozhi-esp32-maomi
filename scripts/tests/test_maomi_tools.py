import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main" / "boards" / "zhengchen" / "1.54tft-wifi-maomi"


def find_host_compiler():
    candidates = []
    if os.environ.get("CXX"):
        candidates.append(Path(os.environ["CXX"]))
    for name in ("g++", "clang++"):
        resolved = shutil.which(name)
        if resolved:
            candidates.append(Path(resolved))
    candidates.append(
        ROOT.parent
        / "tool-cache"
        / "w64devkit-2.9.1"
        / "w64devkit"
        / "bin"
        / "g++.exe"
    )
    return next((candidate for candidate in candidates if candidate.is_file()), None)


MCP_SERVER_STUB = r"""
#pragma once

#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

using ReturnValue = std::string;

enum PropertyType {
    kPropertyTypeBoolean,
    kPropertyTypeInteger,
    kPropertyTypeString,
};

class Property {
public:
    Property(const std::string& name, PropertyType type)
        : name_(name), type_(type), required_(true) {}

    template <typename T>
    Property(const std::string& name, PropertyType type, const T& default_value)
        : name_(name), type_(type), value_(default_value), required_(false) {}

    Property(const std::string& name, PropertyType type, int min_value, int max_value)
        : name_(name), type_(type), required_(true), has_range_(true),
          min_value_(min_value), max_value_(max_value) {}

    Property(const std::string& name, PropertyType type, int default_value, int min_value,
             int max_value)
        : name_(name), type_(type), value_(default_value), required_(false), has_range_(true),
          min_value_(min_value), max_value_(max_value) {}

    const std::string& name() const { return name_; }
    PropertyType type() const { return type_; }
    bool has_default_value() const { return !required_; }
    bool has_range() const { return has_range_; }
    int min_value() const { return min_value_; }
    int max_value() const { return max_value_; }

    template <typename T>
    T value() const {
        return std::get<T>(value_);
    }

    template <typename T>
    void set_value(const T& value) {
        if constexpr (std::is_same_v<T, int>) {
            if (has_range_ && (value < min_value_ || value > max_value_)) {
                throw std::invalid_argument("Integer argument is outside its allowed range");
            }
        }
        value_ = value;
    }

private:
    std::string name_;
    PropertyType type_;
    std::variant<bool, int, std::string> value_ = false;
    bool required_ = true;
    bool has_range_ = false;
    int min_value_ = 0;
    int max_value_ = 0;
};

class PropertyList {
public:
    PropertyList() = default;
    PropertyList(const std::vector<Property>& properties) : properties_(properties) {}

    const Property& operator[](const std::string& name) const {
        for (const auto& property : properties_) {
            if (property.name() == name) {
                return property;
            }
        }
        throw std::runtime_error("Property not found: " + name);
    }

    auto begin() { return properties_.begin(); }
    auto end() { return properties_.end(); }
    auto begin() const { return properties_.begin(); }
    auto end() const { return properties_.end(); }

private:
    std::vector<Property> properties_;
};

class McpServer {
public:
    using Argument = std::variant<bool, int, std::string>;
    using Arguments = std::unordered_map<std::string, Argument>;

    struct Tool {
        std::string name;
        std::string description;
        PropertyList properties;
        std::function<ReturnValue(const PropertyList&)> callback;
    };

    void AddTool(const std::string& name, const std::string& description,
                 const PropertyList& properties,
                 std::function<ReturnValue(const PropertyList&)> callback) {
        tools.push_back({name, description, properties, std::move(callback)});
    }

    std::string Invoke(const std::string& name, const Arguments& supplied = {}) {
        std::lock_guard<std::mutex> lock(invoke_mutex_);
        for (const auto& tool : tools) {
            if (tool.name != name) {
                continue;
            }
            PropertyList arguments = tool.properties;
            for (auto& property : arguments) {
                const auto found = supplied.find(property.name());
                if (found == supplied.end()) {
                    if (!property.has_default_value()) {
                        throw std::runtime_error("Missing valid argument: " + property.name());
                    }
                    continue;
                }
                if (property.type() == kPropertyTypeBoolean &&
                    std::holds_alternative<bool>(found->second)) {
                    property.set_value(std::get<bool>(found->second));
                } else if (property.type() == kPropertyTypeInteger &&
                           std::holds_alternative<int>(found->second)) {
                    property.set_value(std::get<int>(found->second));
                } else if (property.type() == kPropertyTypeString &&
                           std::holds_alternative<std::string>(found->second)) {
                    property.set_value(std::get<std::string>(found->second));
                } else {
                    throw std::runtime_error("Missing valid argument: " + property.name());
                }
            }
            return tool.callback(arguments);
        }
        throw std::runtime_error("Unknown tool: " + name);
    }

    std::vector<Tool> tools;

private:
    std::mutex invoke_mutex_;
};
"""


CONTRACT_TEST = r"""
#include "maomi_tools.h"
#include "mcp_server.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

int error_expectation = 0;

template <typename Callback>
void ExpectError(Callback callback) {
    ++error_expectation;
    bool failed = false;
    try {
        callback();
    } catch (const std::exception&) {
        failed = true;
    }
    if (!failed) {
        std::cerr << "Expected error was not raised at check " << error_expectation << std::endl;
    }
    assert(failed);
}

const McpServer::Tool& FindTool(const McpServer& server, std::string_view name) {
    const auto found = std::find_if(server.tools.begin(), server.tools.end(),
                                    [&name](const McpServer::Tool& tool) {
                                        return tool.name == name;
                                    });
    assert(found != server.tools.end());
    return *found;
}

void AssertSchema(const McpServer& server) {
    assert(server.tools.size() == 9);
    std::array<std::string, 9> names = {
        server.tools[0].name,
        server.tools[1].name,
        server.tools[2].name,
        server.tools[3].name,
        server.tools[4].name,
        server.tools[5].name,
        server.tools[6].name,
        server.tools[7].name,
        server.tools[8].name,
    };
    std::sort(names.begin(), names.end());
    assert(std::adjacent_find(names.begin(), names.end()) == names.end());

    const auto& interact = FindTool(server, maomi::kPetInteractToolName);
    const auto& status = FindTool(server, maomi::kPetStatusToolName);
    const auto& quiet = FindTool(server, maomi::kPetQuietToolName);
    assert(!interact.description.empty());
    assert(!status.description.empty());
    assert(!quiet.description.empty());

    size_t interact_fields = 0;
    for (const auto& property : interact.properties) {
        ++interact_fields;
        assert(property.name() == "action");
        assert(property.type() == kPropertyTypeString);
        assert(!property.has_default_value());
    }
    assert(interact_fields == 1);

    size_t status_fields = 0;
    for (const auto& property : status.properties) {
        static_cast<void>(property);
        ++status_fields;
    }
    assert(status_fields == 0);

    size_t quiet_fields = 0;
    for (const auto& property : quiet.properties) {
        ++quiet_fields;
        assert(property.name() == "enabled");
        assert(property.type() == kPropertyTypeBoolean);
        assert(!property.has_default_value());
    }
    assert(quiet_fields == 1);

    const auto& countdown = FindTool(server, maomi::kCountdownToolName);
    const auto& alarm = FindTool(server, maomi::kAlarmToolName);
    const auto& interval = FindTool(server, maomi::kIntervalReminderToolName);
    const auto& pomodoro = FindTool(server, maomi::kPomodoroToolName);
    const auto& list = FindTool(server, maomi::kReminderListToolName);
    const auto& cancel = FindTool(server, maomi::kReminderCancelToolName);
    assert(!countdown.description.empty());
    assert(!alarm.description.empty());
    assert(!interval.description.empty());
    assert(!pomodoro.description.empty());
    assert(!list.description.empty());
    assert(!cancel.description.empty());

    const auto assert_integer = [](const Property& property, std::string_view name, int minimum,
                                   int maximum) {
        assert(property.name() == name);
        assert(property.type() == kPropertyTypeInteger);
        assert(property.has_range());
        assert(property.min_value() == minimum);
        assert(property.max_value() == maximum);
    };

    size_t countdown_fields = 0;
    for (const auto& property : countdown.properties) {
        if (countdown_fields == 0) {
            assert_integer(property, "duration_seconds", 1, 86400);
            assert(!property.has_default_value());
        } else {
            assert(property.name() == "label");
            assert(property.type() == kPropertyTypeString);
            assert(property.has_default_value());
        }
        ++countdown_fields;
    }
    assert(countdown_fields == 2);

    size_t alarm_fields = 0;
    for (const auto& property : alarm.properties) {
        if (alarm_fields == 0) {
            assert(property.name() == "date");
            assert(property.type() == kPropertyTypeString);
            assert(!property.has_default_value());
        } else if (alarm_fields == 1) {
            assert_integer(property, "hour", 0, 23);
            assert(!property.has_default_value());
        } else if (alarm_fields == 2) {
            assert_integer(property, "minute", 0, 59);
            assert(!property.has_default_value());
        } else {
            assert(property.name() == "label");
            assert(property.type() == kPropertyTypeString);
            assert(property.has_default_value());
        }
        ++alarm_fields;
    }
    assert(alarm_fields == 4);

    size_t interval_fields = 0;
    for (const auto& property : interval.properties) {
        if (interval_fields == 0) {
            assert(property.name() == "kind");
            assert(property.type() == kPropertyTypeString);
            assert(!property.has_default_value());
        } else if (interval_fields == 1) {
            assert_integer(property, "interval_minutes", 10, 720);
            assert(!property.has_default_value());
        } else {
            assert(property.name() == "label");
            assert(property.type() == kPropertyTypeString);
            assert(property.has_default_value());
        }
        ++interval_fields;
    }
    assert(interval_fields == 3);

    size_t pomodoro_fields = 0;
    for (const auto& property : pomodoro.properties) {
        if (pomodoro_fields == 0) {
            assert_integer(property, "work_minutes", 1, 120);
        } else if (pomodoro_fields == 1) {
            assert_integer(property, "break_minutes", 1, 60);
        } else {
            assert_integer(property, "cycles", 1, 12);
        }
        assert(!property.has_default_value());
        ++pomodoro_fields;
    }
    assert(pomodoro_fields == 3);

    size_t list_fields = 0;
    for (const auto& property : list.properties) {
        static_cast<void>(property);
        ++list_fields;
    }
    assert(list_fields == 0);

    size_t cancel_fields = 0;
    for (const auto& property : cancel.properties) {
        assert_integer(property, "id", 1, 65535);
        assert(!property.has_default_value());
        ++cancel_fields;
    }
    assert(cancel_fields == 1);
}

}  // namespace

int main() {
    McpServer server;
    std::atomic<int> interactions{0};
    std::atomic<bool> reject_interactions{false};
    std::atomic<int> quiet_updates{0};
    std::atomic<bool> quiet{false};

    maomi::PetToolDependencies dependencies;
    dependencies.interact = [&interactions, &reject_interactions](maomi::PetAction action) {
        ++interactions;
        maomi::InteractionToolResult result;
        result.state = reject_interactions ? maomi::ToolOperationState::kRejected
                                           : maomi::ToolOperationState::kQueued;
        result.action = action;
        result.points_added = action == maomi::PetAction::kPlay ? 3 : 2;
        result.bond_points = 17 + result.points_added;
        result.sound_queued = true;
        return result;
    };
    dependencies.get_status = [&quiet]() {
        maomi::PetToolSnapshot snapshot;
        snapshot.bond_points = 20;
        snapshot.bond_level = maomi::BondLevel::kFamiliar;
        snapshot.companion_days = 4;
        snapshot.mood = maomi::PetState::kHappy;
        snapshot.battery_level = 76;
        snapshot.charging = false;
        snapshot.manual_quiet = quiet.load();
        snapshot.active_reminders = 0;
        return snapshot;
    };
    dependencies.set_quiet = [&quiet, &quiet_updates](bool enabled) {
        ++quiet_updates;
        quiet = enabled;
        maomi::QuietToolResult result;
        result.state = maomi::ToolOperationState::kCompleted;
        result.enabled = enabled;
        result.persistence_pending = false;
        return result;
    };

    maomi::ReminderList reminders;
    uint16_t next_reminder_id = 1;
    std::atomic<int> reminder_mutations{0};
    const auto add_reminder = [&reminders, &next_reminder_id, &reminder_mutations](
                                  maomi::ReminderKind kind, bool persistent,
                                  std::string_view label) {
        assert(reminders.count < reminders.items.size());
        auto& snapshot = reminders.items[reminders.count++];
        snapshot.id = next_reminder_id++;
        snapshot.kind = kind;
        snapshot.persistent = persistent;
        std::copy(label.begin(), label.end(), snapshot.label.begin());
        snapshot.label[label.size()] = '\0';
        ++reminder_mutations;
        return maomi::ReminderResult{
            .status = maomi::ReminderStatus::kAccepted,
            .id = snapshot.id,
            .kind = kind,
            .persistent = persistent,
        };
    };

    maomi::ReminderToolDependencies reminder_dependencies;
    reminder_dependencies.start_countdown =
        [&add_reminder, &reminders](uint32_t duration_seconds, std::string_view label) {
            auto result = add_reminder(maomi::ReminderKind::kCountdown, false, label);
            reminders.items[reminders.count - 1].remaining_ms = duration_seconds * 1000ULL;
            return result;
        };
    reminder_dependencies.set_alarm =
        [&add_reminder, &reminders](const maomi::DateTime& target, std::string_view label) {
            assert(target.second == 0);
            auto result = add_reminder(maomi::ReminderKind::kAlarm, true, label);
            reminders.items[reminders.count - 1].next_wall_time_seconds =
                1800000000 + target.hour * 3600 + target.minute * 60;
            return result;
        };
    reminder_dependencies.start_interval =
        [&add_reminder, &reminders](maomi::ReminderKind kind, uint32_t interval_minutes,
                                    std::string_view label) {
            auto result = add_reminder(kind, true, label);
            auto& snapshot = reminders.items[reminders.count - 1];
            snapshot.interval_seconds = interval_minutes * 60;
            snapshot.next_wall_time_seconds = 1800000000 + snapshot.interval_seconds;
            return result;
        };
    reminder_dependencies.start_pomodoro =
        [&add_reminder, &reminders](uint32_t work_minutes, uint32_t break_minutes,
                                    uint32_t cycles) {
            assert(break_minutes >= 1);
            auto result = add_reminder(maomi::ReminderKind::kPomodoro, false, {});
            auto& snapshot = reminders.items[reminders.count - 1];
            snapshot.phase = maomi::ReminderPhase::kWork;
            snapshot.remaining_ms = work_minutes * 60 * 1000ULL;
            snapshot.total_cycles = static_cast<uint8_t>(cycles);
            return result;
        };
    reminder_dependencies.cancel =
        [&reminders, &reminder_mutations](uint16_t id) -> maomi::ReminderResult {
        for (size_t index = 0; index < reminders.count; ++index) {
            if (reminders.items[index].id != id) {
                continue;
            }
            const auto removed = reminders.items[index];
            for (size_t move = index + 1; move < reminders.count; ++move) {
                reminders.items[move - 1] = reminders.items[move];
            }
            reminders.items[--reminders.count] = {};
            ++reminder_mutations;
            return {
                .status = maomi::ReminderStatus::kCancelled,
                .id = id,
                .kind = removed.kind,
                .persistent = removed.persistent,
            };
        }
        return {.status = maomi::ReminderStatus::kNotFound};
    };
    reminder_dependencies.list = [&reminders]() { return reminders; };

    maomi::RegisterPetTools(server, std::move(dependencies));
    maomi::RegisterReminderTools(server, std::move(reminder_dependencies));
    AssertSchema(server);

    const auto pet = server.Invoke(maomi::kPetInteractToolName, {{"action", std::string("pet")}});
    const auto feed =
        server.Invoke(maomi::kPetInteractToolName, {{"action", std::string("feed")}});
    const auto play =
        server.Invoke(maomi::kPetInteractToolName, {{"action", std::string("play")}});
    assert(pet.find("\"action\":\"pet\"") != std::string::npos);
    assert(feed.find("\"action\":\"feed\"") != std::string::npos);
    assert(play.find("\"action\":\"play\"") != std::string::npos);
    assert(play.find("\"status\":\"queued\"") != std::string::npos);
    assert(play.find("\"points_added\":3") != std::string::npos);
    assert(interactions == 3);

    ExpectError([&server]() { server.Invoke(maomi::kPetInteractToolName); });
    ExpectError([&server]() {
        server.Invoke(maomi::kPetInteractToolName, {{"action", true}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kPetInteractToolName, {{"action", std::string("delete")}});
    });
    assert(interactions == 3);
    reject_interactions = true;
    ExpectError([&server]() {
        server.Invoke(maomi::kPetInteractToolName, {{"action", std::string("pet")}});
    });
    reject_interactions = false;
    assert(interactions == 4);

    const auto status = server.Invoke(maomi::kPetStatusToolName);
    assert(status.find("\"name\":\"小猫咪\"") != std::string::npos);
    assert(status.find("\"bond_points\":20") != std::string::npos);
    assert(status.find("\"bond_level\":\"familiar\"") != std::string::npos);
    assert(status.find("\"mood\":\"happy\"") != std::string::npos);

    const auto enabled = server.Invoke(maomi::kPetQuietToolName, {{"enabled", true}});
    assert(enabled.find("\"manual_quiet\":true") != std::string::npos);
    assert(quiet);
    ExpectError([&server]() { server.Invoke(maomi::kPetQuietToolName); });
    ExpectError([&server]() {
        server.Invoke(maomi::kPetQuietToolName, {{"enabled", std::string("true")}});
    });
    assert(quiet_updates == 1);

    const auto countdown = server.Invoke(
        maomi::kCountdownToolName,
        {{"duration_seconds", 1}, {"label", std::string("desk \\\"A\\\"\\\\")}});
    assert(countdown.find("\"type\":\"countdown\"") != std::string::npos);
    assert(countdown.find("\"persistent\":false") != std::string::npos);
    assert(countdown.find("\"remaining_seconds\":1") != std::string::npos);
    assert(countdown.find("desk \\\\\\\"A\\\\\\\"\\\\\\\\") != std::string::npos);

    const auto alarm = server.Invoke(maomi::kAlarmToolName,
                                     {{"date", std::string("2028-02-29")},
                                      {"hour", 23},
                                      {"minute", 59}});
    assert(alarm.find("\"type\":\"alarm\"") != std::string::npos);
    assert(alarm.find("\"persistent\":true") != std::string::npos);
    assert(alarm.find("\"next_wall_time_seconds\":") != std::string::npos);

    const auto water = server.Invoke(maomi::kIntervalReminderToolName,
                                     {{"kind", std::string("water")},
                                      {"interval_minutes", 10},
                                      {"label", std::string("water")}});
    const auto sedentary = server.Invoke(maomi::kIntervalReminderToolName,
                                         {{"kind", std::string("sedentary")},
                                          {"interval_minutes", 720}});
    assert(water.find("\"type\":\"water\"") != std::string::npos);
    assert(sedentary.find("\"type\":\"sedentary\"") != std::string::npos);

    const auto pomodoro = server.Invoke(maomi::kPomodoroToolName,
                                        {{"work_minutes", 25},
                                         {"break_minutes", 5},
                                         {"cycles", 4}});
    assert(pomodoro.find("\"type\":\"pomodoro\"") != std::string::npos);
    assert(pomodoro.find("\"phase\":\"work\"") != std::string::npos);
    assert(pomodoro.find("\"total_cycles\":4") != std::string::npos);

    const auto reminder_list = server.Invoke(maomi::kReminderListToolName);
    assert(reminder_list.find("\"count\":5") != std::string::npos);
    assert(reminder_list.find("\"reminders\":[") != std::string::npos);
    const auto cancelled =
        server.Invoke(maomi::kReminderCancelToolName, {{"id", 1}});
    assert(cancelled.find("\"status\":\"cancelled\"") != std::string::npos);
    assert(cancelled.find("\"id\":1") != std::string::npos);
    assert(reminder_mutations == 6);

    ExpectError([&server]() {
        server.Invoke(maomi::kCountdownToolName, {{"duration_seconds", 0}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kCountdownToolName,
                      {{"duration_seconds", 1}, {"label", std::string(33, 'x')}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kCountdownToolName,
                      {{"duration_seconds", 1}, {"label", std::string("bad\nlabel")}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kCountdownToolName,
                      {{"duration_seconds", 1}, {"label", std::string("\xC3", 1)}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kAlarmToolName,
                      {{"date", std::string("2027-02-29")}, {"hour", 12}, {"minute", 0}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kAlarmToolName,
                      {{"date", std::string("2028/02/29")}, {"hour", 12}, {"minute", 0}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kAlarmToolName,
                      {{"date", std::string("2028-02-29")}, {"hour", 24}, {"minute", 0}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kIntervalReminderToolName,
                      {{"kind", std::string("walk")}, {"interval_minutes", 30}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kPomodoroToolName,
                      {{"work_minutes", 25}, {"break_minutes", 0}, {"cycles", 4}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kReminderCancelToolName, {{"id", 65536}});
    });
    ExpectError([&server]() {
        server.Invoke(maomi::kReminderCancelToolName, {{"id", 65535}});
    });
    assert(reminder_mutations == 6);

    constexpr int kThreadCount = 4;
    constexpr int kCallsPerThread = 250;
    std::vector<std::thread> callers;
    for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
        callers.emplace_back([&server]() {
            for (int call = 0; call < kCallsPerThread; ++call) {
                const auto response = server.Invoke(
                    maomi::kPetInteractToolName, {{"action", std::string("pet")}});
                assert(response.find("\"status\":\"queued\"") != std::string::npos);
            }
        });
    }
    for (auto& caller : callers) {
        caller.join();
    }
    assert(interactions == 4 + kThreadCount * kCallsPerThread);

    McpServer unavailable_server;
    maomi::RegisterPetTools(unavailable_server, {});
    maomi::RegisterReminderTools(unavailable_server, {});
    ExpectError([&unavailable_server]() {
        unavailable_server.Invoke(maomi::kPetInteractToolName,
                                  {{"action", std::string("pet")}});
    });
    ExpectError([&unavailable_server]() {
        unavailable_server.Invoke(maomi::kPetStatusToolName);
    });
    ExpectError([&unavailable_server]() {
        unavailable_server.Invoke(maomi::kPetQuietToolName, {{"enabled", true}});
    });
    ExpectError([&unavailable_server]() {
        unavailable_server.Invoke(maomi::kCountdownToolName, {{"duration_seconds", 60}});
    });
    ExpectError([&unavailable_server]() {
        unavailable_server.Invoke(maomi::kAlarmToolName,
                                  {{"date", std::string("2028-02-29")},
                                   {"hour", 12},
                                   {"minute", 0}});
    });
    ExpectError([&unavailable_server]() {
        unavailable_server.Invoke(maomi::kIntervalReminderToolName,
                                  {{"kind", std::string("water")},
                                   {"interval_minutes", 30}});
    });
    ExpectError([&unavailable_server]() {
        unavailable_server.Invoke(maomi::kPomodoroToolName,
                                  {{"work_minutes", 25},
                                   {"break_minutes", 5},
                                   {"cycles", 4}});
    });
    ExpectError([&unavailable_server]() {
        unavailable_server.Invoke(maomi::kReminderListToolName);
    });
    ExpectError([&unavailable_server]() {
        unavailable_server.Invoke(maomi::kReminderCancelToolName, {{"id", 1}});
    });

    std::cout << "maomi MCP tool contract tests passed" << std::endl;
    return 0;
}
"""


class MaomiToolsContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = find_host_compiler()
        if cls.compiler is None:
            raise RuntimeError("required host C++ compiler is unavailable")

    def test_tools_list_validation_and_concurrency_contract(self):
        with tempfile.TemporaryDirectory(prefix="maomi-tools-") as temporary:
            temporary_path = Path(temporary)
            (temporary_path / "mcp_server.h").write_text(
                textwrap.dedent(MCP_SERVER_STUB), encoding="utf-8"
            )
            contract_test = temporary_path / "maomi_tools_contract_test.cc"
            contract_test.write_text(textwrap.dedent(CONTRACT_TEST), encoding="utf-8")
            executable = temporary_path / (
                "maomi_tools_contract_test.exe"
                if os.name == "nt"
                else "maomi_tools_contract_test"
            )
            environment = os.environ.copy()
            environment["PATH"] = os.pathsep.join(
                [str(self.compiler.parent), environment.get("PATH", "")]
            )
            command = [
                str(self.compiler),
                "-std=c++20",
                "-pthread",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(temporary_path),
                "-I",
                str(ROOT / "main"),
                "-I",
                str(BOARD),
                str(contract_test),
                str(BOARD / "maomi_tools.cc"),
                "-o",
                str(executable),
            ]
            compiled = subprocess.run(
                command,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                env=environment,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            completed = subprocess.run(
                [str(executable)],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                env=environment,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_board_connects_reminder_tools_and_due_presentation(self):
        board_source = (
            BOARD / "zhengchen-1.54tft-wifi-maomi.cc"
        ).read_text(encoding="utf-8")

        self.assertIn("RegisterReminderTools(", board_source)
        for operation in (
            "StartCountdown(",
            "SetAlarm(",
            "StartInterval(",
            "StartPomodoro(",
            "Cancel(",
            "List(",
        ):
            self.assertIn(f"maomi_reminders_->{operation}", board_source)

        self.assertIn("maomi_reminders_->Update(", board_source)
        self.assertIn("Event::ReminderDue()", board_source)
        self.assertIn("kMaomiReminderSoundName", board_source)
        self.assertIn(
            "ReleaseExpression(maomi::PetPriority::kReminder)", board_source
        )


if __name__ == "__main__":
    unittest.main()
