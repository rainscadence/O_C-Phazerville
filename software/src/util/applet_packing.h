#pragma once
#include <stddef.h>
#include <stdint.h>
// Existing unmasked legacy helper, extracted unchanged so host tests execute
// production packing. Callers must provide values that fit their fields.
struct PackLocation {
    size_t location;
    size_t size;
};

/* Add value to a 64-bit storage unit at the specified location */
constexpr void Pack(uint64_t &data, const PackLocation p, const uint64_t value) {
    data |= (value << p.location);
}

/* Retrieve value from a 64-bit storage unit at the specified location and of the specified bit size */
constexpr int Unpack(const uint64_t &data, const PackLocation p) {
    uint64_t mask = 1;
    for (size_t i = 1; i < p.size; i++) mask |= (0x01 << i);
    return (data >> p.location) & mask;
}
