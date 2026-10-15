/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#include "intel_wakeref.h"

#define intel_runtime_pm xe_runtime_pm

static inline void disable_rpm_wakeref_asserts(void *rpm)
{
}

static inline void enable_rpm_wakeref_asserts(void *rpm)
{
}
