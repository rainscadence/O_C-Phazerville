// VirtualInputs.h
// Production C++11 header for phz::ii::VirtualInputs
// Wire code is normalized (0..65535), not volts.
// CV units: 1536 per volt. 5V = 7680, 10V = 15360.
// Output is zero until first Write, including bipolar.
// Caller owns interrupt-state-preserving synchronization.

#ifndef PHZ_II_VIRTUAL_INPUTS_H
#define PHZ_II_VIRTUAL_INPUTS_H

#include <cstddef>
#include <cstdint>

namespace phz {
namespace ii {

class VirtualInputs {
public:
    static constexpr size_t kCount = 32;

    enum class Range : uint8_t {
        Off = 0,
        Unipolar5 = 1,
        Bipolar5 = 2,
        Unipolar10 = 3
    };

    struct Snapshot {
        Range range;
        uint16_t code;
        bool received;
        int32_t value;
    };

    VirtualInputs() {
        for (size_t i = 0; i < kCount; ++i) {
            channels_[i].range = Range::Unipolar5;
            channels_[i].code = 0;
            channels_[i].received = false;
            channels_[i].value = 0;
        }
    }

    bool Write(uint16_t one_based_channel, uint16_t code) {
        if (one_based_channel < 1 || one_based_channel > kCount) {
            return false;
        }
        size_t idx = static_cast<size_t>(one_based_channel) - 1;
        if (channels_[idx].range == Range::Off) {
            return false;
        }
        channels_[idx].code = code;
        channels_[idx].received = true;
        channels_[idx].value = ComputeValue(channels_[idx].range, code);
        return true;
    }

    // Teletype-friendly normalized position, with exact endpoints and midpoint.
    bool WriteNormalized(uint16_t channel, uint16_t position) {
        int32_t low, high;
        if (position > 16384 || !Bounds(channel, low, high)) return false;
        auto& state = channels_[channel - 1];
        state.value = low + (int64_t(high - low) * position + 8192) / 16384;
        state.code = (uint32_t(position) * 65535 + 8192) / 16384;
        state.received = true;
        return true;
    }

    // Signed Teletype CV units: 16384 = 10V. Clip to the selected input range.
    // Decode explicitly, avoiding implementation-defined unsigned-to-signed casts.
    bool WriteVoltage(uint16_t channel, uint16_t bits) {
        int32_t low, high;
        if (!Bounds(channel, low, high)) return false;
        const int32_t signed_code = bits < 32768 ? int32_t(bits) : int32_t(bits) - 65536;
        const int64_t product = int64_t(signed_code) * 15360;
        int32_t value = product < 0 ? -((-product + 8192) / 16384)
                                    : (product + 8192) / 16384;
        if (value < low) value = low;
        if (value > high) value = high;
        auto& state = channels_[channel - 1];
        state.value = value;
        state.code = (int64_t(value - low) * 65535 + (high - low) / 2) / (high - low);
        state.received = true;
        return true;
    }

    void ClearValues() {
        for (size_t i = 0; i < kCount; ++i) {
            channels_[i].code = 0;
            channels_[i].received = false;
            channels_[i].value = 0;
        }
    }

    void Reset() {
        for (size_t i = 0; i < kCount; ++i) {
            channels_[i].range = Range::Unipolar5;
            channels_[i].code = 0;
            channels_[i].received = false;
            channels_[i].value = 0;
        }
    }

    bool SetRange(size_t zero_based_index, Range range) {
        if (zero_based_index >= kCount) {
            return false;
        }
        uint8_t r = static_cast<uint8_t>(range);
        if (r > 3) {
            return false;
        }
        if (channels_[zero_based_index].range != range) {
            channels_[zero_based_index].range = range;
            channels_[zero_based_index].code = 0;
            channels_[zero_based_index].received = false;
            channels_[zero_based_index].value = 0;
        }
        return true;
    }

    Snapshot Get(size_t zero_based_index) const {
        if (zero_based_index >= kCount) {
            return {Range::Off, 0, false, 0};
        }
        const auto& channel = channels_[zero_based_index];
        return {channel.range, channel.code, channel.received, channel.value};
    }

    int32_t Value(size_t zero_based_index) const {
        if (zero_based_index >= kCount) {
            return 0;
        }
        return channels_[zero_based_index].value;
    }

    bool RestoreRange(size_t index, uint64_t record) {
        if (index >= kCount) {
            return false;
        }
        if (record > 3) {
            return false;
        }
        Range r = static_cast<Range>(static_cast<uint8_t>(record));
        if (channels_[index].range != r) {
            channels_[index].range = r;
            channels_[index].code = 0;
            channels_[index].received = false;
            channels_[index].value = 0;
        }
        return true;
    }

private:
    bool Bounds(uint16_t channel, int32_t& low, int32_t& high) const {
        if (channel < 1 || channel > kCount) return false;
        switch (channels_[channel - 1].range) {
            case Range::Unipolar5: low = 0; high = 7680; return true;
            case Range::Bipolar5: low = -7680; high = 7680; return true;
            case Range::Unipolar10: low = 0; high = 15360; return true;
            default: return false;
        }
    }

    struct ChannelState {
        Range range;
        uint16_t code;
        bool received;
        int32_t value;
    };

    ChannelState channels_[kCount];

    static int32_t ComputeValue(Range range, uint16_t code) {
        if (range == Range::Off) {
            return 0;
        }
        int64_t min_val = 0;
        int64_t max_val = 0;
        if (range == Range::Unipolar5) {
            min_val = 0;
            max_val = 7680;
        } else if (range == Range::Bipolar5) {
            min_val = -7680;
            max_val = 7680;
        } else if (range == Range::Unipolar10) {
            min_val = 0;
            max_val = 15360;
        }
        int64_t span = max_val - min_val;
        int64_t numerator = span * static_cast<int64_t>(code) + 32767;
        int64_t result = min_val + numerator / 65535;
        return static_cast<int32_t>(result);
    }
};

} // namespace ii
} // namespace phz

#endif // PHZ_II_VIRTUAL_INPUTS_H
