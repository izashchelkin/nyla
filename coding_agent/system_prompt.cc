// ─── System prompt builder ─────────────────────────────────────────────────
// Builds the system prompt from a user-provided file or embedded default,
// augmented with tool descriptions and project context.

#include "coding_agent/system_prompt.h"

#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/subprocess.h"

#include "coding_agent/tool_registry.h"

namespace nyla
{

// ─── Embedded default system prompt ─────────────────────────────────────────
// Used when ~/.config/coding_agent/prompt.md is not found.

static const char kDefaultPrompt[] =
    "You are an expert coding assistant operating inside a native C++23 coding agent harness. "
    "You help users by reading files, executing commands, editing code, and writing new files.\n\n"
    "Guidelines:\n"
    "- Use bash for file operations like ls, grep, find\n"
    "- Use xcav_read or read_file to examine file contents\n"
    "- Use xcav_edit or write_file for all file edits. Prefer xcav_edit for targeted replacements.\n"
    "- Use xcav_blocks before structural operations (move, delete) to survey file structure\n"
    "- Use xcav_move for block-level code moves instead of manual cut-and-paste\n"
    "- Use xcav_move_into for cross-file block moves\n"
    "- Use xcav_delete for block-level deletions\n"
    "- Use xcav_replace for replacing entire functions/structs/classes\n"
    "- Use xcav_undo immediately if an xcav operation produces unexpected results\n"
    "- Be concise in your responses\n"
    "- Show file paths clearly when working with files\n";

// ─── Path to user prompt file ───────────────────────────────────────────────

static const char kPromptFilePath[] = "/.config/coding_agent/prompt.md";

// ─── Helpers ────────────────────────────────────────────────────────────────

static auto AllocStr(region_alloc &alloc, byteview src) -> byteview
{
    if (src.size == 0)
        return {};
    span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(alloc, src.size);
    MemCpy(dst.data, src.data, src.size);
    return {dst.data, src.size};
}

// ─── Load user prompt file ──────────────────────────────────────────────────

static auto TryLoadUserPrompt(region_alloc &alloc) -> byteview
{
    // Build path: ~/.config/coding_agent/prompt.md
    byteview homePath{};
    if (!TryReadEnvVar("HOME"_s, homePath) && !TryReadEnvVar("USERPROFILE"_s, homePath))
        return {};

    uint32_t pathLen = homePath.size + (uint32_t)(sizeof(kPromptFilePath) - 1);
    span<uint8_t> pathBuf = RegionAlloc::AllocArray<uint8_t>(alloc, pathLen);
    MemCpy(pathBuf.data, homePath.data, homePath.size);
    MemCpy(pathBuf.data + homePath.size, kPromptFilePath, sizeof(kPromptFilePath) - 1);

    byteview fullPath{pathBuf.data, pathLen};
    if (!FileExists(fullPath))
        return {};

    file_handle fh = FileOpen(fullPath, FileOpenMode::Read);
    if (!FileValid(fh))
        return {};

    byteview content = FileReadFully(alloc, fh);
    FileClose(fh);
    return content;
}

// ─── File tree summary ──────────────────────────────────────────────────────

static auto BuildFileTreeSummary(region_alloc &alloc, byteview cwd) -> byteview
{
    // Run: find . -maxdepth 3 -type f \( -name '*.cc' -o -name '*.h' -o -name '*.hlsl' -o -name 'CMakeLists*.txt' \) |
    // sort | head -60
    const char *findArgs[] = {"find", ".",     "-maxdepth",
                              "3",    "-type", "f",
                              "(",    "-name", "*.cc",
                              "-o",   "-name", "*.h",
                              "-o",   "-name", "*.hlsl",
                              "-o",   "-name", "CMakeLists.txt",
                              "-o",   "-name", "CMakeListsGenerated.txt",
                              ")",    nullptr};
    // Sort + head via shell pipe
    const char *shellArgs[] = {
        "bash", "-c",
        "find . -maxdepth 3 -type f \\( -name '*.cc' -o -name '*.h' -o -name '*.hlsl' -o -name 'CMakeLists*.txt' \\) "
        "| sort | head -60",
        nullptr};

    subprocess_result result = SubprocessRun(span<const char *const>{shellArgs, 3}, alloc, {}, 5000);
    if (result.exit_code != 0 || result.stdout_data.size == 0)
        return {};

    return result.stdout_data;
}

// ─── Serialize tool descriptions ────────────────────────────────────────────
static auto SerializeToolDescriptions(region_alloc &alloc) -> byteview
{
    span<const ToolDef> tools = GetToolDefs();
    if (tools.size == 0)
        return {};

    // Parameter JSON schemas can be large (~300 chars each); allocate enough
    uint64_t estSize = tools.size * 512 + 256;
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, estSize);
    uint64_t pos = 0;

    pos += StringWriteFmt(Span::SubSpan(buf, pos), "Available tools:\n\n"_s);

    for (uint64_t i = 0; i < tools.size; ++i)
    {
        if (pos + 512 > buf.size)
            break;
        ToolDef const &t = tools.data[i];
        uint32_t descLen = 0;
        while (t.description[descLen])
            ++descLen;
        pos += StringWriteFmt(Span::SubSpan(buf, pos), "- %s: %.*s\n  Parameters: %s\n\n"_s, t.name, descLen,
                              t.description, t.parametersJson);
    }

    return {buf.data, pos};
}

// ─── Public API ─────────────────────────────────────────────────────────────
auto BuildSystemPrompt(region_alloc &alloc) -> byteview
{
    // Estimate total size — tools descriptions can be large (~15 KB total)
    const uint64_t kEstimateSize = 256 * 1024; // 256 KB
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, kEstimateSize);
    uint64_t pos = 0;

    auto safeFmt = [&](auto fmt, auto... args) {
        if (pos >= buf.size)
            return;
        pos += StringWriteFmt(Span::SubSpan(buf, pos), fmt, args...);
    };

    // 1. Try user prompt, fall back to default
    byteview userPrompt = TryLoadUserPrompt(alloc);
    if (userPrompt.size > 0)
    {
        if (pos + userPrompt.size <= buf.size)
        {
            MemCpy(buf.data + pos, userPrompt.data, userPrompt.size);
            pos += userPrompt.size;
        }
    }
    else
    {
        uint32_t defaultLen = (uint32_t)(sizeof(kDefaultPrompt) - 1);
        if (pos + defaultLen <= buf.size)
        {
            MemCpy(buf.data + pos, kDefaultPrompt, defaultLen);
            pos += defaultLen;
        }
    }

    // 2. Project context
    byteview cwd = GetCurrentDirectory(alloc);
    safeFmt("\nProject directory: %.*s\n\n"_s, SV_ARG(cwd));

    // File tree summary
    byteview treeSummary = BuildFileTreeSummary(alloc, cwd);
    if (treeSummary.size > 0 && pos + treeSummary.size + 64 < buf.size)
    {
        MemCpy(buf.data + pos, treeSummary.data, treeSummary.size);
        pos += treeSummary.size;
        safeFmt("\n"_s);
    }

    // 3. Tool descriptions
    byteview toolDescs = SerializeToolDescriptions(alloc);
    if (toolDescs.size > 0 && pos + toolDescs.size < buf.size)
    {
        MemCpy(buf.data + pos, toolDescs.data, toolDescs.size);
        pos += toolDescs.size;
    }

    return {buf.data, pos};
}

} // namespace nyla
