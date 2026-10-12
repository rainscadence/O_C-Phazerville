// Copyright (c) 2018, Jason Justian
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "../util/clkdivmult.h"

class ClockDivider : public HemisphereApplet {
public:
    phz::parameters::Schema Parameters() const override;

  enum ClockDivCursor {
    CHAN_A1, CHAN_A2,
    CHAN_B1, CHAN_B2,

    LAST_SETTING = CHAN_B2
  };

    // extracted some stuff to clkdivmult.h
    //static constexpr int CLOCKDIV_MAX = 32;
    ClkDivMult divmult[4];

    const char* applet_name() {
        return "Clk Div";
    }
    const uint8_t* applet_icon() { return PhzIcons::clockDivider; }

    void Start() {
      divmult[0].steps = 2;
      divmult[2].steps = 4;
    }

    void Reset() {
      ForEachChannel(ch) {
        ForEachChannel(d) divmult[ch*2 + d].Reset();
      }
    }

    void Controller() {
        // Modulate setting via CV
        ForEachChannel(ch)
        {
          int div_m = div[ch];
          Modulate(div_m, ch, -CLOCKDIV_MAX, CLOCKDIV_MAX);
          divmult[ch*2].steps = div_m;
        }

        if (Clock(1)) Reset();

        bool clocked = Clock(0);
        ForEachChannel(ch)
        {
            bool trig = divmult[ch*2 + 0].Tick( clocked );
            trig = divmult[ch*2 + 1].Tick( trig );
            if (trig) ClockOut(ch);
        }
    }

    PHZ_APPLET_UI_CODE
    void View() {
        DrawSelector();
    }

    //void OnButtonPress() { }

    PHZ_APPLET_UI_CODE
    void OnEncoderMove(int direction) {
        if (!EditMode()) {
            MoveCursor(cursor, direction, LAST_SETTING);
            return;
        }

        AdjustParameterNative(cursor + 1, direction);
    }

    uint64_t OnDataRequest() {
        // Versioned applet-local format. All payload bytes are nonnegative and
        // fit; unmasked legacy Pack must never receive a signed negative value.
        uint64_t data = uint64_t(PRESET_V1) << 32;
        for (size_t i = 0; i < 2; ++i) {
          Pack(data, PackLocation {i*8,8}, uint8_t(div[i] + CLOCKDIV_MAX));
          Pack(data, PackLocation {16+i*8,8}, uint8_t(divmult[1+i*2].steps - CLOCKDIV_MIN));
        }
        return data;
    }

    void OnDataReceive(uint64_t data) {
        const uint32_t version = uint32_t(data >> 32);
        int primary[2], secondary[2];
        if (version == PRESET_V1) {
          for (size_t i = 0; i < 2; ++i) {
            primary[i] = Unpack(data, PackLocation {i*8,8}) - CLOCKDIV_MAX;
            secondary[i] = Unpack(data, PackLocation {16+i*8,8}) + CLOCKDIV_MIN;
            if (primary[i] < -CLOCKDIV_MAX || primary[i] > CLOCKDIV_MAX ||
                secondary[i] < CLOCKDIV_MIN || secondary[i] > CLOCKDIV_MAX) return;
          }
        } else {
          // Preserve the legacy decoder, including its already-corrupted
          // all-ones spill records. Lost values cannot be reconstructed.
          // Unknown version tags reject atomically rather than being guessed.
          if (version != 0 && version != UINT32_MAX) return;
          for (size_t i = 0; i < 2; ++i) {
            primary[i] = constrain(Unpack(data, PackLocation {i*8,8}) - 32, -CLOCKDIV_MAX, CLOCKDIV_MAX);
            secondary[i] = constrain(Unpack(data, PackLocation {16+i*8,8}) - 32, CLOCKDIV_MIN, CLOCKDIV_MAX);
          }
        }
        for (size_t i = 0; i < 2; ++i) {
          div[i] = primary[i];
          divmult[1+i*2].Set(secondary[i]);
        }
    }

protected:
  void SetHelp() {
    //                    "-------" <-- Label size guide
    help[HELP_DIGITAL1] = "Clock";
    help[HELP_DIGITAL2] = "Reset";
    help[HELP_CV1]      = "Ch1";
    help[HELP_CV2]      = "Ch2";
    help[HELP_OUT1]     = "Clock";
    help[HELP_OUT2]     = "Clock";
    help[HELP_EXTRA1] = "CV inputs modulate";
    help[HELP_EXTRA2] = "1st multiplier per Ch";
    //                  "---------------------" <-- Extra text size guide
  }

private:
    static constexpr uint32_t PRESET_V1 = 0x43440101;
    template<size_t Index> static int32_t ReadSecondary(const phz::parameters::Owner& owner) {
      return static_cast<const ClockDivider&>(owner).divmult[Index].steps;
    }
    template<size_t Index> static bool WriteSecondary(phz::parameters::Owner& owner, int32_t value, uint64_t) {
      // Preserve the native setter and running clock phase; no implicit reset.
      static_cast<ClockDivider&>(owner).divmult[Index].Set(value);
      return true;
    }
    int div[2] = {1, 2}; // Division setting before modulation.
                         // Positive for divisions, negative for multipliers.
                         // Zero is mute.
    int cursor = 0; // Which output is currently being edited

    void DrawSelector() {
        ForEachChannel(ch)
        {
            const int y = 15 + (ch * 24);

            gfxPrint(1, y, OutputLabel(ch));
            ForEachChannel(d) {
              const int s = divmult[ch*2 + d].steps;

              gfxPos(13, y + d*12);
              if (s > 0) {
                  gfxPrint("/");
                  gfxPrint(s);
                  gfxPrint(" Div");
              }
              if (s < 0) {
                  gfxPrint("x");
                  gfxPrint(-s);
                  gfxPrint(" Mult");
              }
              if (s == 0) {
                  gfxPrint("(off)");
              }
            }

            if (divmult[ch*2].steps != div[ch]) gfxIcon(18, y+8, CV_ICON);
        }
        gfxCursor(12, 23 + (cursor * 12), 50);
        gfxDottedLine(1, 36, 63, 36); // to separate the two channels
    }
};

phz::parameters::Schema PHZ_PARAMETER_CODE ClockDivider::Parameters() const {
  using namespace phz::parameters;
  static constexpr Entry entries[] PHZ_PARAMETER_STORAGE = {
    Field<ClockDivider, &ClockDivider::div, 0>::Make(1, "div[0]", "Primary A", "clock ratio", -CLOCKDIV_MAX, CLOCKDIV_MAX),
    {2, "divmult[1].steps", "Secondary A", "clock ratio", {Kind::Integer, CLOCKDIV_MIN, CLOCKDIV_MAX}, ReadSecondary<1>, WriteSecondary<1>},
    Field<ClockDivider, &ClockDivider::div, 1>::Make(3, "div[1]", "Primary B", "clock ratio", -CLOCKDIV_MAX, CLOCKDIV_MAX),
    {4, "divmult[3].steps", "Secondary B", "clock ratio", {Kind::Integer, CLOCKDIV_MIN, CLOCKDIV_MAX}, ReadSecondary<3>, WriteSecondary<3>},
  };
  static_assert(Valid(entries), "Invalid ClockDivider settings");
  return {entries, sizeof(entries) / sizeof(entries[0])};
}
