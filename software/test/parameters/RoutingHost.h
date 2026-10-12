#pragma once
#include "VcaHost.h"
#include "Audio/AudioParam.h"
// Hash and applet_id body mirror production; the driver checks them.
constexpr uint64_t strhash(const char* str) {
  uint64_t id = 0;
  for (const char* c = str; *c != '\0'; c++) {
    id = (id << 7) | (id >> (64 - 7));
    id ^= (*c);
  }
  return id;
}
struct RoutingHostBase:HemisphereAudioApplet {
 virtual const char* applet_name()=0;
 virtual void Start(){}
 virtual void Unload(){}
 virtual void Controller(){}
 virtual void View(){}
 virtual void OnEncoderMove(int){}
 virtual uint64_t OnDataRequest(){return 0;}
 virtual void OnDataReceive(uint64_t){}
 virtual const uint64_t applet_id() {
    return strhash(applet_name());
 }
 struct Cable {AudioStream* from;int from_ch;AudioStream* to;int to_ch;};
 std::array<Cable,32> patch{};size_t patch_count=0;
 void PatchCable(AudioStream& from,int f,AudioStream& to,int t){
  assert(patch_count<patch.size());patch[patch_count++]={&from,f,&to,t};
 }
};
template<int Channels>using RoutingPassthrough=AudioPassthrough<AudioChannels(Channels)>;
#define private public
#include "Audio/AudioVCA.h"
#include "Audio/InterpolatingStream.h"
#include "Audio/AudioMixer.h"
#define AudioPassthrough RoutingPassthrough
#define HemisphereAudioApplet RoutingHostBase
#include "audio_applets/CrosspanApplet.h"
#include "audio_applets/MidSideApplet.h"
#undef HemisphereAudioApplet
#undef AudioPassthrough
#undef private
