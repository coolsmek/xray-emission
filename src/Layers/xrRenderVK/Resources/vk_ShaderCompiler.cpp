#include "stdafx.h"
#include "vk_Shader.h"
#include "../xrRender/xrD3DDefs.h"
#include <shaderc/shaderc.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cctype>

// ─────────────────────────────────────────────────────────────────────────────
// vk_CreateShaderModule
// Wraps vkCreateShaderModule.  pCode must be a 4-byte-aligned SPIR-V blob.
// ─────────────────────────────────────────────────────────────────────────────
VkShaderModule vk_CreateShaderModule(VkDevice device, const void* pCode, size_t sizeInBytes)
{
    if (device == VK_NULL_HANDLE || !pCode || sizeInBytes < 4)
        return VK_NULL_HANDLE;

    // Verify SPIR-V magic (0x07230203 little-endian)
    const uint32_t SPIRV_MAGIC = 0x07230203u;
    if (*reinterpret_cast<const uint32_t*>(pCode) != SPIRV_MAGIC)
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = sizeInBytes;
    ci.pCode    = reinterpret_cast<const uint32_t*>(pCode);

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult res = vkCreateShaderModule(device, &ci, nullptr, &module);
    if (res != VK_SUCCESS)
    {
        Msg("! vk_CreateShaderModule: vkCreateShaderModule failed (VkResult=%d)", (int)res);
        return VK_NULL_HANDLE;
    }
    return module;
}

// ─────────────────────────────────────────────────────────────────────────────
// vk_DestroyShaderModule
// Safe to call with VK_NULL_HANDLE.
// ─────────────────────────────────────────────────────────────────────────────
void vk_DestroyShaderModule(VkDevice device, VkShaderModule module)
{
    if (device != VK_NULL_HANDLE && module != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, module, nullptr);
}

// =============================================================================
// vk_CompileHlslToSpirv — shaderc runtime HLSL → SPIR-V
// =============================================================================

// ─── FNV-1a 64-bit hash ───────────────────────────────────────────────────────
static uint64_t fnv1a64(const void* data, size_t len, uint64_t h = 14695981039346656037ULL)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

// ─── Map "vs"/"ps"/… to shaderc_shader_kind ───────────────────────────────────
static shaderc_shader_kind stage_to_kind(const char* stage)
{
    if (!stage || !stage[0] || !stage[1]) return shaderc_glsl_infer_from_source;
    const char s0 = (char)tolower((unsigned char)stage[0]);
    const char s1 = (char)tolower((unsigned char)stage[1]);
    if (s0 == 'v' && s1 == 's') return shaderc_vertex_shader;
    if (s0 == 'p' && s1 == 's') return shaderc_fragment_shader;
    if (s0 == 'g' && s1 == 's') return shaderc_geometry_shader;
    if (s0 == 'h' && s1 == 's') return shaderc_tess_control_shader;
    if (s0 == 'd' && s1 == 's') return shaderc_tess_evaluation_shader;
    if (s0 == 'c' && s1 == 's') return shaderc_compute_shader;
    return shaderc_glsl_infer_from_source;
}

// ─── shaderc include callbacks ────────────────────────────────────────────────
// Resolves #include "file.h" via X-Ray FS under $game_shaders$.
struct VkIncludePayload { char* srcName; char* content; };

static shaderc_include_result* vk_include_resolve(
    void* /*user_data*/,
    const char* requested_source,
    int  /*include_type*/,
    const char* /*requesting_source*/,
    size_t /*include_depth*/)
{
    auto* ir = static_cast<shaderc_include_result*>(malloc(sizeof(shaderc_include_result)));
    memset(ir, 0, sizeof(*ir));

    // Convert forward slashes back to backslashes so X-Ray VFS can find it
    string_path fs_request;
    xr_strcpy(fs_request, requested_source);
    for (char* p = fs_request; *p; ++p)
    {
        if (*p == '/') *p = '\\';
    }

    // Try shader sub-directory first, then bare name
    string_path fullPath;
    strconcat(sizeof(fullPath), fullPath, ::Render->getShaderPath(), fs_request);
    IReader* R = FS.r_open("$game_shaders$", fullPath);
    if (!R)
    {
        R = FS.r_open("$game_shaders$", fs_request);
        if (R) xr_strcpy(fullPath, fs_request);
    }

    if (!R)
    {
        // Return a dummy error content so shaderc reports the missing include
        static const char kErr[] = "// [xrRenderVK] include not found\n";
        ir->source_name        = "";
        ir->source_name_length = 0;
        ir->content            = kErr;
        ir->content_length     = sizeof(kErr) - 1;
        ir->user_data          = nullptr;
        return ir;
    }

    size_t len     = R->length();
    char*  content = static_cast<char*>(malloc(len + 1));
    memcpy(content, R->pointer(), len);
    content[len]   = '\0';
    FS.r_close(R);

    // Normalize backslashes to forward slashes in the file path for shaderc
    for (char* p = fullPath; *p; ++p)
    {
        if (*p == '\\') *p = '/';
    }

    // Sanitize #include paths inside the content itself (replace \ with /)
    // This fixes the "Invalid escape sequence" error in shaderc
    char* pSearch = content;
    while ((pSearch = strstr(pSearch, "#include")) != nullptr)
    {
        pSearch += 8;
        while (*pSearch == ' ' || *pSearch == '\t') pSearch++;
        if (*pSearch == '"' || *pSearch == '<')
        {
            char endChar = (*pSearch == '"') ? '"' : '>';
            char* q = pSearch + 1;
            while (*q && *q != endChar && *q != '\n')
            {
                if (*q == '\\') *q = '/';
                q++;
            }
        }
    }

    // Strip legacy DX9 FX technique keywords which crash shaderc (replace with spaces)
    char* pFx = content;
    while ((pFx = strstr(pFx, "FXVS;")) != nullptr) {
        memcpy(pFx, "     ", 5);
        pFx += 5;
    }
    pFx = content;
    while ((pFx = strstr(pFx, "FXPS;")) != nullptr) {
        memcpy(pFx, "     ", 5);
        pFx += 5;
    }

    size_t nameLen = strlen(fullPath);
    char*  srcName = static_cast<char*>(malloc(nameLen + 1));
    memcpy(srcName, fullPath, nameLen + 1);

    auto* payload     = static_cast<VkIncludePayload*>(malloc(sizeof(VkIncludePayload)));
    payload->srcName  = srcName;
    payload->content  = content;

    ir->source_name        = srcName;
    ir->source_name_length = nameLen;
    ir->content            = content;
    ir->content_length     = len;
    ir->user_data          = payload;
    return ir;
}

static void vk_include_release(void* /*user_data*/, shaderc_include_result* ir)
{
    if (!ir) return;
    if (ir->user_data)
    {
        auto* p = static_cast<VkIncludePayload*>(ir->user_data);
        free(p->srcName);
        free(p->content);
        free(p);
    }
    free(ir);
}

// ─── Simple .spv disk cache ───────────────────────────────────────────────────
static bool spirv_cache_load(const char* path, std::vector<uint32_t>& out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { (void)fclose(f); return false; }
    long sz = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) { (void)fclose(f); return false; }
    if (sz <= 0 || (sz % 4) != 0)   { (void)fclose(f); return false; }
    const size_t wordCount = static_cast<size_t>(sz) / sizeof(uint32_t);
    out.resize(wordCount);
    const bool ok = (fread(out.data(), sizeof(uint32_t), wordCount, f) == wordCount);
    (void)fclose(f);
    if (!ok) out.clear();
    return ok;
}

static void spirv_cache_save(const char* path, const uint32_t* words, size_t wordCount)
{
    FILE* f = fopen(path, "wb");
    if (!f) return;
    (void)fwrite(words, sizeof(uint32_t), wordCount, f);
    (void)fclose(f);
}

// ─── vk_CompileHlslToSpirv ───────────────────────────────────────────────────
std::vector<uint32_t> vk_CompileHlslToSpirv(
    const char* hlslSource,
    size_t      hlslLen,
    const char* stageName,
    const char* entryPoint,
    const char* shaderName,
    const D3D_SHADER_MACRO* macros)
{
    std::vector<uint32_t> result;
    if (!hlslSource || hlslLen == 0 || !stageName || !entryPoint || !shaderName)
        return result;

    // ── Build disk-cache path ─────────────────────────────────────────────────
    char cacheDir[MAX_PATH] = {};
    {
        FS_Path* p = FS.get_path("$app_data_root$");
        if (p && p->m_Path)
            _snprintf_s(cacheDir, sizeof(cacheDir), _TRUNCATE, "%sshadercache\\", p->m_Path);
        else
            _snprintf_s(cacheDir, sizeof(cacheDir), _TRUNCATE, "shadercache\\");
    }
    CreateDirectoryA(cacheDir, nullptr);   // silently succeeds if already present

    // Inject row_major packing to match X-Ray's DX11 /Zpr matrix layout!
    std::string patchedSource = "#pragma pack_matrix(row_major)\n";
    patchedSource.append(hlslSource, hlslLen);

    // Hash: HLSL bytes + stage + entry point so different variants are distinct
    uint64_t h = fnv1a64(patchedSource.c_str(), patchedSource.size());
    h = fnv1a64(stageName,  strlen(stageName),  h);
    h = fnv1a64(entryPoint, strlen(entryPoint), h);
    if (macros)
    {
        for (const D3D_SHADER_MACRO* m = macros; m->Name != nullptr; ++m)
        {
            h = fnv1a64(m->Name, strlen(m->Name), h);
            if (m->Definition)
                h = fnv1a64(m->Definition, strlen(m->Definition), h);
        }
    }

    char cachePath[MAX_PATH];
    _snprintf_s(cachePath, sizeof(cachePath), _TRUNCATE,
                "%s%016llx.spv", cacheDir, static_cast<unsigned long long>(h));

    // ── Cache hit ─────────────────────────────────────────────────────────────
    if (spirv_cache_load(cachePath, result))
    {
        Msg("* [VK Shader] Loaded cached SPIR-V '%s' (%zu words)", shaderName, result.size());
        return result;
    }

    // ── Compile via shaderc ───────────────────────────────────────────────────
    Msg("* [VK Shader] Compiling HLSL→SPIR-V: '%s' [%s!%s]", shaderName, stageName, entryPoint);

    // Reuse a single compiler instance across calls (shaderc_compiler_t is thread-safe)
    static shaderc_compiler_t s_compiler = nullptr;
    if (!s_compiler)
        s_compiler = shaderc_compiler_initialize();

    if (!s_compiler)
    {
        Msg("! [VK Shader] shaderc_compiler_initialize() failed — '%s' will render black", shaderName);
        return result;
    }

    shaderc_compile_options_t opts = shaderc_compile_options_initialize();
    if (macros)
    {
        for (const D3D_SHADER_MACRO* m = macros; m->Name != nullptr; ++m)
        {
            if (m->Definition)
                shaderc_compile_options_add_macro_definition(opts, m->Name, strlen(m->Name), m->Definition, strlen(m->Definition));
            else
                shaderc_compile_options_add_macro_definition(opts, m->Name, strlen(m->Name), "", 0);
        }
    }
    shaderc_compile_options_set_source_language   (opts, shaderc_source_language_hlsl);
    shaderc_compile_options_set_target_env        (opts, shaderc_target_env_vulkan,
                                                        shaderc_env_version_vulkan_1_3);
    shaderc_compile_options_set_target_spirv      (opts, shaderc_spirv_version_1_3);
    shaderc_compile_options_set_include_callbacks (opts,
        vk_include_resolve, vk_include_release, nullptr);

    // ── Binding-base shifts: map HLSL register types to non-overlapping descriptor bindings ──
    // VS CBVs (b)   : 0–13   (MaxCBuffers = 14)
    // PS CBVs (b)   : 14–27
    // PS Textures(t): 56–71  (mtMaxPixelShaderTextures = 16)
    // PS Samplers(s): 72–87
    // VS Textures(t): 88–91  (mtMaxVertexShaderTextures = 4)
    // VS Samplers(s): 92–95
    shaderc_compile_options_set_auto_bind_uniforms(opts, true);
    // VS stage
    shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_vertex_shader,
        shaderc_uniform_kind_buffer,  0);   // b registers → 0+
    shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_vertex_shader,
        shaderc_uniform_kind_texture, 88);  // t registers → 88+
    shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_vertex_shader,
        shaderc_uniform_kind_sampler, 92);  // s registers → 92+
    // PS stage
    shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_fragment_shader,
        shaderc_uniform_kind_buffer,  14);  // b registers → 14+
    shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_fragment_shader,
        shaderc_uniform_kind_texture, 56);  // t registers → 56+
    shaderc_compile_options_set_binding_base_for_stage(opts, shaderc_fragment_shader,
        shaderc_uniform_kind_sampler, 72);  // s registers → 72+

    // Do NOT treat warnings as errors — X-Ray legacy HLSL has many harmless warnings

    shaderc_compilation_result_t res = shaderc_compile_into_spv(
        s_compiler,
        patchedSource.c_str(), patchedSource.size(),
        stage_to_kind(stageName),
        shaderName,   // used as filename in error messages and include resolution
        entryPoint,
        opts);

    shaderc_compile_options_release(opts);

    const shaderc_compilation_status status =
        shaderc_result_get_compilation_status(res);

    if (status != shaderc_compilation_status_success)
    {
        Msg("! [VK Shader] Compile error in '%s':\n%s",
            shaderName, shaderc_result_get_error_message(res));
        shaderc_result_release(res);
        return result;   // empty → pipeline cache will skip draw calls using this shader
    }

    // Log any warnings even on success
    size_t warnCount = shaderc_result_get_num_warnings(res);
    if (warnCount > 0)
        Msg("~ [VK Shader] '%s' compiled with %zu warning(s)", shaderName, warnCount);

    const size_t    spirvBytes = shaderc_result_get_length(res);
    const uint32_t* spirvPtr   = reinterpret_cast<const uint32_t*>(shaderc_result_get_bytes(res));
    const size_t    wordCount  = spirvBytes / sizeof(uint32_t);
    result.assign(spirvPtr, spirvPtr + wordCount);

    shaderc_result_release(res);

    // ── Persist to disk cache ─────────────────────────────────────────────────
    spirv_cache_save(cachePath, result.data(), result.size());
    Msg("* [VK Shader] Cached '%s' → %s (%zu words)", shaderName, cachePath, result.size());

    return result;
}

