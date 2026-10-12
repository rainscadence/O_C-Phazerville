#pragma once

#include <Audio.h>

namespace OC {
  namespace AudioIO {
    // total block size including header is 260 bytes
    // 252 * 260 fits nicely into two 32KB pages
    const int AUDIO_MEMORY = 252;
    AudioStream& InputStream(int interface = 0);
    AudioStream& OutputStream();
    enum class TxWaveform : uint8_t {
      Sine, Triangle, Saw, Square, SampleHold, White, Count
    };
    void AcquireTxAudio();
    void ReleaseTxAudio();
    void ConfigureTxVoice(unsigned voice, bool enabled, TxWaveform waveform,
                          float frequency, uint8_t level_percent,
                          bool reset_phase = false);
    void SetTxVoiceLevel(unsigned voice, bool enabled, uint8_t level_percent);
    void SetUsbAudioMirror(bool enabled);
    bool UsbAudioMirror();
    void Init();
  }
}
