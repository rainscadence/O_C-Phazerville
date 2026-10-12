#pragma once
#include "../parameters/CvMapping.h"

class CrosspanApplet : public HemisphereAudioApplet {
public:
  phz::parameters::Schema Parameters() const override;
  const char* applet_name() {
    return "Crosspan";
  }

  void Start() override {
    ForEachChannel(from) {
      attenuations[from].Method(INTERPOLATION_LINEAR);
      attenuations[from].Acquire();
      ForEachChannel(to) {
        mixers[from].gain(to, 1.0f);
        vcas[from][to].level(1.0f);
        vcas[from][to].bias(0.0f);
        vcas[from][to].rectify(true);
      }
    }

    PatchCable(input, 0, vcas[0][0], 0);
    PatchCable(input, 0, vcas[0][1], 0);
    PatchCable(input, 1, vcas[1][0], 0);
    PatchCable(input, 1, vcas[1][1], 0);

    PatchCable(attenuations[0], 0, vcas[0][0], 1);
    PatchCable(attenuations[0], 0, vcas[1][1], 1);
    PatchCable(attenuations[1], 0, vcas[0][1], 1);
    PatchCable(attenuations[1], 0, vcas[1][0], 1);

    PatchCable(vcas[0][0], 0, mixers[0], 0);
    PatchCable(vcas[0][1], 0, mixers[1], 0);
    PatchCable(vcas[1][0], 0, mixers[0], 1);
    PatchCable(vcas[1][1], 0, mixers[1], 1);

    PatchCable(mixers[0], 0, output, 0);
    PatchCable(mixers[1], 0, output, 1);

    AllowRestart();
  }

  void Unload() override {
    ForEachChannel(ch) attenuations[ch].Release();
  }

  void Controller() override {
    float total_crosspan = constrain(
      static_cast<float>(crosspan) * 0.01f + crosspan_cv.InF(), 0.0f, 1.0f
    );
    float out, in;
    if (xfade_shape == EQUAL_POWER) EqualPowerFade(out, in, total_crosspan);
    else {
      out = 1.0f - total_crosspan;
      in = total_crosspan;
    }
    attenuations[0].Push(float_to_q15(out));
    attenuations[1].Push(float_to_q15(in));
  }

  PHZ_APPLET_UI_CODE
  void View() override {
    gfxFrame(1, 28, 62, 8);

    int param_x = static_cast<int>(static_cast<float>(crosspan) * 0.01f * 62);
    gfxLine(param_x, 26, param_x, 38);

    int cv_x = constrain(param_x + crosspan_cv.InF() * 62, 1, 63);
    if (cv_x < param_x) gfxRect(cv_x, 30, param_x - cv_x, 4);
    else gfxRect(param_x, 30, cv_x - param_x, 4);
    if (cursor == 0) gfxCursor(1, 40, 62, 14);

    gfxStartCursor(28, 42);
    gfxPrint(crosspan_cv);
    gfxEndCursor(cursor == 1, false, crosspan_cv.InputName());

    gfxStartCursor(32 - 3 * 9, 55);
    gfxPrint(xfade_shape == EQUAL_POWER ? "Equal pow" : "Equal amp");
    gfxEndCursor(cursor == 2);

    gfxDisplayInputMapEditor();
  }

  PHZ_APPLET_UI_CODE
  void OnButtonPress() override {
    if (CheckEditInputMapPress(cursor, IndexedInput(1, crosspan_cv)))
      return;
    CursorToggle();
  }

  PHZ_APPLET_UI_CODE
  void OnEncoderMove(int direction) override {
    if (!EditMode()) {
      MoveCursor(cursor, direction, 2);
      return;
    }
    if (IsEditingInputMap()) {
      auto map = std::get_if<CVInputMap*>(&selected_input_map);
      if (map && *map == &crosspan_cv) AdjustParameterNative(3, direction);
      return;
    }
    switch (cursor) {
      case 0:
        AdjustParameterNative(1, direction);
        break;
      case 1:
        {
          auto next = crosspan_cv;
          next.ChangeSource(direction);
          SetParameterNative(2, next.Source());
        }
        break;
      case 2:
        SetParameterNative(4, constrain(int64_t(xfade_shape) + direction, int64_t(0), int64_t(1)));
        break;

      default:
        break;
    }
  }

  uint64_t OnDataRequest() override {
    return PackPackables(crosspan, crosspan_cv, pack<1>(xfade_shape));
  }

  void OnDataReceive(uint64_t data) override {
    UnpackPackables(data, crosspan, crosspan_cv, pack<1>(xfade_shape));
    crosspan = constrain(crosspan, 0, 100);
    if (!crosspan_cv.RestoreSource(crosspan_cv.Source())) crosspan_cv.SetSource(0);
    crosspan_cv.attenuversion = constrain(crosspan_cv.attenuversion, -127, 127);
  }

  AudioStream* InputStream() override {
    return &input;
  }

  AudioStream* OutputStream() override {
    return &output;
  }

protected:
  void SetHelp() override {}

private:
  enum XfadeShape : bool { EQUAL_POWER, EQUAL_AMPLITUDE };
  int8_t cursor = 0;
  int8_t crosspan = 0;
  CVInputMap crosspan_cv;
  XfadeShape xfade_shape = EQUAL_POWER;

  AudioPassthrough<2> input;
  std::array<InterpolatingStream<>, 2> attenuations;
  std::array<std::array<AudioVCA, 2>, 2> vcas;
  std::array<AudioMixer<2>, 2> mixers;
  AudioPassthrough<2> output;
};

phz::parameters::Schema PHZ_PARAMETER_CODE CrosspanApplet::Parameters() const {
  using namespace phz::parameters;
  using App = CrosspanApplet;
  using Map = CvMapping<App, &App::crosspan_cv>;
  static constexpr Option shapeOptions[] PHZ_PARAMETER_STORAGE = {
    {0, EQUAL_POWER}, {1, EQUAL_AMPLITUDE},
  };
  static constexpr Entry entries[] PHZ_PARAMETER_STORAGE = {
    Field<App, &App::crosspan>::Make(1, "crosspan", "Crosspan", "percent", 0, 100),
    Map::Source(2, "crosspan_cv.source", "Crosspan CV source"),
    Map::Attenuversion(3, "crosspan_cv.attenuversion", "Crosspan CV amount"),
    {4, "xfade_shape", "Fade curve", "curve",
      {Kind::Enumeration, 0, 0, shapeOptions, 2},
      [](const Owner& o) -> int32_t { return static_cast<const App&>(o).xfade_shape; },
      [](Owner& o, int32_t value, uint64_t) {
        static_cast<App&>(o).xfade_shape = static_cast<XfadeShape>(value);
        return true;
      }},
  };
  static_assert(Valid(entries), "Invalid Crosspan parameter schema");
  return {entries, sizeof(entries) / sizeof(entries[0])};
}
