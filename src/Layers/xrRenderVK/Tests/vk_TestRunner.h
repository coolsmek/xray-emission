#pragma once
#ifdef VK_ENABLE_TESTS

#include <functional>
#include <vector>
#include <string>

// Results piped through engine's existing Msg() surface.
// Tests declare themselves as global-scope static instances.
struct vk_TestCase
{
    const char* suite;
    const char* name;
    std::function<bool()> fn;
};

struct vk_TestRegistry
{
    static vk_TestRegistry& Get();
    void Register(const char* suite, const char* name, std::function<bool()> fn);
    int RunAll();   // returns count of failures; 0 = all passed
    std::vector<vk_TestCase> cases;
};

// Registration macro — use at file scope
#define VK_TEST(Suite, Name)                                             \
    static bool VkTest_##Suite##_##Name();                               \
    static struct _Reg_##Suite##_##Name {                                \
        _Reg_##Suite##_##Name() {                                        \
            vk_TestRegistry::Get().Register(#Suite, #Name,               \
                VkTest_##Suite##_##Name);                                 \
        }                                                                \
    } _reg_##Suite##_##Name;                                             \
    static bool VkTest_##Suite##_##Name()

// Assertion macro — routes to engine Msg() + R_ASSERT2
#define VK_EXPECT(expr)                                                  \
    do { if (!(expr)) {                                                  \
        Msg("! [VK_TEST FAIL] %s:%d — " #expr, __FILE__, __LINE__);     \
        return false;                                                    \
    } } while(0)

#define VK_EXPECT_VK(result)  VK_EXPECT((result) == VK_SUCCESS)

#endif // VK_ENABLE_TESTS

