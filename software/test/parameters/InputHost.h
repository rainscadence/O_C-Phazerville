#pragma once
#include "VcaHost.h"
#include "Audio/AudioParam.h"
// Full hash function copied verbatim from HSUtils.h; the test driver verifies
// that this small host seam stays text-identical to production before compiling.
constexpr uint64_t strhash(const char* str) {
  uint64_t id = 0;
  for (const char* c = str; *c != '\0'; c++) {
    id = (id << 7) | (id >> (64 - 7));
    id ^= (*c);
  }
  return id;
}
namespace OC::AudioIO {
 inline AudioStream physical,usb;
 inline AudioStream& InputStream(int interface=0){return interface?usb:physical;}
}
struct AudioAnalyzePeak:AudioStream {bool available(){return false;}float read(){return 0;}};
struct InputHostBase:HemisphereAudioApplet {
 int hemisphere=0;
 virtual const uint64_t applet_id(){return 0;}
 virtual void Start(){}
 virtual void Unload(){}
 virtual void Controller(){}
 virtual void View(){}
 virtual void OnDataRequest(std::array<uint64_t,4>&){}
 virtual void OnDataReceive(const std::array<uint64_t,4>&){}
 virtual void OnEncoderMove(int){}
};
#define private public
#include "Audio/AudioVCA.h"
#include "Audio/InterpolatingStream.h"
#include "Audio/AudioMixer.h"
#define HemisphereAudioApplet InputHostBase
#include "audio_applets/InputApplet.h"
#undef HemisphereAudioApplet
#undef private
