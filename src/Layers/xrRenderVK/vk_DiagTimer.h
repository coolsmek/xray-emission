#ifndef VK_DIAG_TIMER_H
#define VK_DIAG_TIMER_H
#pragma once

extern bool g_vkMtDiagEnabled;

struct vk_ScopedLockTimer
{
    u64* accum;
    LARGE_INTEGER t0;

    explicit vk_ScopedLockTimer(u64* a) : accum(a)
    {
        if (g_vkMtDiagEnabled && accum)
        {
            QueryPerformanceCounter(&t0);
        }
    }

    ~vk_ScopedLockTimer()
    {
        if (g_vkMtDiagEnabled && accum)
        {
            LARGE_INTEGER t1;
            QueryPerformanceCounter(&t1);
            *accum += (u64)(t1.QuadPart - t0.QuadPart);
        }
    }
};

#endif // VK_DIAG_TIMER_H
