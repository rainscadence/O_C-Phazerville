#pragma once
#include "../parameters/CvMapping.h"

template <AudioChannels Channels>
class LadderApplet : public HemisphereAudioApplet {

public:
  phz::parameters::Schema Parameters() const override;
  const char* applet_name() {
    return "LadderLPF";
  }

  void Start() {
    for (int i = 0; i < Channels; i++) {
      PatchCable(input, i, filters[i], 0);
      PatchCable(filters[i], 0, output, i);
    }
  }

  void Controller() {
    for (int i = 0; i < Channels; i++) {
      filters[i].frequency(PitchToRatio(pitch + pitch_cv.In() + pitch_cv2.In()) * C3);
      filters[i].resonance(0.01f * res + res_cv.InF());
      filters[i].inputDrive(0.01f * gain + gain_cv.InF());
      filters[i].passbandGain(0.01f * pb_gain);
    }
  }

  PHZ_APPLET_UI_CODE
  void View() override {
    const int label_x = 1;
    gfxStartCursor(label_x, 15);
    gfxPrintPitchHz(pitch);
    gfxEndCursor(cursor == 0);
    gfxStartCursor();
    gfxPrint(pitch_cv);
    gfxEndCursor(cursor == 1, false, pitch_cv.InputName());
    gfxStartCursor();
    gfxPrint(pitch_cv2);
    gfxEndCursor(cursor == 2, false, pitch_cv2.InputName());

    gfxPrint(label_x, 25, "Res: ");
    gfxStartCursor();
    graphics.printf("%3d%%", res);
    gfxEndCursor(cursor == 3);
    gfxStartCursor();
    gfxPrint(res_cv);
    gfxEndCursor(cursor == 4, false, res_cv.InputName());

    gfxPrint(label_x, 35, "Drv: ");
    gfxStartCursor();
    graphics.printf("%3d%%", gain);
    gfxEndCursor(cursor == 5);
    gfxStartCursor();
    gfxPrint(gain_cv);
    gfxEndCursor(cursor == 6, false, gain_cv.InputName());

    gfxPrint(label_x, 45, "PBG: ");
    gfxStartCursor();
    graphics.printf("%3d", pb_gain);
    gfxEndCursor(cursor == 7);

    gfxDisplayInputMapEditor();
  }

  PHZ_APPLET_UI_CODE
  void OnButtonPress() override {
    if (CheckEditInputMapPress(
          cursor,
          IndexedInput(1, pitch_cv),
          IndexedInput(2, pitch_cv2),
          IndexedInput(4, res_cv),
          IndexedInput(6, gain_cv)
        ))
      return;
    CursorToggle();
  }

  PHZ_APPLET_UI_CODE
  void OnEncoderMove(int direction) override {
    if (!EditMode()) {
      MoveCursor(cursor, direction, 7);
      return;
    }
    if (IsEditingInputMap()) {
      auto map = std::get_if<CVInputMap*>(&selected_input_map);
      if (map && *map == &pitch_cv) AdjustParameterNative(6, direction);
      else if (map && *map == &pitch_cv2) AdjustParameterNative(8, direction);
      else if (map && *map == &res_cv) AdjustParameterNative(10, direction);
      else if (map && *map == &gain_cv) AdjustParameterNative(12, direction);
      return;
    }
    switch (cursor) {
      case 0:
        SetParameterNative(1, constrain(
          int64_t(pitch) + int64_t(direction) * 16,
          int64_t(-8 * 12 * 128), int64_t(8 * 12 * 128)));
        break;
      case 1:
        ChangeParameterSource(5, pitch_cv, direction);
        break;
      case 2:
        ChangeParameterSource(7, pitch_cv2, direction);
        break;
      case 3:
        AdjustParameterNative(2, direction);
        break;
      case 4:
        ChangeParameterSource(9, res_cv, direction);
        break;
      case 5:
        AdjustParameterNative(3, direction);
        break;
      case 6:
        ChangeParameterSource(11, gain_cv, direction);
        break;
      case 7:
        AdjustParameterNative(4, direction);
        break;
    }
  }

  void OnDataRequest(std::array<uint64_t, CONFIG_SIZE>& data) {
    data[0] = PackPackables(pitch, res, gain, pb_gain);
    data[1] = PackPackables(pitch_cv, res_cv, gain_cv, pitch_cv2);
  }

  void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) {
    UnpackPackables(data[0], pitch, res, gain, pb_gain);
    UnpackPackables(data[1], pitch_cv, res_cv, gain_cv, pitch_cv2);
    pitch = constrain(pitch, -8 * 12 * 128, 8 * 12 * 128);
    res = constrain(res, 0, 180);
    gain = constrain(gain, 0, 400);
    pb_gain = constrain(pb_gain, 0, 50);
    for (auto* map : {&pitch_cv, &pitch_cv2, &res_cv, &gain_cv}) {
      if (!map->RestoreSource(map->Source())) map->SetSource(0);
      map->attenuversion = constrain(map->attenuversion, -127, 127);
    }
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
  void ChangeParameterSource(uint16_t id, const CVInputMap& map, int direction) {
    auto next = map;
    next.ChangeSource(direction);
    SetParameterNative(id, next.Source());
  }

  int8_t cursor = 0;
  int16_t pitch = 1 * 12 * 128; // C4
  int16_t res = 0;
  int16_t gain = 100;
  int16_t pb_gain = 50;
  CVInputMap pitch_cv;
  CVInputMap pitch_cv2;
  CVInputMap res_cv;
  CVInputMap gain_cv;

  AudioPassthrough<Channels> input;
  std::array<AudioFilterLadder, Channels> filters;
  AudioPassthrough<Channels> output;
};

template <AudioChannels Channels>
phz::parameters::Schema PHZ_PARAMETER_CODE LadderApplet<Channels>::Parameters() const {
  using namespace phz::parameters;
  using App = LadderApplet<Channels>;
  using Pitch = CvMapping<App, &App::pitch_cv>;
  using Pitch2 = CvMapping<App, &App::pitch_cv2>;
  using Res = CvMapping<App, &App::res_cv>;
  using Gain = CvMapping<App, &App::gain_cv>;
  static constexpr Entry entries[] PHZ_PARAMETER_STORAGE = {
    Field<App, &App::pitch>::Make(1, "pitch", "Cutoff pitch", "pitch128", -12288, 12288),
    Field<App, &App::res>::Make(2, "res", "Resonance", "percent", 0, 180),
    Field<App, &App::gain>::Make(3, "gain", "Input drive", "percent", 0, 400),
    Field<App, &App::pb_gain>::Make(4, "pb_gain", "Passband gain", "percent", 0, 50),
    Pitch::Source(5, "pitch_cv.source", "Pitch CV source"),
    Pitch::Attenuversion(6, "pitch_cv.attenuversion", "Pitch CV amount"),
    Pitch2::Source(7, "pitch_cv2.source", "Pitch CV 2 source"),
    Pitch2::Attenuversion(8, "pitch_cv2.attenuversion", "Pitch CV 2 amount"),
    Res::Source(9, "res_cv.source", "Resonance CV source"),
    Res::Attenuversion(10, "res_cv.attenuversion", "Resonance CV amount"),
    Gain::Source(11, "gain_cv.source", "Drive CV source"),
    Gain::Attenuversion(12, "gain_cv.attenuversion", "Drive CV amount"),
  };
  static_assert(Valid(entries), "Invalid Ladder parameter schema");
  return {entries, sizeof(entries) / sizeof(entries[0])};
}
