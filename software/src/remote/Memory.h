#pragma once
// Match the existing parameter/UI placement policy: LTO must not inline these
// non-real-time handlers back into ITCM callers or discard audit symbols.
#if defined(__IMXRT1062__)
#define PHZ_REMOTE_CODE __attribute__((section(".flashmem.remote"), noinline, used))
#else
#define PHZ_REMOTE_CODE
#endif
