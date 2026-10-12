#pragma once
// Native CV input mapping. Included by CVInputMap.h after the frame context.

struct CVInputMap {
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
private:
  // Main-thread inventory, including cached applets and temporary preset maps.
  // ISR code never walks this list. Copying settings must not copy list links.
  inline static CVInputMap* binding_head = nullptr;
  inline static uint64_t binding_lifetime = 0;
  CVInputMap* binding_next = nullptr;
  void RegisterBinding() { binding_next = binding_head; binding_head = this; ++binding_lifetime; }
public:
  CVInputMap() { RegisterBinding(); }
  CVInputMap(const CVInputMap& other)
      : source(other.source), attenuversion(other.attenuversion),
        semitone_quant(other.semitone_quant) { RegisterBinding(); }
  CVInputMap& operator=(const CVInputMap& other) {
    if (this != &other) {
      source = other.source;
      attenuversion = other.attenuversion;
      semitone_quant = other.semitone_quant;
    }
    return *this;
  }
  ~CVInputMap() {
    ++binding_lifetime;
    auto** link = &binding_head;
    while (*link && *link != this) link = &(*link)->binding_next;
    if (*link) *link = binding_next;
  }
  // Callback may change settings, but must not create/destroy mapping objects.
  // Caller owns lifecycle protection and a short interrupt lock per mutation.
  template<class Visit>
  static size_t VisitIiBindings(size_t index, Visit visit) {
    if (index >= 32) return 0;
    size_t count = 0;
    for (auto* map = binding_head; map; map = map->binding_next) {
      if (map->source == (0xc0 | index)) { ++count; visit(*map); }
    }
    return count;
  }
  static size_t CountIiBindings(size_t index) {
    return VisitIiBindings(index, [](CVInputMap&) {});
  }
  static uint32_t IiUsageMask() {
    uint32_t mask = 0;
    for (auto* map = binding_head; map; map = map->binding_next)
      if ((map->source & 0xe0) == 0xc0) mask |= uint32_t(1) << (map->source & 31);
    return mask;
  }
  static uint64_t IiBindingIdentity(size_t index) {
    uint64_t fingerprint = binding_lifetime;
    VisitIiBindings(index, [&](CVInputMap& map) {
      fingerprint = (fingerprint ^ uint64_t(reinterpret_cast<uintptr_t>(&map))) * UINT64_C(1099511628211);
    });
    return fingerprint;
  }
#endif
  // upper 3 bits of source
  enum SourceType : uint8_t {
    TYPE_NONE = (0 << 5),
    TYPE_OLD1 = (1 << 5),
    TYPE_ADC = (2 << 5),
    TYPE_DAC = (3 << 5),
    TYPE_MIDI = (4 << 5),

    TYPE_HID = (5 << 5),
    TYPE_RESERVED0 = (6 << 5),
    TYPE_II = TYPE_RESERVED0, // additive source allocation; old meanings unchanged
    TYPE_INTERNAL = (7 << 5), // for noise, LFOs, S&H, etc.
  };

  static constexpr SourceType ordered_types[] = {
    TYPE_NONE,
    TYPE_ADC,
    TYPE_DAC,
    TYPE_MIDI,
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
    TYPE_II,
#endif
    TYPE_INTERNAL,
  };

private:
  uint8_t source = 0; // 3 bits type | 5 bits index
public:
  int8_t attenuversion = 60; // 60 is 100%
                             // max range is +/- 127 (448%)

  static const int num_sources = 3 * IO_CHANNEL_COUNT;

  static constexpr size_t Size = 16; // Make this compatible with Packable

  // Absolute native source access: no preset migration, encoder simulation or
  // attenuation mutation. Invalid/reserved selections leave the map untouched.
  uint8_t Source() const { return source; }
  bool SetSource(uint8_t candidate) {
    const auto type = static_cast<SourceType>(candidate & 0xe0);
    const int count = channel_count(type);
    if (count <= 0 || (candidate & 0x1f) >= count) return false;
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
    if (!phz::ii::SelectableIiInput(phz::ii::virtual_inputs, candidate)) return false;
#endif
    source = candidate;
    return true;
  }
  // Restore is distinct from a new selection: legacy disabled inputs retain
  // their binding, but an Output slot is never a legal CV input.
  bool RestoreSource(uint8_t candidate) {
    const auto type = static_cast<SourceType>(candidate & 0xe0);
    const int count = channel_count(type);
    if (count <= 0 || (candidate & 0x1f) >= count) return false;
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
    if (type == TYPE_II &&
        phz::ii::virtual_inputs.DirectionalMappings::Get(candidate & 31).direction ==
          phz::ii::DirectionalMappings::Direction::Output) return false;
#endif
    source = candidate;
    return true;
  }

  void AutoLearn() {
    if (source_type() == TYPE_MIDI) {
      frame.MIDIState.mapping[index()].AutoLearn();
      frame.MIDIState.UpdateMidiChannelFilter();
    }
  }

  util::SemitoneQuantizer semitone_quant;

  const bool enabled() const { return source != 0; }
  SourceType source_type() const {
    return (SourceType)(source & (0x7 << 5)); // upper 3 bits
  }
  inline uint8_t index() const {
    return source & 0x1f; // lower 5 bits
  }
  constexpr int channel_count(SourceType t) const {
    switch(t) {
      default:
        return 0;
      case TYPE_NONE:
        return 1;
      case TYPE_ADC:
        // strictly hardware inputs
        return ADC_CHANNEL_COUNT;
      case TYPE_DAC:
        // This one can have virtual signals.
        // 2 outputs per applet slot, including Clock and Audio Applets...
        return 2 * APPLET_CURSOR_COUNT;
      case TYPE_MIDI:
        return MIDIMAP_MAX;
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
      case TYPE_II:
        return 32;
#endif
      case TYPE_INTERNAL:
        return 1;
    }
  }

  int RawIn() const {
    switch (source_type()) {
      case TYPE_ADC:
        return frame.In(index());

      case TYPE_DAC:
        return frame.ViewOut(index());

      case TYPE_MIDI:
        return frame.MIDIState.mapping[index()].ViewOut();
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
      case TYPE_II:
        return phz::ii::virtual_inputs.Value(index());
#endif

      case TYPE_INTERNAL:
        // noise source
        if (index() == 0)
          return int(random(ONE_OCTAVE * HS::octave_max * 2)) - ONE_OCTAVE * HS::octave_max;
        // TODO: LFOs and other basic stuff
        // else, fall through to default
      default:
        return 0;
    }
  }

  int In(int default_value = 0) const {
    if (!source) return default_value;
    return RawIn() * Atten(attenuversion) / 1000;
  }

  float InF(float default_value = 0.0f) const {
    if (!source) return default_value;
    return 0.001f * Atten(attenuversion) * static_cast<float>(RawIn())
      / static_cast<float>((source_type() == TYPE_ADC)?HEMISPHERE_MAX_INPUT_CV:HEMISPHERE_MAX_CV);
  }

  int SemitoneIn(int default_value = 0) {
    return semitone_quant.Process(In(default_value));
  }

  int InRescaled(int max_value) const {
    return Proportion(In(), (source_type() == TYPE_ADC)?HEMISPHERE_MAX_INPUT_CV:HEMISPHERE_MAX_CV, max_value);
  }

  bool ChangeSourceType(int dir) {
    const int count = sizeof(ordered_types) / sizeof(ordered_types[0]);
    int tidx = 0;
    while (tidx < count && source_type() != ordered_types[tidx]) ++tidx;
    if (tidx >= count) tidx = 0;
    tidx += (dir>0?1:-1);
    CONSTRAIN(tidx, 0, count - 1);
    int sidx = constrain(index(), 0, channel_count(ordered_types[tidx]) - 1);

    SourceType oldtype = source_type();
    source = ordered_types[tidx] | sidx;
    return oldtype != source_type();
  }
private:
  void ChangeSourceStep(int dir) {
    int sidx = index() + dir;
    if (sidx < 0) {
      if (ChangeSourceType(-1))
        sidx = channel_count(source_type()) - 1;
    }
    if (sidx >= channel_count(source_type())) {
      if (ChangeSourceType(1))
        sidx = 0;
    }
    CONSTRAIN(sidx, 0, channel_count(source_type()) - 1);
    source = source_type() | uint8_t(sidx);
  }
public:
  void ChangeSource(int dir) {
    if (!dir) return;
    // Preserve accelerated navigation, but bound arithmetic and skip at most
    // the complete 32-slot ii block. Every applet uses this same selector.
    ChangeSourceStep(constrain(dir, -32, 32));
#if defined(ARDUINO_TEENSY41) || defined(PHZ_VIRTUAL_INPUT_TEST)
    for (unsigned n = 0; n < 32 &&
         !phz::ii::SelectableIiInput(phz::ii::virtual_inputs, source); ++n)
      ChangeSourceStep(dir > 0 ? 1 : -1);
#endif
  }

  void SetMidiMap(uint8_t midx) {
    CONSTRAIN(midx, 0, channel_count(TYPE_MIDI) - 1);
    source = TYPE_MIDI | (midx & 0x1f);
  }

  // UI category shortcut. Physical and virtual logical outputs share their
  // stored type, but are separate browsing groups. Never change live routing
  // while scanning or expose disabled/output-mode ii input candidates.
  void ChangeSourceGroup(int dir) {
    if (!dir) return;
    const auto group = [](const CVInputMap& map) {
      return int(map.source_type()) * 2 +
        (map.source_type() == TYPE_DAC && map.index() >= DAC_CHANNEL_COUNT);
    };
    const int original = group(*this);
    CVInputMap candidate(*this);
    for (unsigned n = 0; n < 256; ++n) {
      const auto previous = candidate.Source();
      candidate.ChangeSource(dir > 0 ? 1 : -1);
      if (candidate.Source() == previous) return;
      if (group(candidate) != original) {
        SetSource(candidate.Source());
        return;
      }
    }
  }
  void SetInput(uint8_t idx) {
    CONSTRAIN(idx, 0, channel_count(TYPE_ADC) - 1);
    source = TYPE_ADC | (idx & 0x1f);
  }
  void SetOutput(uint8_t idx) {
    CONSTRAIN(idx, 0, channel_count(TYPE_DAC) - 1);
    source = TYPE_DAC | (idx & 0x1f);
  }
  void SetInternal(uint8_t idx) {
    CONSTRAIN(idx, 0, channel_count(TYPE_INTERNAL) - 1);
    source = TYPE_INTERNAL | (idx & 0x1f);
  }

  const char * InputName() const {
    static char in_label[] = "C 1";
    static char ii_label[] = "I 1";

    if (source_type() == TYPE_II) {
      const unsigned n = index() + 1;
      ii_label[1] = n < 10 ? ' ' : char('0' + n / 10);
      ii_label[2] = char('0' + n % 10);
      return ii_label;
    }

    if (!source) {
      return " - ";
    }
    const char type[] = {' ', '-', 'C', '#', 'M', 'G', '?', '?'};
    uint8_t idx = index();

    in_label[0] = type[source_type() >> 5];
    switch (source_type()) {
      case TYPE_DAC:
        in_label[1] = OC::Strings::capital_letters[idx][0];
        in_label[2] = ' ';
        break;

      case TYPE_MIDI:
      case TYPE_ADC:
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

  uint8_t const* Icon() const {
    if (source_type() == TYPE_II) {
      static const uint8_t ii_icon[8] = {0, 0x7a, 0, 0, 0x7a, 0, 0, 0};
      return ii_icon;
    }
    switch (source_type()) {
      case TYPE_NONE:
        return PARAM_MAP_ICONS + 0;
      case TYPE_INTERNAL:
        return MOD_ICON;
      case TYPE_MIDI:
        return PhzIcons::midiIn;

      case TYPE_ADC:
        if (index() < 8)
          return PARAM_MAP_ICONS + (1 + index()) * 8;
      case TYPE_DAC:
        if (index() < 8)
          return PARAM_MAP_ICONS + (1 + ADC_CHANNEL_COUNT + index()) * 8;

      default:
        return ZAP_ICON;
    }
  }

  uint16_t Pack() const {
    return uint16_t(source) | (uint16_t(uint8_t(attenuversion)) << 8);
  }

  void Unpack(uint16_t data) {
    source = data & 0xFF;

    // migrate old data; zero is still disabled
    if (source && (source >> 5) < 2) {
      if (source <= ADC_CHANNEL_COUNT)
        SetInput(source - 1); // CV input
      else if (source <= DAC_CHANNEL_COUNT + ADC_CHANNEL_COUNT)
        SetOutput(source - ADC_CHANNEL_COUNT - 1); // DAC output
      else // if (source <= DAC_CHANNEL_COUNT + ADC_CHANNEL_COUNT + MIDIMAP_MAX)
        SetMidiMap(source - ADC_CHANNEL_COUNT - DAC_CHANNEL_COUNT - 1);
    }
    // enforce constrain for presets saved with v2.0-alpha
    if (source_type() == TYPE_INTERNAL) SetInternal(index());

    attenuversion = extract_value<int8_t>(data >> 8);
  }
};

// Let's PackingUtils know this is Packable as is.
constexpr CVInputMap& pack(CVInputMap& input) {
  return input;
}
