#pragma once

#include <cstdint>

namespace maomi {

struct DateTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

struct TimeSample {
    bool server_time_set = false;
    DateTime local_time;
    uint64_t monotonic_ms = 0;
};

struct ClockSnapshot {
    bool valid = false;
    DateTime local_time;
    uint64_t monotonic_ms = 0;
    int32_t date_key = 0;
    uint32_t wall_clock_rollback_count = 0;
};

struct MonotonicDeadline {
    uint64_t at_ms = 0;
};

// Main-task-owned clock policy. Callers provide the server-time signal and local civil time;
// relative deadlines remain independent of wall-clock corrections.
class ReliableClock {
public:
    static constexpr int kMinimumTrustedYear = 2024;

    void Observe(const TimeSample& sample);
    ClockSnapshot GetSnapshot() const;

    bool HasReached(const DateTime& target) const;
    MonotonicDeadline DeadlineAfter(uint64_t delay_ms) const;
    bool HasReached(MonotonicDeadline deadline) const;
    uint32_t CompanionDays(int32_t first_day, uint32_t historical_max) const;

    static bool IsValidDateTime(const DateTime& value, int minimum_year = 1);
    static bool IsWithinDailyWindow(const DateTime& value, int start_hour, int start_minute,
                                    int end_hour, int end_minute);
    static int32_t EncodeDate(const DateTime& value);
    static bool DecodeDate(int32_t date_key, DateTime* value);

private:
    ClockSnapshot snapshot_;
    bool has_last_valid_wall_seconds_ = false;
    int64_t last_valid_wall_seconds_ = 0;

    static int DaysInMonth(int year, int month);
    static int64_t DaysFromCivil(int year, unsigned month, unsigned day);
    static int64_t CivilSeconds(const DateTime& value);
};

}  // namespace maomi
