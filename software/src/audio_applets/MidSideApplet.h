#pragma once
#include "../parameters/Schema.h"

class MidSideApplet : public HemisphereAudioApplet {
public:
  phz::parameters::Schema Parameters() const override;
  const char* applet_name() {
    return "Mid/Side";
  }

  void Start() override {
    ForEachChannel(to) {
      PatchCable(mixers[to], 0, output, to);
      ForEachChannel(from) {
        PatchCable(input, from, mixers[to], from);
      }
    }
    SetGain(gain);
  }

  void Controller() override {}

  PHZ_APPLET_UI_CODE
  void View() override {
    gfxPos(32 - 5 * 3, 25);
    graphics.printf("%3ddB", gain);
  }

  PHZ_APPLET_UI_CODE
  void OnEncoderMove(int direction) override {
    AdjustParameterNative(1, direction);
  }

  uint64_t OnDataRequest() override {
    return PackPackables(gain);
  }

  void OnDataReceive(uint64_t data) override {
    UnpackPackables(data, gain);
    SetGain(gain);
  }

  AudioStream* InputStream() override {
    return &input;
  }

  AudioStream* OutputStream() override {
    return &output;
  }

  void SetGain(int8_t new_gain) {
    gain = constrain(new_gain, -90, 90);
    float scalar = dbToScalar(gain);
    mixers[0].gain(0, scalar);
    mixers[0].gain(1, scalar);
    mixers[1].gain(0, scalar);
    mixers[1].gain(1, -scalar);
  }

protected:

  void SetHelp() override {}

private:
  int8_t gain = -6; // in dB

  AudioPassthrough<2> input;
  std::array<AudioMixer<2>, 2> mixers;
  AudioPassthrough<2> output;
};

phz::parameters::Schema PHZ_PARAMETER_CODE MidSideApplet::Parameters() const {
  using namespace phz::parameters;
  using App = MidSideApplet;
  static constexpr Entry entries[] PHZ_PARAMETER_STORAGE = {
    {1, "gain", "Gain", "dB", {Kind::Integer, -90, 90},
      [](const Owner& o) -> int32_t { return static_cast<const App&>(o).gain; },
      [](Owner& o, int32_t value, uint64_t) {
        // The stored value alone does not update the signed audio matrix.
        static_cast<App&>(o).SetGain(value);
        return true;
      }},
  };
  static_assert(Valid(entries), "Invalid MidSide parameter schema");
  return {entries, sizeof(entries) / sizeof(entries[0])};
}
