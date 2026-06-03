// ─── Agent loop implementation ───────────────────────────────────────────────

#include "coding_agent/agent_loop.h"
#include "coding_agent/system_prompt.h"
#include "coding_agent/terminal_colors.h"

#include "nyla/commons/file.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/json_parser.h"
#include "nyla/commons/json_value.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/time.h"
namespace nyla
{

// ─── Helpers ────────────────────────────────────────────────────────────────

static auto AllocString(region_alloc &alloc, byteview src) -> byteview
{
    if (src.size == 0)
        return {};
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, src.size);
    MemCpy(buf.data, src.data, src.size);
    return {buf.data, src.size};
}

// Read a single line from stdin. Returns empty byteview on EOF.
// Strips trailing \n if present. Partial line at EOF is returned as-is.
static auto ReadLine(region_alloc &alloc, file_handle stdinHandle) -> byteview
{
    uint8_t buf[4096];
    uint32_t total = 0;
    uint8_t *accum = nullptr;

    for (;;)
    {
        uint32_t n = FileRead(stdinHandle, sizeof(buf), buf);
        if (n == 0)
        {
            // EOF — return accumulated partial line (if any)
            if (total == 0)
                return {};
            span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(alloc, total);
            MemCpy(dst.data, accum, total);
            return {dst.data, total};
        }

        // Search for newline in this chunk
        uint32_t newlinePos = 0;
        bool found = false;
        for (uint32_t i = 0; i < n; ++i)
        {
            if (buf[i] == '\n')
            {
                newlinePos = i;
                found = true;
                break;
            }
        }

        if (found)
        {
            // We have a complete line. Allocate and copy accum + chunk up to \n.
            uint32_t lineLen = total + newlinePos;
            if (lineLen == 0)
                return {}; // empty line (just \n)

            span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(alloc, lineLen);
            if (accum)
                MemCpy(dst.data, accum, total);
            MemCpy(dst.data + total, buf, newlinePos);

            // If we read past the newline, we lose those bytes.
            // For a simple line-based REPL this is fine — stdin is unbuffered.
            // TODO: push back unread bytes if needed for streaming.
            return {dst.data, lineLen};
        }

        // No newline yet — accumulate and continue
        span<uint8_t> newAccum = RegionAlloc::AllocArray<uint8_t>(alloc, total + n);
        if (accum)
        {
            MemCpy(newAccum.data, accum, total);
        }
        MemCpy(newAccum.data + total, buf, n);
        accum = newAccum.data;
        total += n;
    }
}

// ─── Context management helpers ─────────────────────────────────────────────
static auto EstimateMessageTokens(Message const &msg) -> uint32_t
{
    uint64_t bytes = 32;
    bytes += msg.content.size;
    bytes += msg.toolCallId.size;
    bytes += msg.toolCallsJson.size;

    uint64_t tokens = (bytes + 3) / 4;
    if (tokens > 0xffffffffu)
        return 0xffffffffu;
    return (uint32_t)tokens;
}

static auto EstimateConversationTokens(Conversation const &conv) -> uint32_t
{
    uint64_t tokens = 0;
    for (uint64_t i = 0; i < conv.size; ++i)
        tokens += EstimateMessageTokens(conv.data[i]);

    if (tokens > 0xffffffffu)
        return 0xffffffffu;
    return (uint32_t)tokens;
}

static auto RoleName(MessageRole role) -> const char *
{
    switch (role)
    {
    case MessageRole::System:
        return "system";
    case MessageRole::User:
        return "user";
    case MessageRole::Assistant:
        return "assistant";
    case MessageRole::Tool:
        return "tool";
    }
    return "unknown";
}

static void AppendRaw(span<uint8_t> dst, uint64_t &pos, const char *s)
{
    while (*s && pos < dst.size)
    {
        dst.data[pos] = (uint8_t)*s;
        ++pos;
        ++s;
    }
}

static void AppendBytes(span<uint8_t> dst, uint64_t &pos, byteview src)
{
    uint64_t n = src.size;
    if (pos + n > dst.size)
        n = dst.size - pos;
    if (n > 0)
    {
        MemCpy(dst.data + pos, src.data, n);
        pos += n;
    }
}

static auto BuildCompactionSummary(Conversation const &conv, uint64_t firstRecent, region_alloc &alloc) -> byteview
{
    constexpr uint64_t kSummaryBufSize = 8192;
    constexpr uint64_t kSnippetBytes = 360;

    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, kSummaryBufSize);
    uint64_t pos = 0;

    AppendRaw(buf, pos, "Earlier conversation was compacted locally because the context window was getting large.\n");
    AppendRaw(buf, pos, "This is an approximate transcript sketch; ask the user to restate details if needed.\n\n");

    for (uint64_t i = 1; i < firstRecent && pos + 64 < buf.size; ++i)
    {
        Message const &msg = conv.data[i];
        AppendRaw(buf, pos, "- ");
        AppendRaw(buf, pos, RoleName(msg.role));
        AppendRaw(buf, pos, ": ");

        byteview src = msg.content;
        if (src.size == 0 && msg.toolCallsJson.size > 0)
        {
            AppendRaw(buf, pos, "[tool_calls] ");
            src = msg.toolCallsJson;
        }
        else if (msg.role == MessageRole::Tool && msg.toolCallId.size > 0)
        {
            AppendRaw(buf, pos, "[");
            AppendBytes(buf, pos, msg.toolCallId);
            AppendRaw(buf, pos, "] ");
        }

        uint64_t snippetSize = src.size;
        if (snippetSize > kSnippetBytes)
            snippetSize = kSnippetBytes;
        AppendBytes(buf, pos, {src.data, snippetSize});
        if (src.size > snippetSize)
            AppendRaw(buf, pos, "...");
        AppendRaw(buf, pos, "\n");
    }

    return {buf.data, pos};
}
static auto EffectiveContextWarnTokens(AgentConfig const &config) -> uint32_t
{
    if (config.contextWindowTokens == 0)
        return 0;
    if (config.contextWarnTokens > 0)
        return config.contextWarnTokens;
    return (config.contextWindowTokens * 85) / 100;
}

static auto EffectiveContextCompactTokens(AgentConfig const &config) -> uint32_t
{
    if (config.contextWindowTokens == 0)
        return 0;
    if (config.contextCompactTokens > 0)
        return config.contextCompactTokens;
    return (config.contextWindowTokens * 92) / 100;
}

static auto CompactConversationIfNeeded(Conversation &conv, AgentConfig const &config, region_alloc &alloc,
                                        file_handle stdoutHandle) -> bool
{
    uint32_t compactTokens = EffectiveContextCompactTokens(config);
    if (compactTokens == 0)
        return false;

    uint32_t beforeTokens = EstimateConversationTokens(conv);
    if (beforeTokens < compactTokens)
        return false;
    if (conv.size <= 2 || config.contextKeepRecentMessages == 0)
        return false;

    uint64_t firstRecent = 1;
    if (conv.size > config.contextKeepRecentMessages)
        firstRecent = conv.size - config.contextKeepRecentMessages;

    while (firstRecent > 1 && conv.data[firstRecent].role == MessageRole::Tool)
        --firstRecent;

    if (firstRecent <= 1)
        return false;

    byteview summary = BuildCompactionSummary(conv, firstRecent, alloc);

    Conversation compacted{};
    InlineVec::Append(compacted, conv.data[0]);

    Message summaryMsg{};
    summaryMsg.role = MessageRole::System;
    summaryMsg.content = summary;
    InlineVec::Append(compacted, summaryMsg);

    for (uint64_t i = firstRecent; i < conv.size; ++i)
        InlineVec::Append(compacted, conv.data[i]);

    conv = compacted;

    uint32_t afterTokens = EstimateConversationTokens(conv);
    FileWriteFmt(stdoutHandle, "\n%s[context compacted: %u -> %u estimated tokens, kept %u recent messages]%s\n"_s,
                 Term::kYellow, beforeTokens, afterTokens, (uint32_t)(conv.size - 2), Term::kReset);
    return true;
}

// ─── Stub tool dispatch ─────────────────────────────────────────────────────
auto StubDispatchTool(byteview /*toolCallId*/, byteview toolName, byteview /*toolArguments*/, region_alloc &alloc)
    -> ToolResult
{
    ToolResult result;
    result.isError = true;

    uint8_t buf[256];
    uint32_t len =
        StringWriteFmt(span<uint8_t>{buf, sizeof(buf)}, "Unknown tool: %.*s"_s, (int)toolName.size, toolName.data);
    result.content = AllocString(alloc, {buf, len});
    return result;
}

// ─── Main loop ──────────────────────────────────────────────────────────────
void RunAgentLoop(region_alloc &alloc, AgentConfig const &config, ProviderFn provider, DispatchToolFn dispatchTool)
{
    using namespace Term;

    Conversation conv{};
    file_handle stdinHandle = GetStdin();
    file_handle stdoutHandle = GetStdout();

    // ─── System prompt ─────────────────────────────────────────────────
    {
        byteview sysPrompt = BuildSystemPrompt(alloc);
        Message sysMsg{};
        sysMsg.role = MessageRole::System;
        sysMsg.content = sysPrompt;
        InlineVec::Append(conv, sysMsg);
    }
    FileWriteFmt(stdoutHandle, "%scoding_agent%s ready. Type a message (Ctrl-D to exit).\n"_s, kCyan, kReset);

    // ─── REPL loop ──────────────────────────────────────────────────────────
    for (;;)
    {
        // Prompt
        FileWriteFmt(stdoutHandle, "\n%s>%s "_s, kGreen, kReset);

        byteview userInput = ReadLine(alloc, stdinHandle);
        if (userInput.size == 0)
        {
            FileWriteFmt(stdoutHandle, "\nGoodbye.\n"_s);
            break;
        }

        // Check for exit command
        if (ByteviewEq(userInput, "exit") || ByteviewEq(userInput, "quit"))
        {
            FileWriteFmt(stdoutHandle, "Goodbye.\n"_s);
            break;
        }

        // Append user message
        {
            Message userMsg{};
            userMsg.role = MessageRole::User;
            userMsg.content = userInput;
            InlineVec::Append(conv, userMsg);
        }

        // ─── Tool-call loop ─────────────────────────────────────────────────
        bool waitingForUser = false;
        bool contextWarnedThisTurn = false;
        uint32_t turnTokens = 0; // total tokens used this turn
        for (uint32_t iteration = 0; iteration < config.maxToolIterations; ++iteration)
        {
            CompactConversationIfNeeded(conv, config, alloc, stdoutHandle);

            uint32_t warnTokens = EffectiveContextWarnTokens(config);
            uint32_t estimatedTokens = EstimateConversationTokens(conv);
            if (!contextWarnedThisTurn && warnTokens > 0 && estimatedTokens >= warnTokens)
            {
                FileWriteFmt(stdoutHandle, "\n%s[context warning: about %u estimated tokens in conversation]%s\n"_s,
                             kYellow, estimatedTokens, kReset);
                contextWarnedThisTurn = true;
            }

            // Call provider
            ProviderResponse response = provider(conv, config, alloc);
            turnTokens += response.totalTokens;
            if (!contextWarnedThisTurn && warnTokens > 0 && response.totalTokens >= warnTokens)
            {
                FileWriteFmt(stdoutHandle, "\n%s[context warning: provider reported %u tokens for this request]%s\n"_s,
                             kYellow, response.totalTokens, kReset);
                contextWarnedThisTurn = true;
            }

            if (response.finishReason == ProviderFinishReason::Stop)
            {
                // Text response — display and wait for next user input
                if (response.content.size > 0)
                {
                    FileWriteFmt(stdoutHandle, "\n%.*s"_s, (int)response.content.size, response.content.data);

                    Message assistantMsg{};
                    assistantMsg.role = MessageRole::Assistant;
                    assistantMsg.content = response.content;
                    InlineVec::Append(conv, assistantMsg);
                }

                // Show token usage
                if (turnTokens > 0)
                    FileWriteFmt(stdoutHandle, "\n%s[%u tokens]%s"_s, kDim, turnTokens, kReset);

                waitingForUser = true;
                break;
            }

            if (response.finishReason == ProviderFinishReason::ToolCalls)
            {
                // Append assistant message with tool_calls
                {
                    Message assistantMsg{};
                    assistantMsg.role = MessageRole::Assistant;
                    assistantMsg.toolCallsJson = response.toolCallsJson;
                    InlineVec::Append(conv, assistantMsg);
                }

                // Parse tool_calls JSON array and dispatch each
                span<json_value> jsonStorage = RegionAlloc::AllocArray<json_value>(alloc, 256);
                json_parser parser;
                JsonParser::Init(parser, response.toolCallsJson, jsonStorage);
                json_value *root = JsonParser::ParseNext(parser);

                if (!root || root->tag != json_tag::ArrayBegin)
                {
                    FileWriteFmt(stdoutHandle, "\n%s[malformed tool_calls — skipping]%s\n"_s, kRed, kReset);
                    waitingForUser = true;
                    break;
                }

                // Helper: C string to byteview
                auto bv = [](const char *s) -> byteview {
                    uint32_t n = 0;
                    while (s[n])
                        ++n;
                    return {(const uint8_t *)s, n};
                };

                for (json_value &tc : *root)
                {
                    if (tc.tag != json_tag::ObjectBegin)
                        continue;

                    byteview callId{};
                    byteview fnName{};
                    byteview fnArgs{};
                    // Extract id
                    json_value *idVal;
                    if (JsonValue::TryAny(tc, bv("id"), idVal) && idVal->tag == json_tag::String)
                        callId = idVal->val.valStr;

                    // Extract function.name and function.arguments
                    json_value *fnVal;
                    if (JsonValue::TryAny(tc, bv("function"), fnVal) && fnVal->tag == json_tag::ObjectBegin)
                    {
                        json_value *nameVal;
                        if (JsonValue::TryAny(*fnVal, bv("name"), nameVal) && nameVal->tag == json_tag::String)
                            fnName = nameVal->val.valStr;
                        json_value *argsVal;
                        if (JsonValue::TryAny(*fnVal, bv("arguments"), argsVal) && argsVal->tag == json_tag::String)
                        {
                            // JSON parser stores escaped strings with raw backslashes -- unescape them
                            byteview raw = argsVal->val.valStr;
                            if (raw.size > 0)
                            {
                                span<uint8_t> unesc = RegionAlloc::AllocArray<uint8_t>(alloc, raw.size);
                                uint32_t out = 0;
                                for (uint32_t i = 0; i < raw.size; ++i)
                                {
                                    if (raw.data[i] == '\\' && i + 1 < raw.size)
                                    {
                                        ++i; // skip backslash
                                        uint8_t next = raw.data[i];
                                        if (next == 'n')
                                            unesc.data[out++] = '\n';
                                        else if (next == 'r')
                                            unesc.data[out++] = '\r';
                                        else if (next == 't')
                                            unesc.data[out++] = '\t';
                                        else
                                            unesc.data[out++] = (uint8_t)next;
                                    }
                                    else
                                    {
                                        unesc.data[out++] = raw.data[i];
                                    }
                                }
                                fnArgs = {unesc.data, out};
                            }
                        }
                    }

                    if (fnName.size == 0)
                    {
                        FileWriteFmt(stdoutHandle, "\n%s[tool call with no name — skipping]%s\n"_s, kRed, kReset);
                        continue;
                    }

                    FileWriteFmt(stdoutHandle, "\n%s[%.*s]%s "_s, kCyan, (int)fnName.size, fnName.data, kReset);

                    ToolResult result = dispatchTool(callId, fnName, fnArgs, alloc);
                    // Append tool result message to conversation
                    {
                        Message toolMsg{};
                        toolMsg.role = MessageRole::Tool;
                        toolMsg.toolCallId = callId;
                        toolMsg.content = result.content;
                        InlineVec::Append(conv, toolMsg);
                    }

                    if (result.isError)
                        FileWriteFmt(stdoutHandle, "%sERROR: %.*s%s\n"_s, kRed, (int)result.content.size,
                                     result.content.data, kReset);
                    else
                        FileWriteFmt(stdoutHandle, "%sOK%s\n"_s, kGreen, kReset);
                }

                // Continue tool-call loop — provider may call more tools
                continue;
            }

            if (response.finishReason == ProviderFinishReason::Error)
            {
                if (response.content.size > 0)
                    FileWriteFmt(stdoutHandle, "\n%s%.*s%s\n"_s, kRed, (int)response.content.size,
                                 response.content.data, kReset);
                else
                    FileWriteFmt(stdoutHandle, "\n%s[error from provider]%s\n"_s, kRed, kReset);
                waitingForUser = true;
                break;
            }
        }

        if (!waitingForUser)
        {
            FileWriteFmt(stdoutHandle, "\n%s[reached max tool iterations (%u) — returning to user]%s\n"_s, kYellow,
                         config.maxToolIterations, kReset);
        }
    }
}

} // namespace nyla
