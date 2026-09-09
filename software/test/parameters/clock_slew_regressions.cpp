#include "AppletHost.h"
#include "applets/ClockDivider.h"
#include "applets/Slew.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main(int argc,char**argv) {
  assert(argc==2);
  if(!strcmp(argv[1],"clock")) {
    ClockDivider a{};a.Start();a.editing=true;a.OnEncoderMove(-65);
    a.Controller();assert(a.divmult[0].steps==-64);
    const uint64_t blob=a.OnDataRequest();ClockDivider b{};b.Start();b.OnDataReceive(blob);b.Controller();
    printf("ClockDivider saved=%016llx restored=%d,%d,%d,%d\n",(unsigned long long)blob,b.divmult[0].steps,b.divmult[1].steps,b.divmult[2].steps,b.divmult[3].steps);fflush(stdout);
    assert(b.divmult[0].steps==-64&&b.divmult[1].steps==1&&b.divmult[2].steps==2&&b.divmult[3].steps==1);
  } else {
    Slew a{};a.Start();a.OnDataReceive(25|(25ull<<8));a.inputs[1]=2;a.Controller();
    assert(a.outputs[1]==2);puts("Slew channel1 one-tick remainder reaches target safely");
  }
}
