#ifdef ARDUINO_TEENSY41

#include "AudioIO.h"
#include "AudioStream.h"
#include "Audio/AudioMixer.h"
#include "Audio/AudioOwnerRoute.h"
#include "PhzConfig.h"
#include "usb_desc.h"

FLASHMEM void AudioOwnerRoute::update() {
  audio_block_t *inputs[4]{};
  for (unsigned i = 0; i < 4; ++i) inputs[i] = receiveReadOnly(i);

  audio_block_t *outputs[2]{};
  for (unsigned channel = 0; channel < 2; ++channel)
    if (inputs[channel] || inputs[2 + channel]) outputs[channel] = allocate();

  for (unsigned sample = 0; sample < AUDIO_BLOCK_SAMPLES; ++sample) {
    const float owner_gain = NextOwnerGain();
    const bool tx = current_owner_ == Owner::Tx;
    for (unsigned channel = 0; channel < 2; ++channel) {
      const float voice_gain = tx ? NextTxGain(channel) : 1.0f;
      if (!outputs[channel]) continue;
      const audio_block_t *input = inputs[(tx ? 2 : 0) + channel];
      const int32_t scaled = input ? int32_t(float(input->data[sample]) *
                                              owner_gain * voice_gain) : 0;
      outputs[channel]->data[sample] = int16_t(
          scaled < -32768 ? -32768 : scaled > 32767 ? 32767 : scaled);
    }
  }

  for (unsigned channel = 0; channel < 2; ++channel) {
    if (!outputs[channel]) continue;
    transmit(outputs[channel], channel);
    release(outputs[channel]);
  }
  for (auto *block : inputs) if (block) release(block);
}

namespace OC {
  namespace AudioIO {
    AudioInputI2S2 input_stream;

    AudioOutputI2S2* output_stream = nullptr;
    DMAMEM AudioOwnerRoute output_route;
    DMAMEM AudioSynthWaveform tx_wave[2];
    DMAMEM AudioSynthNoiseWhite tx_noise[2];
    DMAMEM AudioMixer<2> tx_mix[2];
    DMAMEM AudioConnection tx_wave_conn[2] = {
      {tx_wave[0], 0, tx_mix[0], 0}, {tx_wave[1], 0, tx_mix[1], 0}
    };
    DMAMEM AudioConnection tx_noise_conn[2] = {
      {tx_noise[0], 0, tx_mix[0], 1}, {tx_noise[1], 0, tx_mix[1], 1}
    };
    DMAMEM AudioConnection tx_out_conn[2] = {
      {tx_mix[0], 0, output_route, 2}, {tx_mix[1], 0, output_route, 3}
    };
#ifdef AUDIO_INTERFACE
    DMAMEM AudioInputUSB input_usb;
    AudioMixer<2> usbmix[2];
    DMAMEM AudioMixer<1> usbmirror[2];
    DMAMEM AudioOutputUSB output_usb;
    DMAMEM AudioConnection out_conn_usbL{output_route, 0, usbmirror[0], 0};
    DMAMEM AudioConnection out_conn_usbR{output_route, 1, usbmirror[1], 0};
    DMAMEM AudioConnection out_conn_usbLm{usbmirror[0], 0, output_usb, 0};
    DMAMEM AudioConnection out_conn_usbRm{usbmirror[1], 0, output_usb, 1};
    AudioConnection out_conn_usbL2{input_stream, 0, output_usb, 2};
    AudioConnection out_conn_usbR2{input_stream, 1, output_usb, 3};

    AudioConnection in_conn_usbL{input_usb, 2, usbmix[0], 0};
    AudioConnection in_conn_usbR{input_usb, 3, usbmix[1], 0};
    AudioConnection in_conn_mixL{output_route, 0, usbmix[0], 1};
    AudioConnection in_conn_mixR{output_route, 1, usbmix[1], 1};
#endif
    bool usb_audio_mirror = true;

    AudioConnection out_conn[2];

    AudioStream& InputStream(int interface) {
      switch (interface) {
        default:
        case 0:
          return input_stream;
#ifdef AUDIO_INTERFACE
        case 1:
          return input_usb;
#endif
      }
    }

    FLASHMEM AudioStream& OutputStream() {
      // The output stream should be created after all other streams have been
      // created or we get an extra 3ms of latency. Hence, we initialize it
      // lazily. This is pretty hacky; if someone references this before all
      // other streams have been created it won't work. But it was the simplest
      // fix for now.
      if (output_stream == nullptr) {
        output_stream = new AudioOutputI2S2();
#ifdef AUDIO_INTERFACE
        out_conn[0].connect(usbmix[0], 0, *output_stream, 0);
        out_conn[1].connect(usbmix[1], 0, *output_stream, 1);
        usbmix[0].gain(0, 1.0f);
        usbmix[0].gain(1, 1.0f);
        usbmix[1].gain(0, 1.0f);
        usbmix[1].gain(1, 1.0f);
#else
        out_conn[0].connect(output_route, 0, *output_stream, 0);
        out_conn[1].connect(output_route, 1, *output_stream, 1);
#endif
      }
      return output_route;
    }

    FLASHMEM void AcquireTxAudio() {
      (void)OutputStream(); // TX may be the first audio-producing app after boot.
      output_route.SilenceTxImmediately();
      output_route.RequestOwner(AudioOwnerRoute::Owner::Tx);
    }

    FLASHMEM void ReleaseTxAudio() {
      output_route.RequestOwner(AudioOwnerRoute::Owner::Main);
    }

    FLASHMEM void ConfigureTxVoice(unsigned voice, bool enabled, TxWaveform waveform,
                          float frequency, uint8_t level_percent,
                          bool reset_phase) {
      if (voice > 1) return;
      if (frequency < 0.1f || frequency > 20000.0f) return;
      if (level_percent > 100) level_percent = 100;
      const bool white = waveform == TxWaveform::White;
      tx_mix[voice].gain(0, white ? 0.0f : 1.0f);
      tx_mix[voice].gain(1, white ? 1.0f : 0.0f);
      tx_noise[voice].amplitude(1.0f);
      if (!white) {
        static constexpr short types[] = {
          WAVEFORM_SINE, WAVEFORM_TRIANGLE, WAVEFORM_BANDLIMIT_SAWTOOTH,
          WAVEFORM_BANDLIMIT_SQUARE, WAVEFORM_SAMPLE_HOLD
        };
        unsigned index = unsigned(waveform);
        if (index >= sizeof(types) / sizeof(types[0])) index = 0;
        tx_wave[voice].begin(1.0f, frequency, types[index]);
        tx_wave[voice].pulseWidth(0.5f);
        if (reset_phase) tx_wave[voice].phase(0.0f);
      }
      output_route.SetTxTarget(voice,
          enabled ? float(level_percent) * 0.01f : 0.0f);
    }

    FLASHMEM void SetTxVoiceLevel(unsigned voice, bool enabled,
                                  uint8_t level_percent) {
      if (voice > 1) return;
      if (level_percent > 100) level_percent = 100;
      output_route.SetTxTarget(voice,
          enabled ? float(level_percent) * 0.01f : 0.0f);
    }

    FLASHMEM void SetUsbAudioMirror(bool enabled) {
      usb_audio_mirror = enabled;
#ifdef AUDIO_INTERFACE
      usbmirror[0].gain(0, enabled ? 1.0f : 0.0f);
      usbmirror[1].gain(0, enabled ? 1.0f : 0.0f);
#endif
    }

    FLASHMEM bool UsbAudioMirror() { return usb_audio_mirror; }

    void Init() {
      AudioMemory(AUDIO_MEMORY);
      SetUsbAudioMirror(usb_audio_mirror);
    }
  }
}
#endif
