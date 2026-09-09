#pragma once
#include "AppletHost.h"
#include <array>
#include <tuple>
#include <variant>
#include <cmath>
#include <string>
#include "Audio.h"
#include "PackingUtils.h"
// Only board, frame, audio scheduling/CMSIS, and drawing are host seams.
// CVInputMapValue, packing, interpolation, applet and AudioVCA are production code.
#define OC_CONFIG_H_
constexpr int ADC_CHANNEL_COUNT=8,DAC_CHANNEL_COUNT=8,IO_CHANNEL_COUNT=8;
constexpr int APPLET_CURSOR_COUNT=8,MIDIMAP_MAX=32;
constexpr uint32_t OC_CORE_ISR_FREQ=16666;
#define CONSTRAIN(v,lo,hi) v=constrain(v,lo,hi)
namespace util {struct SemitoneQuantizer {int Process(int x){return x;}};}
namespace OC::Strings { inline const char* capital_letters[16]={"A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P"}; }
namespace PhzIcons {inline const uint8_t* midiIn=nullptr;}
inline uint8_t PARAM_MAP_ICONS[256]{};
inline const uint8_t *MOD_ICON=nullptr,*ZAP_ICON=nullptr;
inline long random(long max){return max/2;}
struct FrameFixture {
 int adc[8]{},dac[16]{};
 int In(int i)const{assert(i>=0&&i<8);return adc[i];}
 int ViewOut(int i)const{assert(i>=0&&i<16);return dac[i];}
 struct Midi {
  struct Mapping {int value=0;void AutoLearn(){}int ViewOut()const{return value;}} mapping[32];
  void UpdateMidiChannelFilter(){}
 } MIDIState;
} inline frame;
#include "CVInputMapValue.h"
#include "dsputils.h"
#include "dsputils_arm.h"
enum AudioChannels:uint8_t { NONE,MONO,STEREO };
template<AudioChannels Channels> struct AudioPassthrough:AudioStream {};
inline const uint8_t *CHECK_ON_ICON=nullptr,*CHECK_OFF_ICON=nullptr;
struct HemisphereAudioApplet:HemisphereApplet {
 static constexpr int CONFIG_SIZE=4,LVL_MIN_DB=-90,LVL_MAX_DB=90;
 virtual void OnButtonPress(){}
 virtual AudioStream* InputStream()=0;
 virtual AudioStream* OutputStream()=0;
 virtual void SetHelp(){}
 struct Graphics {template<class... A>void printf(A...) {}} graphics;
 enum {CV_INPUT_MAP=1};
 std::variant<std::monostate,CVInputMap*> selected_input_map;
 bool IsEditingInputMap()const{return selected_input_map.index()!=0;}
 bool EditSelectedInputMap(int d){if(!IsEditingInputMap())return false;auto* m=std::get<CVInputMap*>(selected_input_map);m->attenuversion=constrain(m->attenuversion+d,-127,127);return true;}
 std::pair<int,CVInputMap&> IndexedInput(int i,CVInputMap& m){return {i,m};}
 template<class... P>bool CheckEditInputMapPress(int c,P... maps){
  if(IsEditingInputMap()){selected_input_map=std::monostate{};if(EditMode())CursorToggle();return !EditMode();}
  if(!EditMode())return false;
  return (...||(c==maps.first?(selected_input_map=&maps.second,true):false));
 }
 template<class... A>void PatchCable(A&...) {}
 void PatchCable(AudioStream&,int,AudioStream&,int){}
 void AllowRestart(){}
 template<class... A>void gfxStartCursor(A...) {}
 template<class... A>void gfxEndCursor(A...) {}
 template<class... A>void gfxPrintDb(A...) {}
 template<class... A>void gfxPrintIcon(A...) {}
 void gfxDisplayInputMapEditor(){}
};
