#pragma once

#include <Audio.h>
#include <stdint.h>
#include <string.h>

// Two stereo producers share the main outputs. Inputs 0/1 are the normal
// Quadrants path; inputs 2/3 are the TXI2C2MIDI voices. Ownership changes are
// deliberately fade-out/switch/fade-in so neither producer can leak stale
// audio into the other application's lifetime.
class AudioOwnerRoute : public AudioStream {
 public:
  enum class Owner : uint8_t { Main = 0, Tx = 1 };
  static constexpr uint16_t kFadeSamples = 240; // 5 ms at 48 kHz.

  AudioOwnerRoute() : AudioStream(4, input_queue_) {}

  void RequestOwner(Owner owner) {
    AudioNoInterrupts();
    requested_owner_ = owner;
    if (requested_owner_ != current_owner_ && phase_ == Phase::Steady) {
      phase_ = Phase::FadeOut;
      owner_ramp_ = kFadeSamples;
    }
    AudioInterrupts();
  }

  Owner owner() const { return current_owner_; }

  void SetTxTarget(unsigned channel, float gain, bool immediate = false) {
    if (channel > 1) return;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    AudioNoInterrupts();
    tx_target_[channel] = gain;
    if (immediate) {
      tx_gain_[channel] = gain;
      tx_ramp_[channel] = 0;
    } else {
      tx_ramp_[channel] = kFadeSamples;
    }
    AudioInterrupts();
  }

  void SilenceTxImmediately() {
    AudioNoInterrupts();
    for (unsigned channel = 0; channel < 2; ++channel) {
      tx_gain_[channel] = tx_target_[channel] = 0.0f;
      tx_ramp_[channel] = 0;
    }
    AudioInterrupts();
  }

  void update() override;

 private:
  enum class Phase : uint8_t { Steady, FadeOut, FadeIn };

  float NextOwnerGain() {
    if (phase_ == Phase::Steady) return 1.0f;
    if (!owner_ramp_) {
      if (phase_ == Phase::FadeOut) {
        current_owner_ = requested_owner_;
        phase_ = Phase::FadeIn;
        owner_ramp_ = kFadeSamples;
        return 0.0f;
      }
      if (current_owner_ != requested_owner_) {
        phase_ = Phase::FadeOut;
        owner_ramp_ = kFadeSamples;
        return 1.0f;
      }
      phase_ = Phase::Steady;
      return 1.0f;
    }
    const float fraction = float(owner_ramp_) / float(kFadeSamples);
    --owner_ramp_;
    return phase_ == Phase::FadeOut ? fraction : 1.0f - fraction;
  }

  float NextTxGain(unsigned channel) {
    if (!tx_ramp_[channel]) return tx_gain_[channel];
    tx_gain_[channel] += (tx_target_[channel] - tx_gain_[channel]) /
                         float(tx_ramp_[channel]);
    --tx_ramp_[channel];
    return tx_gain_[channel];
  }

  audio_block_t *input_queue_[4];
  volatile Owner current_owner_ = Owner::Main;
  volatile Owner requested_owner_ = Owner::Main;
  volatile Phase phase_ = Phase::Steady;
  volatile uint16_t owner_ramp_ = 0;
  volatile float tx_gain_[2] = {0.0f, 0.0f};
  volatile float tx_target_[2] = {0.0f, 0.0f};
  volatile uint16_t tx_ramp_[2] = {0, 0};
};
