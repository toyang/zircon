// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS

#if defined(__x86_64__)

// Value for ZX_THREAD_STATE_GENERAL_REGS on x86-64 platforms.
typedef struct zx_thread_state_general_regs {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
} zx_thread_state_general_regs_t;

// Backwards-compatible definition.
// TODO(ZX-1648) remove when callers are updated.
typedef struct zx_thread_state_general_regs zx_x86_64_general_regs_t;

#elif defined(__aarch64__)

// Value for ZX_THREAD_STATE_GENERAL_REGS on ARM64 platforms.
typedef struct zx_thread_state_general_regs {
    uint64_t r[30];
    uint64_t lr;
    uint64_t sp;
    uint64_t pc;
    uint64_t cpsr;
} zx_thread_state_general_regs_t;

// Backwards-compatible definition.
// TODO(ZX-1648) remove when callers are updated.
typedef struct zx_thread_state_general_regs zx_arm64_general_regs_t;

#endif

// Possible values for "kind" in zx_thread_read_state and zx_thread_write_state.
#define ZX_THREAD_STATE_GENERAL_REGS 0u

// Backwards-compatible enum for general registers.
// TODO(ZX-1648) remove when callers are updated.
#define ZX_THREAD_STATE_REGSET0 0

__END_CDECLS
