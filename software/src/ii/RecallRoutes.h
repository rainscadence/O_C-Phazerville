#pragma once
#include "AppletRecall.h"
#include "DirectionalMappings.h"

namespace phz { namespace ii {
// Stable shared CV fields 1/2, trigger fields 3/4; private fields 0x100..0x10f. Only source
// selection is touched, never CV attenuation or MIDI/physical output state.
template<class Input, class Visit>
void VisitRecallInputs(Input input, Visit visit) {
  for(uint16_t field=1;field<=4;++field) if(auto* map=input(field)) visit(field,*map);
  for(uint16_t field=0x100;field<0x110;++field) if(auto* map=input(field)) visit(field,*map);
}
template<size_t Fields, class Input>
bool CaptureRecall(Input input, const DirectionalMappings& routes,
                   uint16_t first_producer, RecallProfile<Fields>& out) {
  RecallProfile<Fields> candidate;
  bool fits=true;
  auto append=[&](uint16_t field, RecallKind kind, uint8_t channel) {
    if(candidate.count==Fields){fits=false;return;}
    candidate.bindings[candidate.count++]={field,kind,channel};
  };
  VisitRecallInputs(input,[&](uint16_t field,const auto& map) {
    auto source=map.Source();
    append(field,RecallKind::Input,(source&0xe0)==0xc0 ? (source&31)+1 : 0);
  });
  for(uint16_t ch=0;ch<2;++ch)
    append(ch+1,RecallKind::Output,routes.OutputMappingFor(first_producer+ch));
  if(!fits || !candidate.Valid())return false;
  out=candidate;return true;
}
// Caller owns DSP/lifecycle protection. Incompatible directions or occupied
// outputs remain unmapped, never stolen. Return false for a visible conflict.
template<size_t Fields, class Input>
bool ApplyRecall(const RecallProfile<Fields>& profile, Input input,
                 DirectionalMappings& routes, uint16_t first_producer,
                 uint16_t output_count, size_t (*input_users)(size_t) = nullptr) {
  if(!profile.Valid() || !first_producer || first_producer+1>output_count)return false;
  VisitRecallInputs(input,[](uint16_t,auto& map){if((map.Source()&0xe0)==0xc0)map.SetSource(0);});
  routes.RouteOutput(first_producer,0,output_count);
  routes.RouteOutput(first_producer+1,0,output_count);
  bool applied=true;
  for(size_t i=0;i<profile.count;++i){
    const auto& b=profile.bindings[i];
    if((b.kind==RecallKind::Input && !input(b.field)) ||
       (b.kind==RecallKind::Output && (b.field<1 || b.field>2))){applied=false;continue;}
    // An unused slot may adopt the remembered assignment's direction. Never
    // change a direction while another input binding or producer uses it.
    if(b.channel){
      const auto direction=b.kind==RecallKind::Input ? DirectionalMappings::Direction::Input : DirectionalMappings::Direction::Output;
      const auto target=routes.Get(b.channel-1);
      if(target.direction!=direction){
        if(!input_users || target.producer || input_users(b.channel-1) ||
           !routes.SetDirection(b.channel-1,direction,0)){applied=false;continue;}
        if(direction==DirectionalMappings::Direction::Input && target.range==VirtualInputs::Range::Off)
          routes.SetInputRange(b.channel-1,VirtualInputs::Range::Unipolar5);
      }
    }
    if(b.kind==RecallKind::Input){
      auto* map=input(b.field);
      if(!map){applied=false;continue;}
      if(b.channel && (routes.Get(b.channel-1).direction!=DirectionalMappings::Direction::Input ||
          !map->SetSource(uint8_t(0xc0|(b.channel-1)))))applied=false;
    }else{
      if(b.field<1 || b.field>2 || !routes.RouteOutput(first_producer+b.field-1,b.channel,output_count))applied=false;
    }
  }
  return applied;
}
}}
