#pragma once
#include "ii/SettingsBridge.h"
#include <algorithm>
#include <stdint.h>
// Board I/O/drawing seams only. The tests include the real applet headers and
// execute their real declarations, setters, UI handlers and Controller code.
constexpr int ONE_OCTAVE = 1536;
namespace HS { inline int octave_max = 6; }
namespace OC { struct DAC { static inline int kOctaveZero = 3; }; }
#define DAC_20Vpp 0
#define HEMISPHERE_MAX_CV (HS::octave_max * ONE_OCTAVE)
#define HEMISPHERE_MIN_CV (-OC::DAC::kOctaveZero * ONE_OCTAVE)
#define ForEachChannel(ch) for (int ch = 0; ch < 2; ++ch)
#define CONSTRAIN(v,lo,hi) v = constrain(v,lo,hi)
template<class A,class B,class C> auto constrain(A a,B b,C c) { return a < b ? b : a > c ? c : a; }
inline int Proportion(int a,int b,int c) { return int(int64_t(a)*c/b); }
struct PackLocation { unsigned shift,width; };
inline void Pack(uint64_t& out,PackLocation p,int value) { out |= (uint64_t(value)&((uint64_t{1}<<p.width)-1))<<p.shift; }
inline int Unpack(uint64_t value,PackLocation p) { return (value>>p.shift)&((uint64_t{1}<<p.width)-1); }
namespace PhzIcons { inline const uint8_t* voltage=nullptr; inline const uint8_t* dualAttenuverter=nullptr; }
constexpr const uint8_t* DOWN_ICON=nullptr;
enum { HELP_DIGITAL1, HELP_DIGITAL2, HELP_CV1, HELP_CV2, HELP_OUT1, HELP_OUT2, HELP_EXTRA1, HELP_EXTRA2 };
struct HemisphereApplet : phz::parameters::Owner {
  bool gates[2]{}, editing=false;
  int inputs[2]{}, outputs[2]{};
  const char* help[8]{};
  bool Gate(int ch) { return gates[ch]; }
  int In(int ch) { return inputs[ch]; }
  void Out(int ch,int value) { outputs[ch]=value; }
  bool EditMode() const { return editing; }
  void CursorToggle() { editing=!editing; }
  bool CursorBlink() { return true; }
  template<class T> void MoveCursor(T& c,int d,int max) { c=constrain(c+d,0,max); }
  const char* OutputLabel(int ch) { return ch ? "B" : "A"; }
  template<class... A> void gfxPrint(A...) {}
  template<class... A> void gfxPrintVoltage(A...) {}
  template<class... A> void gfxInvert(A...) {}
  template<class... A> void gfxCursor(A...) {}
  template<class... A> void gfxIcon(A...) {}
  template<class... A> void gfxFrame(A...) {}
  bool SetParameterNative(uint16_t id,int32_t v) {
    phz::parameters::Domain d;
    return DescribeParameter(id,d) && SetParameter(id,v,d.dependencies);
  }
  bool AdjustParameterNative(uint16_t id,int direction) {
    phz::parameters::Domain d; int32_t old;
    if (!DescribeParameter(id,d) || !ReadParameter(id,old)) return false;
    return SetParameter(id,constrain(int64_t(old)+direction,int64_t(d.minimum),int64_t(d.maximum)),d.dependencies);
  }
};
