#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "types.h"

std::optional<std::chrono::time_point<Clock>> parse_alarm_time(const std::string& s);
std::string format_local_time(const std::chrono::time_point<Clock>& t);
std::vector<std::chrono::time_point<Clock>> parse_alarm_times(const std::vector<std::string>& times);

// Owns the ring / snooze / hold-to-stop state machine. main() forwards input
// and reacts to the returned outcomes; audio and the window stay in main().
class AlarmScheduler {
 public:
  enum class Outcome { None, Snoozed, Dismissed };

  AlarmScheduler(std::vector<std::chrono::time_point<Clock>> times, int snooze_minutes,
                 int hold_to_stop_seconds);

  // True the moment a scheduled alarm or an elapsed snooze becomes due; the
  // caller should start audio. Consumes the due alarm/snooze and starts ringing.
  bool poll();
  void start_ringing();  // manual / --test-source trigger

  bool ringing() const { return ringing_; }
  double hold_progress() const { return hold_progress_; }

  void press_hold();       // SPACE down
  Outcome release_hold();  // SPACE up: Snoozed on a short press while ringing
  Outcome update_hold();   // per frame while held: Dismissed when the hold completes

  bool has_pending() const;  // any scheduled alarms or a live snooze remain
  std::optional<std::chrono::time_point<Clock>> pending_snooze() const { return snooze_time_; }

 private:
  std::vector<std::chrono::time_point<Clock>> alarm_times_;
  std::optional<std::chrono::time_point<Clock>> snooze_time_;
  std::chrono::minutes snooze_duration_;
  std::chrono::seconds hold_to_stop_;
  bool ringing_ = false;
  bool holding_ = false;
  std::chrono::time_point<Clock> hold_started_{};
  double hold_progress_ = 0.0;
};
