#pragma once
#include "../parameters/CvMapping.h"

template <AudioChannels Channels>
class InputApplet : public HemisphereAudioApplet {
public:
  phz::parameters::Schema Parameters() const override;
  const uint64_t applet_id() override {
    return strhash("Input");
  }

  const char* applet_name() {
    return "Input";
  }
  void Start() override {
    if (MONO == Channels) {
      modesel_ = hemisphere & 1;
    }

    ForEachChannel(ch) {
      PatchCable(OC::AudioIO::InputStream(0), ch, srcmix[ch], 0);
#ifdef AUDIO_INTERFACE
      PatchCable(OC::AudioIO::InputStream(1), ch, srcmix[ch], 1);
#endif

      attenuations[ch].Method(INTERPOLATION_LINEAR);
      attenuations[ch].Acquire();
      PatchCable(srcmix[ch], 0, vcas[ch], 0);
      PatchCable(attenuations[ch], 0, vcas[ch], 1);

      PatchCable(srcmix[ch], 0, peakmeter[ch], 0);
    }

    for (int i = 0; i < Channels; ++i) {
      ForEachChannel(ch) {
        PatchCable(vcas[ch], 0, mixer[i], ch);
      }
      PatchCable(passthru, i, mixer[i], 2);
      PatchCable(mixer[i], 0, output, i);

      mixer[i].gain(0, 0.0); // Left to i
      mixer[i].gain(1, 0.0); // Right to i

      mixer[i].gain(2, 1.0); // passthru
    }
    UpdateMix();
  }
  void Unload() override {
    for (auto& attenuation : attenuations) {
      attenuation.Release();
    }
    AllowRestart();
  }
  void Controller() override {
    ForEachChannel(ch) {
      attenuations[ch].Push(float_to_q15(level_cv.InF(1.0)));
    }
  }
  PHZ_APPLET_UI_CODE
  void View() override {
    const char* const txt[] = {
      "Left", "Right", "Dual", "Mixed",
#ifdef AUDIO_INTERFACE
      "USB L", "USB R", "USB Dual", "USB Mix"
#endif
    };
    gfxPrint(3, 15, txt[modesel_]);
    if (cursor == CHANNEL_MODE) gfxCursor(3, 23, 31, "Mode");
    gfxPrint(1, 45, "Lvl:");
    gfxPrintDb(level);
    if (cursor == IN_LEVEL) gfxCursor(26, 53, 30, "Gain");
    gfxStartCursor();
    gfxPrint(level_cv);
    gfxEndCursor(cursor == LEVEL_CV, false, level_cv.InputName());

    ForEachChannel(ch) {
      if (peakmeter[ch].available()) {
        int peaklvl = peakmeter[ch].read() * 64;
        gfxInvert(ch*61, 64 - peaklvl, 3, peaklvl);
      }
    }

    gfxDisplayInputMapEditor();
  }
  void OnDataRequest(std::array<uint64_t, CONFIG_SIZE>& data) override {
    data[0] = 0; // Always clear the legacy selector when reusing a save buffer.
    data[1] = PackPackables(modesel_, level, level_cv);
  }
  void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) override {
    if (data[0] != 0) {
      // backward compat
      bool mixtomono;
      UnpackPackables(data[0], pack<4>(modesel_), pack(level), pack<1>(mixtomono), level_cv);
      if (mixtomono || modesel_ == 2) modesel_ = MIXED;
      CONSTRAIN(modesel_, 0, MODE_COUNT - 1);
    } else {
      UnpackPackables(data[1], modesel_, level, level_cv);
    }
    // Mono Dual historically produced Left (and USB Dual produced USB Left).
    // Preserve that output while removing modes the mono UI cannot select.
    if (Channels == MONO && modesel_ == DUAL) modesel_ = LEFT;
#ifdef AUDIO_INTERFACE
    if (Channels == MONO && modesel_ == USB_DUAL) modesel_ = USB_L;
#endif
    phz::parameters::Domain mode;
    if (!DescribeParameter(1, mode) || !phz::parameters::NativeAllowed(mode, modesel_))
      modesel_ = LEFT; // Unavailable/corrupt saved modes never index the label table.
    level = constrain(level, LVL_MIN_DB - 1, LVL_MAX_DB);
    if (!level_cv.RestoreSource(level_cv.Source())) level_cv.SetSource(0);
    level_cv.attenuversion = constrain(level_cv.attenuversion, -127, 127);
    UpdateMix();
  }

  // *****
  PHZ_APPLET_UI_CODE
  void OnButtonPress() override {
    if (CheckEditInputMapPress(cursor, IndexedInput(LEVEL_CV, level_cv)))
      return;
    CursorToggle();
  }
  // *****
  PHZ_APPLET_UI_CODE
  void OnEncoderMove(int direction) override {
    if (!EditMode()) {
      MoveCursor(cursor, direction, MAX_CURSOR);
      return;
    }
    if (IsEditingInputMap()) {
      auto map = std::get_if<CVInputMap*>(&selected_input_map);
      if (map && *map == &level_cv) AdjustParameterNative(4, direction);
      return;
    }
    switch (cursor) {
      case IN_LEVEL:
        AdjustParameterNative(2, direction);
        break;
      case LEVEL_CV: {
        auto next = level_cv;
        next.ChangeSource(direction);
        SetParameterNative(3, next.Source());
        break;
      }
      case CHANNEL_MODE:
        ChangeMode(direction);
        break;
    }
  }

  AudioStream* InputStream() override {
    return &passthru;
  }
  AudioStream* OutputStream() override {
    return &output;
  }

  void UpdateMix() {
    float lvl_scalar = level < LVL_MIN_DB ? 0.0f : dbToScalar(level); // * level_cv.InF(1.0f);
    //q15_t lvl = float_to_q15(lvl_scalar);

    ForEachChannel(ch) {
      switch (modesel_) {
#ifdef AUDIO_INTERFACE
        case USB_DUAL:
        case USB_MIX:
        case USB_L:
        case USB_R:
          srcmix[ch].gain(0, 0.0f); // i2s
          srcmix[ch].gain(1, 1.0f); // usb
          break;
#endif
        case DUAL:
        case MIXED:
        case LEFT:
        case RIGHT:
          srcmix[ch].gain(0, 1.0f); // i2s
          srcmix[ch].gain(1, 0.0f); // usb
          break;
      }
    }
    for (int ch = 0; ch < Channels; ++ch) {
      switch (modesel_) {
#ifdef AUDIO_INTERFACE
        case USB_DUAL:
#endif
        case DUAL:
          mixer[ch].gain(0, lvl_scalar * (1-ch)); // Left to ch
          mixer[ch].gain(1, lvl_scalar * ch); // Right to ch
          break;
#ifdef AUDIO_INTERFACE
        case USB_MIX:
#endif
        case MIXED:
          // TODO: adjust by 0.5 when mixing to mono?
          mixer[ch].gain(0, lvl_scalar); // Left to ch
          mixer[ch].gain(1, lvl_scalar); // Right to ch
          break;
#ifdef AUDIO_INTERFACE
        case USB_L:
#endif
        case LEFT:
          mixer[ch].gain(0, lvl_scalar); // Left to ch
          mixer[ch].gain(1, 0.0f); // Right to ch
          break;
#ifdef AUDIO_INTERFACE
        case USB_R:
#endif
        case RIGHT:
          mixer[ch].gain(0, 0.0f); // Left to ch
          mixer[ch].gain(1, lvl_scalar); // Right to ch
          break;
      }
    }

  }

protected:
  void SetHelp() override {}

private:
  void ChangeMode(int direction) {
    phz::parameters::Domain mode;
    if (!DescribeParameter(1, mode)) return;
    size_t index = 0;
    for (size_t i = 0; i < mode.option_count; ++i)
      if (mode.options[i].native == modesel_) index = i;
    // Navigate legal choices, not enum ordinals (mono has permanent holes).
    const int64_t next = constrain(int64_t(index) + direction, int64_t(0),
                                    int64_t(mode.option_count - 1));
    SetParameterNative(1, mode.options[next].native);
  }

  AudioPassthrough<Channels> passthru;
  std::array<InterpolatingStream<>, 2> attenuations;
  std::array<AudioVCA, 2> vcas;
  AudioMixer<2> srcmix[2];
  AudioMixer<3> mixer[Channels];
  AudioPassthrough<Channels> output;
  AudioAnalyzePeak peakmeter[2];

  int8_t modesel_ = Channels == MONO ? LEFT : DUAL;
  int8_t level = 0;
  CVInputMap level_cv;

  enum ModeSelect {
    LEFT, RIGHT, DUAL, MIXED,
#ifdef AUDIO_INTERFACE
    USB_L, USB_R, USB_DUAL, USB_MIX,
#endif
    MODE_COUNT
  };
  enum InputCursor { CHANNEL_MODE, IN_LEVEL, LEVEL_CV, MAX_CURSOR = LEVEL_CV };
  int cursor = 0;
};

template <AudioChannels Channels>
phz::parameters::Schema PHZ_PARAMETER_CODE InputApplet<Channels>::Parameters() const {
  using namespace phz::parameters;
  using App = InputApplet<Channels>;
  using LevelMap = CvMapping<App, &App::level_cv>;
  // Fixed public codes equal named native modes. Mono holes stay reserved;
  // USB modes are absent (rejected) in builds without that interface.
  static constexpr Option monoModes[] PHZ_PARAMETER_STORAGE = {
    {0, LEFT}, {1, RIGHT}, {3, MIXED},
#ifdef AUDIO_INTERFACE
    {4, USB_L}, {5, USB_R}, {7, USB_MIX},
#endif
  };
  static constexpr Option stereoModes[] PHZ_PARAMETER_STORAGE = {
    {0, LEFT}, {1, RIGHT}, {2, DUAL}, {3, MIXED},
#ifdef AUDIO_INTERFACE
    {4, USB_L}, {5, USB_R}, {6, USB_DUAL}, {7, USB_MIX},
#endif
  };
  static constexpr Entry entries[] PHZ_PARAMETER_STORAGE = {
    {1, "modesel_", "Mode", "mode",
      {Kind::Enumeration, 0, 0, Channels == MONO ? monoModes : stereoModes,
       Channels == MONO ? sizeof(monoModes)/sizeof(Option) : sizeof(stereoModes)/sizeof(Option)},
      [](const Owner& o) -> int32_t { return static_cast<const App&>(o).modesel_; },
      [](Owner& o, int32_t v, uint64_t) {
        auto& app = static_cast<App&>(o);
        app.modesel_ = int8_t(v); app.UpdateMix(); return true;
      }},
    {2, "level", "Level", "dB", {Kind::Integer, LVL_MIN_DB - 1, LVL_MAX_DB},
      [](const Owner& o) -> int32_t { return static_cast<const App&>(o).level; },
      [](Owner& o, int32_t v, uint64_t) {
        auto& app = static_cast<App&>(o);
        app.level = int8_t(v); app.UpdateMix(); return true;
      }},
    LevelMap::Source(3, "level_cv.source", "Level CV source"),
    LevelMap::Attenuversion(4, "level_cv.attenuversion", "Level CV amount"),
  };
  static_assert(Valid(entries), "Invalid Input parameter schema");
  return {entries, sizeof(entries)/sizeof(entries[0])};
}
