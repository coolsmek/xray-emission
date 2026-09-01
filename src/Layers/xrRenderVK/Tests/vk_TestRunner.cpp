#include "stdafx.h"
#ifdef VK_ENABLE_TESTS
#include "vk_TestRunner.h"
#include "vk_TestRunner_ForceLink.h"

vk_TestRegistry& vk_TestRegistry::Get()
{
    static vk_TestRegistry s_instance;
    return s_instance;
}

void vk_TestRegistry::Register(const char* suite, const char* name, std::function<bool()> fn)
{
    cases.push_back({suite, name, fn});
}

int vk_TestRegistry::RunAll()
{
    Msg("* [VK_TEST] Running %zu test(s)", cases.size());
    int failures = 0;
    for (auto& tc : cases)
    {
        bool passed = false;
        __try { passed = tc.fn(); }
        __except(EXCEPTION_EXECUTE_HANDLER) { passed = false; }

        Msg("%s [VK_TEST] %s::%s",
            passed ? "*" : "!", tc.suite, tc.name);
        if (!passed) ++failures;
    }
    Msg("* [VK_TEST] %d failure(s)", failures);
    R_ASSERT2(failures == 0, "VK regression tests FAILED — see log above");
    return failures;
}
#endif // VK_ENABLE_TESTS
