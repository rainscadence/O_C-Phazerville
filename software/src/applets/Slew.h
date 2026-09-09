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

#define HEM_SLEW_MAX_VALUE 200
#define HEM_SLEW_MAX_TICKS 64000

class Slew : public HemisphereApplet {
public:
    phz::parameters::Schema Parameters() const override;

    const char* applet_name() { // Maximum 10 characters
        return "Slew";
    }
    const uint8_t* applet_icon() { return PhzIcons::slew; }

    void Start() {
        ForEachChannel(ch) signal[ch] = 0;
        rise = 50;
        fall = 50;
    }

    void Controller() {
        ForEachChannel(ch)
        {
            // Multiplication is defined for negative native input; signed
            // left shift is not. The native CV range fits the fixed-point type.
            simfloat input = static_cast<simfloat>(int64_t(In(ch)) * (int64_t{1} << 14));
            if (Gate(ch)) signal[ch] = input; // Defeat slew when channel's gate is high
            if (input != signal[ch]) {

                int segment = (input > signal[ch]) ? rise : fall;
                simfloat remaining = input - signal[ch];

                // The number of ticks it would take to get from 0 to HEMISPHERE_MAX_INPUT_CV
                int max_change = Proportion(segment, HEM_SLEW_MAX_VALUE, HEM_SLEW_MAX_TICKS);

                // The number of ticks it would take to move the remaining amount at max_change
                int ticks_to_remaining = Proportion(simfloat2int(remaining), HEMISPHERE_MAX_INPUT_CV, max_change);
                if (ticks_to_remaining < 0) ticks_to_remaining = -ticks_to_remaining;

                // Apply channel scaling before guarding the final divisor.
                if (ch == 1) ticks_to_remaining /= 2;
                simfloat delta;
                if (ticks_to_remaining <= 0) {
                    delta = remaining;
                } else {
                    delta = remaining / ticks_to_remaining;
                }
                signal[ch] += delta;
            }
            Out(ch, simfloat2int(signal[ch]));
        }
    }

    void View() {
        DrawIndicator();
    }

    void OnButtonPress() {
        cursor = 1 - cursor;
    }

    void OnEncoderMove(int direction) {
        AdjustParameterNative(cursor == 0 ? 1 : 2, direction);
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        Pack(data, PackLocation {0,8}, rise);
        Pack(data, PackLocation {8,8}, fall);
        return data;
    }

    void OnDataReceive(uint64_t data) {
        rise = constrain(Unpack(data, PackLocation {0,8}), 0, HEM_SLEW_MAX_VALUE);
        fall = constrain(Unpack(data, PackLocation {8,8}), 0, HEM_SLEW_MAX_VALUE);
    }

protected:
    void SetHelp() {
        //                    "-------" <-- Label size guide
        help[HELP_DIGITAL1] = "Defeat1";
        help[HELP_DIGITAL2] = "Defeat2";
        help[HELP_CV1]      = "Input 1";
        help[HELP_CV2]      = "Input 2";
        help[HELP_OUT1]     = "(1) Lin";
        help[HELP_OUT2]     = "(2) Exp";
        help[HELP_EXTRA1] = "";
        help[HELP_EXTRA2] = "";
       //                  "---------------------" <-- Extra text size guide
    }

private:
    template<int Slew::* Member> static int32_t ReadSetting(const phz::parameters::Owner& owner) {
      return static_cast<const Slew&>(owner).*Member;
    }
    template<int Slew::* Member> static bool WriteSetting(phz::parameters::Owner& owner, int32_t value, uint64_t) {
      auto& app = static_cast<Slew&>(owner);
      app.*Member = value;
      app.last_ms_value = Proportion(value, HEM_SLEW_MAX_VALUE, HEM_SLEW_MAX_TICKS) / 17;
      app.last_change_ticks = OC::CORE::ticks;
      return true;
    }
    int rise; // Time to reach signal level if signal < 5V
    int fall; // Time to reach signal level if signal > 0V
    simfloat signal[2]; // Current signal level for each channel
    int cursor; // 0 = Rise, 1 = Fall
    int last_ms_value;
    int last_change_ticks;

    void DrawIndicator() {
        // Rise portion
        int r_x = Proportion(rise, 200, 31);
        gfxLine(0, 62, r_x, 33, cursor == 1);

        // Fall portion
        int f_x = 62 - Proportion(fall, 200, 31);
        gfxLine(f_x, 33, 62, 62, cursor == 0);

        // Center portion, if necessary
        gfxLine(r_x, 33, f_x, 33, 1);

        // Output indicators
        ForEachChannel(ch)
        {
            if (Gate(ch)) gfxFrame(1, 15 + (ch * 8), ProportionCV(ViewOut(ch), 62), 6);
            else gfxRect(1, 15 + (ch * 8), ProportionCV(ViewOut(ch), 62), 6);
        }

        // Change indicator, if necessary
        if (OC::CORE::ticks - last_change_ticks < 20000) {
            gfxPrint(15, 43, last_ms_value);
            gfxPrint("ms");
        }
    }
};

phz::parameters::Schema Slew::Parameters() const {
  using namespace phz::parameters;
  static constexpr Entry entries[] PHZ_PARAMETER_STORAGE = {
    {1, "rise", "Rise", "coefficient", {Kind::Integer, 0, HEM_SLEW_MAX_VALUE}, ReadSetting<&Slew::rise>, WriteSetting<&Slew::rise>},
    {2, "fall", "Fall", "coefficient", {Kind::Integer, 0, HEM_SLEW_MAX_VALUE}, ReadSetting<&Slew::fall>, WriteSetting<&Slew::fall>},
  };
  static_assert(Valid(entries), "Invalid Slew settings");
  return {entries, sizeof(entries) / sizeof(entries[0])};
}
