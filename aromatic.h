#pragma once
// Aromatic diffuser state and frame helpers, shared by the ESPHome lambdas.
// Protocol details: PROTOCOL.md
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace aromatic {

// Schedule slot frame. Reply 0x4A and write 0x2A share this layout:
//  0 code, 1 aroma, 2 0x02, 3 fan level << 1 | fog, 4 0x00, 5 slot index (1-5),
//  6 flags (bit2 fan, bit1 enabled, bit0 shown), 7 on hour, 8 on minute,
//  9 off hour, 10 off minute, 11 days (bit0 Sunday .. bit6 Saturday),
//  12 custom intensity, 13 level, 14-15 custom spray s, 16-17 custom pause s.
constexpr size_t kSlots = 5;
constexpr size_t kSlotLen = 18;
constexpr const char *kDayNames[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

struct Slot {
  std::array<uint8_t, kSlotLen> raw{};
  bool valid = false;

  bool enabled() const { return raw[6] & 0x02; }
  bool custom() const { return raw[12] == 1; }
  uint8_t level() const { return raw[13] ? raw[13] : 1; }
  uint16_t custom_spray() const { return raw[14] << 8 | raw[15]; }
  uint16_t custom_pause() const { return raw[16] << 8 | raw[17]; }
  int start_minute() const { return raw[7] * 60 + raw[8]; }
  int end_minute() const { return raw[9] * 60 + raw[10]; }
};

struct Grade {
  uint16_t spray;
  uint16_t pause;
};

class State {
 public:
  std::array<Slot, kSlots> slots;
  uint8_t control = 0;  // reply 0x4D: bit0 on/off, bit1 fan, bit2 demo, bit4 lamp
  bool control_valid = false;
  uint8_t fan_fog = 0x01;  // byte 3 of the slot frames
  uint8_t max_grade = 4;
  uint8_t fan_max = 4;
  uint16_t spray_min = 15, spray_max = 300, pause_min = 15, pause_max = 600;
  std::vector<Grade> grades;
  std::vector<std::vector<uint8_t>> queue;  // frames to write on the next connection

  // Updates state from a notification on FFF6. Values with a queued write are
  // kept, so the login state dump doesn't undo edits that haven't been sent.
  void parse(const std::vector<uint8_t> &x) {
    if (x.empty())
      return;
    switch (x[0]) {
      case 0x4A:
        if (x.size() >= 14 && x[5] >= 1 && x[5] <= kSlots) {
          if (!this->queued(0x2A, 5, 0))
            this->fan_fog = x[3];
          Slot &s = this->slots[x[5] - 1];
          if (s.valid && this->queued(0x2A, kSlotLen, x[5]))
            break;
          s.raw.fill(0);
          for (size_t i = 0; i < x.size() && i < kSlotLen; i++)
            s.raw[i] = x[i];
          s.raw[3] = this->fan_fog;
          s.valid = true;
        }
        break;
      case 0x46:
        if (x.size() >= 10) {
          this->max_grade = x[1];
          this->spray_min = x[2] << 8 | x[3];
          this->spray_max = x[4] << 8 | x[5];
          this->pause_min = x[6] << 8 | x[7];
          this->pause_max = x[8] << 8 | x[9];
        }
        if (x.size() > 12)
          this->fan_max = x[12];
        break;
      case 0x47:
        this->grades.clear();
        for (size_t i = 1; i + 3 < x.size(); i += 4)
          this->grades.push_back({uint16_t(x[i] << 8 | x[i + 1]), uint16_t(x[i + 2] << 8 | x[i + 3])});
        break;
      case 0x4D:
        if (x.size() >= 2 && !this->queued(0x2D, 2, 0)) {
          this->control = x[1];
          this->control_valid = true;
        }
        break;
    }
  }

  bool power() const { return this->control & 0x01; }
  uint8_t fan_level() const { return (this->fan_fog >> 1) & 0x07; }

  // Spray and pause seconds a slot runs at.
  Grade timing(const Slot &s) const {
    if (s.custom())
      return {s.custom_spray(), s.custom_pause()};
    size_t i = s.level() - 1;
    if (i < this->grades.size())
      return this->grades[i];
    return {0, 0};
  }

  // Index of the slot running at this day (0 = Sunday) and minute of day, or -1.
  int active_slot(int dow, int minute) const {
    for (size_t i = 0; i < kSlots; i++) {
      const Slot &s = this->slots[i];
      if (!s.valid || !s.enabled() || !(s.raw[11] & (1 << dow)))
        continue;
      int on = s.start_minute(), off = s.end_minute();
      bool inside = on <= off ? (minute >= on && minute < off) : (minute >= on || minute < off);
      if (inside)
        return i;
    }
    return -1;
  }

  // Index of the next slot starting later today, or -1.
  int next_slot(int dow, int minute) const {
    int best = -1;
    for (size_t i = 0; i < kSlots; i++) {
      const Slot &s = this->slots[i];
      if (!s.valid || !s.enabled() || !(s.raw[11] & (1 << dow)) || s.start_minute() <= minute)
        continue;
      if (best < 0 || s.start_minute() < this->slots[best].start_minute())
        best = i;
    }
    return best;
  }

  void set_enabled(size_t i, bool on) {
    Slot &s = this->slots[i];
    s.raw[6] = on ? (s.raw[6] | 0x03) : (s.raw[6] & ~0x02);
    this->queue_slot(i);
  }
  void set_start(size_t i, uint8_t h, uint8_t m) {
    this->slots[i].raw[7] = h;
    this->slots[i].raw[8] = m;
    this->queue_slot(i);
  }
  void set_end(size_t i, uint8_t h, uint8_t m) {
    this->slots[i].raw[9] = h;
    this->slots[i].raw[10] = m;
    this->queue_slot(i);
  }
  void set_days(size_t i, uint8_t mask) {
    this->slots[i].raw[11] = mask & 0x7F;
    this->queue_slot(i);
  }
  // level 1..max_grade, or 0 for custom spray/pause times.
  void set_level(size_t i, uint8_t level) {
    Slot &s = this->slots[i];
    if (level == 0) {
      s.raw[12] = 1;
      if (s.custom_spray() == 0)
        put16(s, 14, this->spray_min);
      if (s.custom_pause() == 0)
        put16(s, 16, this->pause_min);
    } else {
      s.raw[12] = 0;
      s.raw[13] = level;
    }
    this->queue_slot(i);
  }
  void set_custom_spray(size_t i, uint16_t seconds) {
    put16(this->slots[i], 14, seconds);
    this->queue_slot(i);
  }
  void set_custom_pause(size_t i, uint16_t seconds) {
    put16(this->slots[i], 16, seconds);
    this->queue_slot(i);
  }

  void set_fan_level(uint8_t level) {
    this->fan_fog = (level & 0x07) << 1 | (this->fan_fog & 0x01);
    for (auto &s : this->slots)
      s.raw[3] = this->fan_fog;
    this->replace_queued(0x2A, 5, 0, {0x2A, 0x01, 0x02, this->fan_fog, 0x00});
  }

  // Master on/off; keeps the lamp, demo and fan bits as last reported.
  void set_power(bool on) { this->set_control_bit(0x01, on); }

 private:
  // Total control (2D) writes all bits at once: lamp(4) 1 demo(2) fan(1) on/off(0).
  void set_control_bit(uint8_t bit, bool on) {
    uint8_t c = on ? (this->control | bit) : (this->control & ~bit);
    this->control = c & 0x17;
    this->replace_queued(0x2D, 2, 0, {0x2D, uint8_t(0x08 | this->control)});
  }

  static void put16(Slot &s, size_t at, uint16_t v) {
    s.raw[at] = v >> 8;
    s.raw[at + 1] = v & 0xFF;
  }

  void queue_slot(size_t i) {
    Slot &s = this->slots[i];
    if (!s.valid)
      return;
    std::vector<uint8_t> f(s.raw.begin(), s.raw.end());
    f[0] = 0x2A;
    f[3] = this->fan_fog;
    this->replace_queued(0x2A, kSlotLen, s.raw[5], f);
  }

  bool queued(uint8_t code, size_t len, uint8_t index) const {
    for (const auto &q : this->queue) {
      if (q[0] == code && q.size() == len && (len != kSlotLen || q[5] == index))
        return true;
    }
    return false;
  }

  // Queues a frame, replacing an older queued frame of the same kind.
  void replace_queued(uint8_t code, size_t len, uint8_t index, std::vector<uint8_t> frame) {
    for (auto &q : this->queue) {
      if (q[0] == code && q.size() == len && (len != kSlotLen || q[5] == index)) {
        q = std::move(frame);
        return;
      }
    }
    this->queue.push_back(std::move(frame));
  }
};

inline State &state() {
  static State s;
  return s;
}

inline std::string days_to_text(uint8_t mask) {
  if ((mask & 0x7F) == 0x7F)
    return "Every day";
  std::string out;
  for (int d = 0; d < 7; d++) {
    if (mask & (1 << d)) {
      if (!out.empty())
        out += ",";
      out += kDayNames[d];
    }
  }
  return out.empty() ? "Never" : out;
}

// Accepts "Every day", "Never", or day names like "Sun,Mon,Thu" (any case,
// separated by commas or spaces). Returns nothing for unknown words.
inline std::optional<uint8_t> text_to_days(const std::string &text) {
  std::string t;
  for (char c : text)
    t += (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
  if (t == "every day" || t == "daily" || t == "all")
    return 0x7F;
  if (t == "never" || t == "none")
    return 0;
  uint8_t mask = 0;
  size_t i = 0;
  while (i < t.size()) {
    if (t[i] == ',' || t[i] == ' ') {
      i++;
      continue;
    }
    size_t j = i;
    while (j < t.size() && t[j] != ',' && t[j] != ' ')
      j++;
    std::string word = t.substr(i, j - i);
    bool found = false;
    for (int d = 0; d < 7; d++) {
      std::string name = kDayNames[d];
      name[0] = char(name[0] + 32);
      if (word.size() >= 3 && word.compare(0, 3, name) == 0) {
        mask |= 1 << d;
        found = true;
      }
    }
    if (!found)
      return std::nullopt;
    i = j;
  }
  return mask;
}

inline std::string level_name(const Slot &s) {
  if (s.custom())
    return "Custom";
  char buf[12];
  snprintf(buf, sizeof(buf), "Level %u", s.level());
  return buf;
}

}  // namespace aromatic
