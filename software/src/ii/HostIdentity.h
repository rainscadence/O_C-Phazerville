#pragma once
#include <stddef.h>
#include <stdint.h>

namespace phz { namespace ii {
struct HostIdentity {
  uint64_t applets[4]{};
  int16_t bank=-1;
  int16_t preset=-1; // last successful LOAD, never pending selection or SAVE
};

class LoadedSetupIdentity {
 public:
  void Reset(){bank_=-1;preset_=-1;}
  void BankLoaded(uint8_t bank){if(bank_!=bank)preset_=-1;bank_=bank;}
  void PresetLoaded(uint8_t preset){if(bank_>=0 && preset<32)preset_=preset;}
  void CopyTo(HostIdentity& out)const{out.bank=bank_;out.preset=preset_;}
 private:
  int16_t bank_=-1,preset_=-1;
};

// Caller owns the IRQ lock. Publish in the main loop; Wire callbacks access
// fixed cached words only. FFFF is unavailable, not bank/preset zero.
class HostIdentityReply {
 public:
  static constexpr uint16_t kUnavailable=0xffff;
  static bool IsCommand(uint8_t command){return command>=9 && command<=11;}
  void Clear(){valid_=false;pending_=false;}
  void Publish(const HostIdentity& identity,uint32_t epoch,bool open){
    if(!open){Clear();return;}
    for(size_t i=0;i<4;++i)
      words_[i]=identity.applets[i]>0 && identity.applets[i]<kUnavailable ? uint16_t(identity.applets[i]) : kUnavailable;
    words_[4]=identity.bank>=0 && identity.bank<=255 ? uint16_t(identity.bank) : kUnavailable;
    words_[5]=identity.preset>=0 && identity.preset<32 && words_[4]!=kUnavailable ? uint16_t(identity.preset) : kUnavailable;
    epoch_=epoch;valid_=true;
  }
  bool Select(const uint8_t* frame,size_t count,uint32_t epoch,bool open){
    pending_=false;
    if(!frame || count!=3 || !IsCommand(frame[0]))return false;
    uint16_t arg=(uint16_t(frame[1])<<8)|frame[2];
    size_t index;
    if(frame[0]==9){if(arg<1 || arg>4)return false;index=arg-1;}
    else{if(arg)return false;index=frame[0]==10?4:5;}
    selected_=valid_ && open && epoch==epoch_ ? words_[index] : kUnavailable;
    selected_epoch_=epoch;pending_=true;return true;
  }
  void Take(uint8_t (&reply)[2],uint32_t epoch,bool open){
    const uint16_t word=pending_ && open && epoch==selected_epoch_ ? selected_ : kUnavailable;
    reply[0]=uint8_t(word>>8);reply[1]=uint8_t(word);pending_=false;
  }
 private:
  uint16_t words_[6]{},selected_=kUnavailable;
  uint32_t epoch_=0,selected_epoch_=0;
  bool valid_=false,pending_=false;
};
}}
