#include "maomi_pet_core.h"

#include <utility>

namespace maomi {
namespace {

constexpr uint32_t kQueuePressureLogInterval = 64;

Event MakeEvent(EventType type) {
    Event event;
    event.type = type;
    return event;
}

uint32_t SaturatingAdd(uint32_t left, uint32_t right) {
    if (right > UINT32_MAX - left) {
        return UINT32_MAX;
    }
    return left + right;
}

}  // namespace

Event Event::UserWake() { return MakeEvent(EventType::kUserWake); }

Event Event::ConversationFinished() { return MakeEvent(EventType::kConversationFinished); }

Event Event::Interaction(PetState state) {
    auto event = MakeEvent(EventType::kInteraction);
    event.state = state;
    return event;
}

Event Event::ChargingChanged(bool charging) {
    auto event = MakeEvent(EventType::kChargingChanged);
    event.flag = charging;
    return event;
}

Event Event::BatteryChanged(int level) {
    auto event = MakeEvent(EventType::kBatteryChanged);
    event.value = level;
    return event;
}

Event Event::TimeValidityChanged(bool valid) {
    auto event = MakeEvent(EventType::kTimeValidityChanged);
    event.flag = valid;
    return event;
}

Event Event::Tick(uint32_t tick) {
    auto event = MakeEvent(EventType::kTick);
    event.value = static_cast<int32_t>(tick);
    return event;
}

Event Event::ReminderDue() { return MakeEvent(EventType::kReminderDue); }

Event Event::OfficialStateChanged(DeviceState state) {
    auto event = MakeEvent(EventType::kOfficialStateChanged);
    event.value = static_cast<int32_t>(state);
    event.flag = state != kDeviceStateIdle;
    return event;
}

Event Event::RequestExpression(PetState state, PetPriority priority, bool resume_after_official) {
    auto event = MakeEvent(EventType::kRequestExpression);
    event.state = state;
    event.priority = priority;
    event.flag = resume_after_official;
    return event;
}

Event Event::ReleaseExpression(PetPriority priority) {
    auto event = MakeEvent(EventType::kReleaseExpression);
    event.priority = priority;
    return event;
}

PetCore::PetCore(MainTaskScheduler scheduler, Logger logger, DeviceState initial_official_state)
    : scheduler_(std::move(scheduler)), logger_(std::move(logger)) {
    snapshot_.official_state = initial_official_state;
    snapshot_.paused_by_official_state = initial_official_state != kDeviceStateIdle;
}

SubmitResult PetCore::Submit(const Event& event) {
    bool should_schedule = false;
    SubmitResult result = SubmitResult::kQueued;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++snapshot_.submitted;
        if (!scheduler_ || !IsValidEvent(event)) {
            ++snapshot_.rejected;
            return SubmitResult::kRejected;
        }

        Event queued_event = event;
        if (queued_event.occurrences == 0) {
            queued_event.occurrences = 1;
        }
        if (queued_event.type == EventType::kOfficialStateChanged) {
            queued_event.flag = queued_event.value != kDeviceStateIdle;
        }

        if (IsCoalescible(queued_event.type)) {
            for (size_t i = 0; i < queue_size_; ++i) {
                if (queue_[i].type != queued_event.type) {
                    continue;
                }
                if (queued_event.type == EventType::kOfficialStateChanged && i + 1 != queue_size_) {
                    continue;
                }
                if (queued_event.type == EventType::kOfficialStateChanged) {
                    queued_event.flag = queue_[i].flag || queued_event.flag;
                }
                if (IsImportant(queued_event.type) &&
                    queued_event.type != EventType::kOfficialStateChanged) {
                    queued_event.occurrences =
                        SaturatingAdd(queue_[i].occurrences, queued_event.occurrences);
                }
                RemoveQueuedEvent(i);
                queue_[queue_size_++] = queued_event;
                snapshot_.queue_depth = queue_size_;
                ++snapshot_.coalesced;
                return SubmitResult::kCoalesced;
            }
        }

        if (queue_size_ == kEventQueueCapacity) {
            if (IsImportant(queued_event.type)) {
                size_t victim = queue_size_;
                for (size_t i = 0; i < queue_size_; ++i) {
                    if (!IsImportant(queue_[i].type)) {
                        victim = i;
                        break;
                    }
                }
                if (victim == queue_size_ &&
                    queued_event.type == EventType::kOfficialStateChanged) {
                    for (size_t i = 0; i < queue_size_; ++i) {
                        if (queue_[i].type == EventType::kOfficialStateChanged) {
                            victim = i;
                            // Preserve that a busy state occurred even when its oldest detail is
                            // replaced by the latest official state.
                            queued_event.flag = queued_event.flag || queue_[i].flag;
                            break;
                        }
                    }
                }
                if (victim < queue_size_) {
                    RemoveQueuedEvent(victim);
                    ++snapshot_.evicted;
                } else {
                    ++snapshot_.rejected;
                    result = SubmitResult::kRejected;
                }
            } else {
                ++snapshot_.rejected;
                result = SubmitResult::kRejected;
            }

            const uint32_t pressure_count = snapshot_.rejected + snapshot_.evicted;
            if (!has_logged_queue_pressure_ ||
                pressure_count - last_logged_pressure_count_ >= kQueuePressureLogInterval) {
                queue_warning_pending_ = true;
            }
            if (result == SubmitResult::kRejected) {
                return result;
            }
        }

        queue_[queue_size_++] = queued_event;
        snapshot_.queue_depth = queue_size_;
        if (!drain_scheduled_) {
            drain_scheduled_ = true;
            should_schedule = true;
        }
    }

    if (should_schedule) {
        scheduler_([this]() { DrainOnMainTask(); });
    }
    return result;
}

SubmitResult PetCore::RequestExpression(PetState state, PetPriority priority,
                                        bool resume_after_official) {
    return Submit(Event::RequestExpression(state, priority, resume_after_official));
}

SubmitResult PetCore::ReleaseExpression(PetPriority priority) {
    return Submit(Event::ReleaseExpression(priority));
}

int PetCore::AddObserver(Observer observer) {
    if (!observer) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& slot : observers_) {
        if (slot.id < 0) {
            slot.id = next_observer_id_++;
            slot.callback = std::move(observer);
            return slot.id;
        }
    }
    return -1;
}

bool PetCore::RemoveObserver(int observer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& slot : observers_) {
        if (slot.id == observer_id) {
            slot.id = -1;
            slot.callback = {};
            return true;
        }
    }
    return false;
}

Snapshot PetCore::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool PetCore::IsValidState(PetState state) {
    return state >= PetState::kIdle && state <= PetState::kReminding;
}

bool PetCore::IsExpressionPriority(PetPriority priority) {
    return priority >= PetPriority::kReminder && priority <= PetPriority::kAutonomous;
}

bool PetCore::IsValidExpression(PetState state, PetPriority priority) {
    switch (priority) {
        case PetPriority::kReminder:
            return state == PetState::kReminding;
        case PetPriority::kPower:
            return state == PetState::kCharging || state == PetState::kFull ||
                   state == PetState::kLowBattery;
        case PetPriority::kInteraction:
            return state == PetState::kCurious || state == PetState::kHappy ||
                   state == PetState::kEating || state == PetState::kPlaying;
        case PetPriority::kAutonomous:
            return state == PetState::kCurious || state == PetState::kSleepy ||
                   state == PetState::kSleeping;
        default:
            return false;
    }
}

bool PetCore::IsImportant(EventType type) {
    return type == EventType::kUserWake || type == EventType::kReminderDue ||
           type == EventType::kOfficialStateChanged;
}

bool PetCore::IsCoalescible(EventType type) {
    switch (type) {
        case EventType::kUserWake:
        case EventType::kChargingChanged:
        case EventType::kBatteryChanged:
        case EventType::kTimeValidityChanged:
        case EventType::kTick:
        case EventType::kReminderDue:
        case EventType::kOfficialStateChanged:
            return true;
        default:
            return false;
    }
}

bool PetCore::IsValidEvent(const Event& event) {
    if (event.type < EventType::kUserWake || event.type > EventType::kReleaseExpression) {
        return false;
    }
    switch (event.type) {
        case EventType::kInteraction:
            return event.state == PetState::kHappy || event.state == PetState::kEating ||
                   event.state == PetState::kPlaying;
        case EventType::kBatteryChanged:
            return event.value >= 0 && event.value <= 100;
        case EventType::kOfficialStateChanged:
            return event.value >= kDeviceStateUnknown && event.value <= kDeviceStateFatalError;
        case EventType::kRequestExpression:
            return IsValidState(event.state) && IsExpressionPriority(event.priority) &&
                   IsValidExpression(event.state, event.priority);
        case EventType::kReleaseExpression:
            return IsExpressionPriority(event.priority);
        default:
            return true;
    }
}

size_t PetCore::PriorityIndex(PetPriority priority) {
    return static_cast<size_t>(priority) - static_cast<size_t>(PetPriority::kReminder);
}

void PetCore::RemoveQueuedEvent(size_t index) {
    for (size_t i = index + 1; i < queue_size_; ++i) {
        queue_[i - 1] = queue_[i];
    }
    --queue_size_;
    snapshot_.queue_depth = queue_size_;
}

void PetCore::DrainOnMainTask() {
    while (true) {
        Event event;
        bool has_event = false;
        Logger logger;
        Snapshot log_snapshot;
        bool should_log = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_size_ == 0) {
                drain_scheduled_ = false;
                if (queue_warning_pending_ && logger_) {
                    queue_warning_pending_ = false;
                    has_logged_queue_pressure_ = true;
                    last_logged_pressure_count_ = snapshot_.rejected + snapshot_.evicted;
                    logger = logger_;
                    log_snapshot = snapshot_;
                    should_log = true;
                }
            } else {
                event = queue_[0];
                RemoveQueuedEvent(0);
                ++snapshot_.processed;
                has_event = true;
            }
        }

        if (should_log) {
            logger(LogEvent::kQueuePressure, log_snapshot);
        }
        if (!has_event) {
            return;
        }
        ProcessOnMainTask(event);
    }
}

void PetCore::ProcessOnMainTask(const Event& event) {
    std::array<Observer, kObserverCapacity> callbacks;
    size_t callback_count = 0;
    Snapshot notification;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Snapshot before = snapshot_;
        switch (event.type) {
            case EventType::kUserWake:
                snapshot_.wake_signals = SaturatingAdd(snapshot_.wake_signals, event.occurrences);
                if (!snapshot_.paused_by_official_state) {
                    SetExpression(PetPriority::kInteraction, PetState::kCurious, false);
                }
                break;
            case EventType::kConversationFinished:
                if (!snapshot_.paused_by_official_state) {
                    SetExpression(PetPriority::kInteraction, PetState::kHappy, false);
                }
                break;
            case EventType::kInteraction:
                SetExpression(PetPriority::kInteraction, event.state, true);
                break;
            case EventType::kChargingChanged:
                snapshot_.charging = event.flag;
                if (snapshot_.battery_level >= 0 && snapshot_.battery_level <= 20) {
                    SetExpression(PetPriority::kPower, PetState::kLowBattery, true);
                } else if (snapshot_.charging) {
                    SetExpression(
                        PetPriority::kPower,
                        snapshot_.battery_level == 100 ? PetState::kFull : PetState::kCharging,
                        true);
                } else {
                    ClearExpression(PetPriority::kPower);
                }
                break;
            case EventType::kBatteryChanged:
                snapshot_.battery_level = event.value;
                if (snapshot_.battery_level <= 20) {
                    SetExpression(PetPriority::kPower, PetState::kLowBattery, true);
                } else if (snapshot_.charging) {
                    SetExpression(
                        PetPriority::kPower,
                        snapshot_.battery_level == 100 ? PetState::kFull : PetState::kCharging,
                        true);
                } else {
                    ClearExpression(PetPriority::kPower);
                }
                break;
            case EventType::kTimeValidityChanged:
                snapshot_.time_valid = event.flag;
                break;
            case EventType::kTick:
                snapshot_.last_tick = static_cast<uint32_t>(event.value);
                break;
            case EventType::kReminderDue:
                snapshot_.reminder_signals =
                    SaturatingAdd(snapshot_.reminder_signals, event.occurrences);
                SetExpression(PetPriority::kReminder, PetState::kReminding, true);
                break;
            case EventType::kOfficialStateChanged:
                snapshot_.official_state = static_cast<DeviceState>(event.value);
                snapshot_.paused_by_official_state = snapshot_.official_state != kDeviceStateIdle;
                if (event.flag) {
                    for (auto& expression : expressions_) {
                        if (expression.active && !expression.resume_after_official) {
                            expression.active = false;
                        }
                    }
                }
                break;
            case EventType::kRequestExpression:
                if (!snapshot_.paused_by_official_state || event.flag) {
                    SetExpression(event.priority, event.state, event.flag);
                }
                break;
            case EventType::kReleaseExpression:
                ClearExpression(event.priority);
                break;
        }
        RecomputeVisibleState();

        if (VisibleSnapshotChanged(before)) {
            notification = snapshot_;
            for (const auto& slot : observers_) {
                if (slot.id >= 0 && slot.callback) {
                    callbacks[callback_count++] = slot.callback;
                }
            }
        }
    }

    for (size_t i = 0; i < callback_count; ++i) {
        callbacks[i](notification);
    }
}

void PetCore::SetExpression(PetPriority priority, PetState state, bool resume_after_official) {
    auto& slot = expressions_[PriorityIndex(priority)];
    slot.active = true;
    slot.resume_after_official = resume_after_official;
    slot.state = state;
}

void PetCore::ClearExpression(PetPriority priority) {
    expressions_[PriorityIndex(priority)].active = false;
}

void PetCore::RecomputeVisibleState() {
    snapshot_.state = PetState::kIdle;
    snapshot_.priority = PetPriority::kIdle;
    if (snapshot_.paused_by_official_state) {
        return;
    }
    for (size_t i = 0; i < expressions_.size(); ++i) {
        if (expressions_[i].active) {
            snapshot_.state = expressions_[i].state;
            snapshot_.priority =
                static_cast<PetPriority>(static_cast<size_t>(PetPriority::kReminder) + i);
            return;
        }
    }
}

bool PetCore::VisibleSnapshotChanged(const Snapshot& before) const {
    return snapshot_.state != before.state || snapshot_.priority != before.priority ||
           snapshot_.official_state != before.official_state ||
           snapshot_.paused_by_official_state != before.paused_by_official_state ||
           snapshot_.charging != before.charging || snapshot_.time_valid != before.time_valid ||
           snapshot_.battery_level != before.battery_level;
}

}  // namespace maomi
