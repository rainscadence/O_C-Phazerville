#pragma once
#include "Schema.h"

namespace phz { namespace parameters {
// Frozen P2 wire codes. Native CVInputMap source bytes are deliberately distinct.
// Shared by participating applets, not a per-applet transport registry.
inline constexpr Option CvSourceOptions[] PHZ_PARAMETER_STORAGE = {
  {0, 0},
  {1, 64},
  {2, 65},
  {3, 66},
  {4, 67},
  {5, 68},
  {6, 69},
  {7, 70},
  {8, 71},
  {9, 96},
  {10, 97},
  {11, 98},
  {12, 99},
  {13, 100},
  {14, 101},
  {15, 102},
  {16, 103},
  {17, 104},
  {18, 105},
  {19, 106},
  {20, 107},
  {21, 108},
  {22, 109},
  {23, 110},
  {24, 111},
  {25, 128},
  {26, 129},
  {27, 130},
  {28, 131},
  {29, 132},
  {30, 133},
  {31, 134},
  {32, 135},
  {33, 136},
  {34, 137},
  {35, 138},
  {36, 139},
  {37, 140},
  {38, 141},
  {39, 142},
  {40, 143},
  {41, 144},
  {42, 145},
  {43, 146},
  {44, 147},
  {45, 148},
  {46, 149},
  {47, 150},
  {48, 151},
  {49, 152},
  {50, 153},
  {51, 154},
  {52, 155},
  {53, 156},
  {54, 157},
  {55, 158},
  {56, 159},
  {57, 224},
};

template<class App, auto Member> struct CvMapping {
  static const auto& Get(const Owner& owner) { return static_cast<const App&>(owner).*Member; }
  static auto& Get(Owner& owner) { return static_cast<App&>(owner).*Member; }
  static PHZ_PARAMETER_CODE int32_t ReadSource(const Owner& owner) { return Get(owner).Source(); }
  static PHZ_PARAMETER_CODE bool WriteSource(Owner& owner, int32_t value, uint64_t) {
    return value >= 0 && value <= 255 && Get(owner).SetSource(uint8_t(value));
  }
  static PHZ_PARAMETER_CODE int32_t ReadAtten(const Owner& owner) { return Get(owner).attenuversion; }
  static PHZ_PARAMETER_CODE bool WriteAtten(Owner& owner, int32_t value, uint64_t) {
    if(value < -127 || value > 127) return false;
    Get(owner).attenuversion = int8_t(value);
    return true;
  }
  static constexpr Entry Source(uint16_t id,const char* key,const char* label) {
    return {id,key,label,"source",
      {Kind::Enumeration,0,0,CvSourceOptions,sizeof(CvSourceOptions)/sizeof(Option)},
      ReadSource,WriteSource};
  }
  static constexpr Entry Attenuversion(uint16_t id,const char* key,const char* label) {
    return {id,key,label,"attenuversion",{Kind::Integer,-127,127},ReadAtten,WriteAtten};
  }
};
}}
