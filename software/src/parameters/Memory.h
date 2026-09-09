#pragma once
// Read-only metadata and infrequent setting-control code, NOT audio/ISR DSP.
// The pinned GCC/LTO needs used for inline/template constants to retain their
// explicit section. noinline prevents control helpers from growing ITCM callers.
// These attributes do not prove IRQ latency; final ELF and bench gates remain.
#if defined(__IMXRT1062__)
#define PHZ_PARAMETER_STORAGE __attribute__((section(".progmem.parameters"), used))
#define PHZ_PARAMETER_CODE __attribute__((section(".flashmem.parameters"), noinline, used))
#else
#define PHZ_PARAMETER_STORAGE
#define PHZ_PARAMETER_CODE
#endif
