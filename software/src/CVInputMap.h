#pragma once
#include "CVInputMapValue.h"

struct DigitalInputMap {
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
private:
  // Reuse the CV binding inventory so direction changes and preset usage
  // account for digital consumers too. Only this member owns ii selection.
  CVInputMap ii_input;
  uint8_t Source() const {
    if (ii_input.source_type() == CVInputMap::TYPE_II)
      return TYPE_II | ii_input.index();
    return source == TYPE_II ? 0 : source;
  }
  void Select(uint8_t next) {
    if ((next & 0xe0) == TYPE_II) {
      if (!ii_input.SetSource(next)) return;
      source = TYPE_II;
    } else {
      ii_input.SetSource(0);
      source = next;
    }
    last_gate_state = false;
  }
public:
  CVInputMap* IiInput() { return &ii_input; }
#else
private:
  uint8_t Source() const { return source; }
  void Select(uint8_t next) { source = next; last_gate_state = false; }
public:
#endif
  // upper 3 bits
  enum DigitalSourceType : uint8_t {
    TYPE_NONE = 0,
    DEPRECATED = (1 << 5),

    TYPE_DIGITAL_INPUT = (2 << 5),
    TYPE_ADC = (3 << 5),
    TYPE_DAC = (4 << 5),
    TYPE_MIDI_MAP = (5 << 5),

    RESERVED = (6 << 5),
    TYPE_II = RESERVED,
    TYPE_INTERNAL = (7 << 5), // -1 and -2 fall here
  };

  static constexpr DigitalSourceType ordered_types[] = {
    TYPE_NONE,
    TYPE_DIGITAL_INPUT,
    TYPE_ADC,
    TYPE_DAC,
    TYPE_MIDI_MAP,
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
    TYPE_II,
#endif
    TYPE_INTERNAL,
  };

  static const int ppqn = 4;
  static constexpr float internal_clocked_gate_pw = 0.5f;

  static constexpr size_t Size = 32; // Make this compatible with Packable

  // settings
private:
  uint8_t source = 0;
public:
  ClkDivMult div_mult;
  int8_t e_length, e_beats, e_rotate;

  // state
  bool last_gate_state = false; // for detecting clocks
  uint32_t clkcount = 0;

  bool ChangeSourceType(int dir) {
    const int count = sizeof(ordered_types) / sizeof(ordered_types[0]);
    int tidx = 0;
    while (tidx < count && source_type() != ordered_types[tidx]) ++tidx;
    if (tidx >= count) tidx = 0;
    tidx += (dir>0?1:-1);
    CONSTRAIN(tidx, 0, count - 1);
    int sidx = constrain(index(), 0, channel_count(ordered_types[tidx]) - 1);

    DigitalSourceType oldtype = source_type();
    Select(ordered_types[tidx] | sidx);
    return oldtype != source_type();
  }
  void ChangeSource(int dir) {
    if (!dir) return;
    constexpr int count = sizeof(ordered_types) / sizeof(ordered_types[0]);
    int type = 0;
    while (type < count && ordered_types[type] != source_type()) ++type;
    if (type == count) type = 0;
    int idx = index();
    for (int attempt = 0; attempt < 256; ++attempt) {
      idx += dir > 0 ? 1 : -1;
      if (idx < 0) {
        if (!type) return;
        idx = channel_count(ordered_types[--type]) - 1;
      } else if (idx >= channel_count(ordered_types[type])) {
        if (type + 1 == count) return;
        ++type; idx = 0;
      }
      const uint8_t next = ordered_types[type] | idx;
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
      if (!phz::ii::SelectableIiInput(phz::ii::virtual_inputs, next)) continue;
#endif
      Select(next);
      return;
    }
  }

  void SetGateInput(uint8_t idx) {
    CONSTRAIN(idx, 0, channel_count(TYPE_DIGITAL_INPUT) - 1);
    Select(TYPE_DIGITAL_INPUT | (idx & 0x1f));
  }
  void SetInput(uint8_t idx) {
    CONSTRAIN(idx, 0, channel_count(TYPE_ADC) - 1);
    Select(TYPE_ADC | (idx & 0x1f));
  }
  void SetOutput(uint8_t idx) {
    CONSTRAIN(idx, 0, channel_count(TYPE_DAC) - 1);
    Select(TYPE_DAC | (idx & 0x1f));
  }
  void SetMidiMap(uint8_t midx) {
    CONSTRAIN(midx, 0, channel_count(TYPE_MIDI_MAP) - 1);
    Select(TYPE_MIDI_MAP | (midx & 0x1f));
  }
  void SetClockSource(uint8_t idx) {
    CONSTRAIN(idx, 0, channel_count(TYPE_INTERNAL) - 1);
    Select(TYPE_INTERNAL | (idx & 0x1f));
  }

  void Reset(bool hard = false) {
    if (hard) div_mult.steps = 1;
    div_mult.Reset();
    last_gate_state = false;
    clkcount = 0;
  }
  void Clear() {
    Select(0);
    Reset(true);
  }

  bool Gate() const {
    switch (source_type()) {
      case TYPE_INTERNAL:
        // Formerly hardcoded clock sources.
        // Now, the ClockSetup applet provides virtual outputs I/J/K/L
        return frame.ViewOut(DAC_CHANNEL_COUNT + index()) > GATE_THRESHOLD;

      case TYPE_DIGITAL_INPUT:
        return frame.gate_high[index()];
      case TYPE_ADC:
        return frame.gate_high[DIGITAL_INPUT_COUNT + index()];
        // gate_high is already calculated for ADC
        //return frame.inputs[index()] > GATE_THRESHOLD;
      case TYPE_DAC:
        return frame.ViewOut(index()) > GATE_THRESHOLD;
      case TYPE_MIDI_MAP:
        return frame.MIDIState.mapping[index()].ViewOut() > GATE_THRESHOLD;
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
      case TYPE_II:
        return ii_input.RawIn() > GATE_THRESHOLD;
#endif
      case TYPE_NONE:
      default:
        return false;
    }
  }

  /**
   * Returns true on rising gate input. Will return true once and then go back
   * to false until the gate goes low again.
   **/
  bool Clock() {
    if (clock_m.auto_reset) Reset();
    bool gate = Gate();
    bool tock = !last_gate_state && gate;
    last_gate_state = gate;

    tock = div_mult.Tick(tock);

    // process trigger filters here - Euclidean, etc
    if (tock) {
      if (e_length > 0)
        tock = EuclideanFilter(e_length + 1, e_beats, e_rotate, clkcount);
      ++clkcount;
    }

    return tock;
  }

  uint8_t const* Icon() const {
    switch (source_type()) {
      case TYPE_INTERNAL:
        return (clkcount & 1) ? METRO_L_ICON : METRO_R_ICON;
      case TYPE_MIDI_MAP:
        return PhzIcons::midiIn;
      case TYPE_II: {
        static const uint8_t icon[8] = {0, 0x7a, 0, 0, 0x7a, 0, 0, 0};
        return icon;
      }

      case TYPE_DIGITAL_INPUT:
        if (index() < 4)
          return DIGITAL_INPUT_ICONS + index() * 8;
      case TYPE_ADC:
        if (index() < 8)
          return PARAM_MAP_ICONS + (1 + index()) * 8;
      case TYPE_DAC:
        if (index() < 8)
          return PARAM_MAP_ICONS + (1 + ADC_CHANNEL_COUNT + index()) * 8;

        return ZAP_ICON;

      case TYPE_NONE:
      default:
        return PARAM_MAP_ICONS + 0;
    }
  }
  char const* InputName() const {
    static char in_label[] = "T 1";
    const char type[] = {' ', '-', 'T', 'C', '#', 'M', 'I', '*'};
    const char* clktype[] = {"CL1", "CL4", "RUN", "RST"};

    if (!Source()) return " - ";

    in_label[0] = type[source_type() >> 5];
    uint8_t idx = constrain(index(), 0, channel_count(source_type()));
    switch (source_type()) {
      case TYPE_INTERNAL:
        return clktype[idx];

      case TYPE_DAC:
        in_label[1] = OC::Strings::capital_letters[idx][0];
        in_label[2] = ' ';
        break;

      case TYPE_DIGITAL_INPUT:
      case TYPE_ADC:
      case TYPE_MIDI_MAP:
      case TYPE_II:
        ++idx;
        if (idx / 10)
          in_label[1] = char('0' + idx / 10);
        else
          in_label[1] = ' ';
        in_label[2] = char('0' + idx % 10);
        break;

      default:
        in_label[1] = '*';
        in_label[2] = ' ';
        break;
    }

    return in_label;
  }

  uint32_t Pack() const {
    return Source() | as_unsigned(div_mult.steps << 8) | (e_length & 0x1f) << 16 | (e_beats & 0x3f) << 21 | (e_rotate & 0x1f) << 27;
  }

  void Unpack(uint32_t data) {
    Clear();
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
    if ((data & 0xe0) == TYPE_II) {
      // Preset direction validation happens against the staged bank, not the
      // old live bank. Retain disabled bindings just like CV input restore.
      ii_input.Unpack(uint16_t(60 << 8) | uint8_t(data));
      source = TYPE_II;
    } else
#endif
    Select(data & 0xFF);

    // migrate old data; zero is still disabled; -1 and -2 are still valid
    if (source && (source >> 5) < 2) {
      if (source < 1 + DIGITAL_INPUT_COUNT)
        SetGateInput(source - 1);
      else if (source < 1 + DIGITAL_INPUT_COUNT + ADC_CHANNEL_COUNT)
        SetInput(source - DIGITAL_INPUT_COUNT - 1);
      else if (source < 1 + DIGITAL_INPUT_COUNT + ADC_CHANNEL_COUNT + DAC_CHANNEL_COUNT)
        SetOutput(source - DIGITAL_INPUT_COUNT - ADC_CHANNEL_COUNT - 1);
      else
        SetMidiMap(source - DIGITAL_INPUT_COUNT - ADC_CHANNEL_COUNT - DAC_CHANNEL_COUNT - 1);
    }

    // migrate old clock sources -1 and -2
    if (source_type() == TYPE_INTERNAL) {
      if (0xff == source) // "CL4"
        SetClockSource(1);
      if (0xfe == source) // "CL1"
        SetClockSource(0);
    }

    div_mult.Set(extract_value<int8_t>(data >> 8));
    if (0 == div_mult.steps) div_mult.steps = 1;

    e_length = (data >> 16) & 0x1f; // 5 bits
    e_beats  = (data >> 21) & 0x3f; // 6 bits
    e_rotate = (data >> 27) & 0x1f; // 5 bits
  }

  const bool enabled() const { return Source() != 0; }
  DigitalSourceType source_type() const {
    return (DigitalSourceType)(Source() & (0x7 << 5)); // upper 3 bits
  }
  inline uint8_t index() const {
    return Source() & 0x1f;
  }
  const bool is_clock() const {
    return (source_type() == TYPE_INTERNAL) && (index() <= 1);
  }
  constexpr int channel_count(DigitalSourceType t) const {
    switch (t) {
      default: return 0;

      case TYPE_NONE:
        return 1;

      case TYPE_INTERNAL:
        return 4; // 1x, 4x, RUN, RESET

      case TYPE_DIGITAL_INPUT:
        return DIGITAL_INPUT_COUNT;
      case TYPE_ADC:
        // strictly hardware inputs
        return ADC_CHANNEL_COUNT;
      case TYPE_DAC:
        // This one can have virtual signals.
        // 2 outputs per applet slot, including Clock and Audio Applets...
        return 2 * APPLET_CURSOR_COUNT;
      case TYPE_MIDI_MAP:
        return MIDIMAP_MAX;
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
      case TYPE_II:
        return 32;
#endif
    }
  }
};

// Let's PackingUtils know this is Packable as is.
constexpr DigitalInputMap& pack(DigitalInputMap& input) {
  return input;
}

extern DigitalInputMap trigmap[ADC_CHANNEL_COUNT];
extern CVInputMap cvmap[ADC_CHANNEL_COUNT];
