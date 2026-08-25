#include "maomi_clock.h"

#include <algorithm>
#include <limits>

namespace maomi {

void ReliableClock::Observe(const TimeSample& sample) {
    snapshot_.monotonic_ms = sample.monotonic_ms;
    snapshot_.valid =
        sample.server_time_set && IsValidDateTime(sample.local_time, kMinimumTrustedYear);
    if (!snapshot_.valid) {
        snapshot_.local_time = {};
        snapshot_.date_key = 0;
        return;
    }

    const int64_t wall_seconds = CivilSeconds(sample.local_time);
    if (has_last_valid_wall_seconds_ && wall_seconds < last_valid_wall_seconds_ &&
        snapshot_.wall_clock_rollback_count < std::numeric_limits<uint32_t>::max()) {
        ++snapshot_.wall_clock_rollback_count;
    }
    has_last_valid_wall_seconds_ = true;
    last_valid_wall_seconds_ = wall_seconds;
    snapshot_.local_time = sample.local_time;
    snapshot_.date_key = EncodeDate(sample.local_time);
}

ClockSnapshot ReliableClock::GetSnapshot() const { return snapshot_; }

bool ReliableClock::HasReached(const DateTime& target) const {
    return snapshot_.valid && IsValidDateTime(target, kMinimumTrustedYear) &&
           CivilSeconds(snapshot_.local_time) >= CivilSeconds(target);
}

MonotonicDeadline ReliableClock::DeadlineAfter(uint64_t delay_ms) const {
    if (delay_ms > std::numeric_limits<uint64_t>::max() - snapshot_.monotonic_ms) {
        return {std::numeric_limits<uint64_t>::max()};
    }
    return {snapshot_.monotonic_ms + delay_ms};
}

bool ReliableClock::HasReached(MonotonicDeadline deadline) const {
    return snapshot_.monotonic_ms >= deadline.at_ms;
}

uint32_t ReliableClock::CompanionDays(int32_t first_day, uint32_t historical_max) const {
    DateTime first;
    if (!snapshot_.valid || !DecodeDate(first_day, &first) || first.year < kMinimumTrustedYear) {
        return historical_max;
    }

    const int64_t elapsed_days =
        DaysFromCivil(snapshot_.local_time.year, snapshot_.local_time.month,
                      snapshot_.local_time.day) -
        DaysFromCivil(first.year, first.month, first.day);
    if (elapsed_days < 0) {
        return historical_max;
    }

    const uint64_t candidate = static_cast<uint64_t>(elapsed_days) + 1;
    const uint32_t bounded_candidate = candidate > std::numeric_limits<uint32_t>::max()
                                           ? std::numeric_limits<uint32_t>::max()
                                           : static_cast<uint32_t>(candidate);
    return std::max(historical_max, bounded_candidate);
}

bool ReliableClock::IsValidDateTime(const DateTime& value, int minimum_year) {
    if (value.year < minimum_year || value.year > 9999 || value.month < 1 || value.month > 12 ||
        value.day < 1 || value.day > DaysInMonth(value.year, value.month)) {
        return false;
    }
    return value.hour >= 0 && value.hour <= 23 && value.minute >= 0 && value.minute <= 59 &&
           value.second >= 0 && value.second <= 59;
}

bool ReliableClock::IsWithinDailyWindow(const DateTime& value, int start_hour, int start_minute,
                                        int end_hour, int end_minute) {
    if (!IsValidDateTime(value) || start_hour < 0 || start_hour > 23 || start_minute < 0 ||
        start_minute > 59 || end_hour < 0 || end_hour > 23 || end_minute < 0 || end_minute > 59) {
        return false;
    }

    const int start = start_hour * 60 + start_minute;
    const int end = end_hour * 60 + end_minute;
    const int current = value.hour * 60 + value.minute;
    if (start == end) {
        return false;
    }
    if (start < end) {
        return current >= start && current < end;
    }
    return current >= start || current < end;
}

int32_t ReliableClock::EncodeDate(const DateTime& value) {
    if (!IsValidDateTime(value)) {
        return 0;
    }
    return value.year * 10000 + value.month * 100 + value.day;
}

bool ReliableClock::DecodeDate(int32_t date_key, DateTime* value) {
    if (value == nullptr || date_key <= 0) {
        return false;
    }
    DateTime decoded;
    decoded.year = date_key / 10000;
    decoded.month = (date_key / 100) % 100;
    decoded.day = date_key % 100;
    if (!IsValidDateTime(decoded)) {
        return false;
    }
    *value = decoded;
    return true;
}

int ReliableClock::DaysInMonth(int year, int month) {
    static constexpr int kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month != 2) {
        return kDaysPerMonth[month - 1];
    }
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return leap ? 29 : 28;
}

int64_t ReliableClock::DaysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(day_of_era) - 719468;
}

int64_t ReliableClock::CivilSeconds(const DateTime& value) {
    return DaysFromCivil(value.year, value.month, value.day) * 86400 + value.hour * 3600 +
           value.minute * 60 + value.second;
}

}  // namespace maomi
