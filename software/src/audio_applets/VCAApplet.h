#pragma once

#include "../Audio/AudioVCA.h"
#include "../Audio/InterpolatingStream.h"
#include "../parameters/CvMapping.h"

template <AudioChannels Channels>
class VcaApplet : public HemisphereAudioApplet {
public:
  phz::parameters::Schema Parameters() const override;
  const char* applet_name() {
    return "VCA";
  }

  void Start() {
    // Less CPU than hermite, but I couldn't hear the difference whereas I could
    // with ZOH
    cv_stream.Method(INTERPOLATION_LINEAR);
    cv_stream.Acquire();
    for (int i = 0; i < Channels; i++) {
      PatchCable(input, i, vcas[i], 0);
      PatchCable(cv_stream, 0, vcas[i], 1);
      PatchCable(vcas[i], 0, output, i);
    }
    SetLevel(level);
    SetBias(bias);
    SetRectify(rectify);
  }

  void Unload() {
    cv_stream.Release();
    AllowRestart();
  }

  void Controller() {
    float lin_cv = level_cv.InF(1.0f);
    int effective_shape = constrain(shape + shape_cv.InRescaled(100), 0, 100);
    float cv = effective_shape > 0
        ? varexp(0.3f * effective_shape, (lin_cv > 1.0f ? 1.0f : lin_cv))
        : lin_cv;
    cv_stream.Push(float_to_q15(invert ? -cv : cv));
  }

  PHZ_APPLET_UI_CODE
  void View() {
    gfxPrint(1, 15, "Lvl:");
    gfxStartCursor();
    gfxPrintDb(level);
    gfxEndCursor(cursor == 0);
    gfxStartCursor();
    gfxPrint(level_cv);
    gfxEndCursor(cursor == 1, false, level_cv.InputName());

    gfxPrint(1, 25, "Off:");
    gfxStartCursor();
    gfxPrintDb(bias);
    gfxEndCursor(cursor == 2);

    gfxPrint(1, 35, "Exp: ");
    gfxStartCursor();
    graphics.printf("%3d%%", shape);
    gfxEndCursor(cursor == 3);
    gfxStartCursor();
    gfxPrint(shape_cv);
    gfxEndCursor(cursor == 4, false, shape_cv.InputName());

    gfxPrint(1, 45, "Rectify: ");
    gfxStartCursor();
    gfxPrintIcon(rectify ? CHECK_ON_ICON : CHECK_OFF_ICON);
    gfxEndCursor(cursor == 5);

    gfxPrint(1, 55, "Invert:  ");
    gfxStartCursor();
    gfxPrintIcon(invert ? CHECK_ON_ICON : CHECK_OFF_ICON);
    gfxEndCursor(cursor == 6);

    gfxDisplayInputMapEditor();
  }

  PHZ_APPLET_UI_CODE
  void OnButtonPress() override {
    if (CheckEditInputMapPress(
          cursor,
          IndexedInput(1, level_cv),
          IndexedInput(4, shape_cv)
        ))
      return;
    CursorToggle();
  }

  PHZ_APPLET_UI_CODE
  void OnEncoderMove(int direction) {
    if (!EditMode()) {
      MoveCursor(cursor, direction, NUM_PARAMS - 1);
      return;
    }
    // Use the shared setting for nested attenuation edits too.
    if (IsEditingInputMap()) {
      auto map = std::get_if<CVInputMap*>(&selected_input_map);
      if (map && *map == &level_cv) AdjustParameterNative(3, direction);
      else if (map && *map == &shape_cv) AdjustParameterNative(7, direction);
      return;
    }
    switch (cursor) {
      case 0:
        AdjustParameterNative(1, direction);
        break;
      case 1:
        ChangeSource(2, level_cv, direction);
        break;
      case 2:
        AdjustParameterNative(4, direction);
        break;
      case 3:
        AdjustParameterNative(5, direction);
        break;
      case 4:
        ChangeSource(6, shape_cv, direction);
        break;
      case 5:
        SetParameterNative(8, !rectify);
        break;
      case 6:
        SetParameterNative(9, !invert);
        break;
    }
  }

  void OnDataRequest(std::array<uint64_t, CONFIG_SIZE>& data) {
    uint64_t& d = data[0];
    d = PackPackables(level, bias, shape);
    Pack(d, {62, 1}, rectify);
    Pack(d, {63, 1}, invert);
    data[1] = PackPackables(level_cv, shape_cv);
  }

  void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) {
    uint64_t d = data[0];
    UnpackPackables(d, level, bias, shape);
    rectify = Unpack(d, {62, 1});
    invert = Unpack(d, {63, 1});
    UnpackPackables(data[1], level_cv, shape_cv);
    shape = constrain(shape, 0, 100);
    SanitizeMap(level_cv);
    SanitizeMap(shape_cv);

    SetLevel(level);
    SetBias(bias);
    SetRectify(rectify);
  }

  AudioStream* InputStream() override {
    return &input;
  }
  AudioStream* OutputStream() override {
    return &output;
  }

  void SetLevel(int lvl) {
    level = constrain(lvl, LVL_MIN_DB - 1, LVL_MAX_DB);
    float lvl_scalar = level < LVL_MIN_DB ? 0.0f : dbToScalar(level);
    for (auto& vca : vcas) vca.level(lvl_scalar);
  }

  void SetBias(int b) {
    bias = constrain(b, LVL_MIN_DB - 1, LVL_MAX_DB);
    float bias_scalar = bias < LVL_MIN_DB ? 0.0f : dbToScalar(bias);
    for (auto& vca : vcas) vca.bias(bias_scalar);
  }

  void SetRectify(bool r) {
    rectify = r;
    for (auto& vca : vcas) vca.rectify(rectify);
  }

protected:
  void SetHelp() override {}

private:
  static void SanitizeMap(CVInputMap& map) {
    if (!map.RestoreSource(map.Source())) map.SetSource(0);
    map.attenuversion = constrain(map.attenuversion, -127, 127);
  }

  void ChangeSource(uint16_t id, const CVInputMap& map, int direction) {
    // Resolve UI navigation on a copy; only the shared setter mutates live state.
    auto next = map;
    next.ChangeSource(direction);
    SetParameterNative(id, next.Source());
  }

  static const int NUM_PARAMS = 7;

  int8_t cursor = 0;
  int8_t level = 0;
  int8_t bias = LVL_MIN_DB - 1;
  int8_t shape = 0;
  CVInputMap level_cv;
  CVInputMap shape_cv;
  bool rectify = true;
  bool invert = false;

  AudioPassthrough<Channels> input;
  InterpolatingStream<> cv_stream;
  std::array<AudioVCA, Channels> vcas;
  AudioPassthrough<Channels> output;

  // Gives variable curve exponent by controlling the base normalized to go from
  // 0 to 1 for powers 0 to 1.
  float varexp(float log2base, float power) {
    return (fastpow2(log2base * power) - 1.0f) / (fastpow2(log2base) - 1.0f);
  }
};

template <AudioChannels Channels>
phz::parameters::Schema PHZ_PARAMETER_CODE VcaApplet<Channels>::Parameters() const {
  using namespace phz::parameters;
  using App = VcaApplet<Channels>;
  using LevelMap = CvMapping<App, &App::level_cv>;
  using ShapeMap = CvMapping<App, &App::shape_cv>;
  static constexpr Entry entries[] PHZ_PARAMETER_STORAGE = {
    {1, "level", "Level", "dB", {Kind::Integer, LVL_MIN_DB - 1, LVL_MAX_DB},
      [](const Owner& o) -> int32_t { return static_cast<const App&>(o).level; },
      [](Owner& o, int32_t v, uint64_t) {
        static_cast<App&>(o).SetLevel(v); return true;
      }},
    LevelMap::Source(2, "level_cv.source", "Level CV source"),
    LevelMap::Attenuversion(3, "level_cv.attenuversion", "Level CV amount"),
    {4, "bias", "Offset", "dB", {Kind::Integer, LVL_MIN_DB - 1, LVL_MAX_DB},
      [](const Owner& o) -> int32_t { return static_cast<const App&>(o).bias; },
      [](Owner& o, int32_t v, uint64_t) {
        static_cast<App&>(o).SetBias(v); return true;
      }},
    Field<App, &App::shape>::Make(5, "shape", "Shape", "percent", 0, 100),
    ShapeMap::Source(6, "shape_cv.source", "Shape CV source"),
    ShapeMap::Attenuversion(7, "shape_cv.attenuversion", "Shape CV amount"),
    {8, "rectify", "Rectify", "boolean", {Kind::Boolean, 0, 1},
      [](const Owner& o) -> int32_t { return static_cast<const App&>(o).rectify; },
      [](Owner& o, int32_t v, uint64_t) {
        static_cast<App&>(o).SetRectify(v != 0); return true;
      }},
    Field<App, &App::invert>::Make(9, "invert", "Invert", "boolean", 0, 1),
  };
  static_assert(Valid(entries), "Invalid VCA parameter schema");
  return {entries, sizeof(entries) / sizeof(entries[0])};
}
