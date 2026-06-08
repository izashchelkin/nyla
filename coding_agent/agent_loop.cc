// ─── Agent loop implementation ───────────────────────────────────────────────

#include "coding_agent/agent_loop.h"
#include "coding_agent/system_prompt.h"
#include "coding_agent/terminal_colors.h"
#include "coding_agent/tool_failure_repair.h"
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
void RunAgentLoop(region_alloc &alloc, AgentConfig &config, ProviderFn &provider, DispatchToolFn dispatchTool,
                  span<const ModelDef> models)
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

    // Show available models and current
    FileWriteFmt(stdoutHandle, "%scoding_agent%s ready. Model: %s%s%s. Type /model to switch, Ctrl-D to exit.\n"_s,
                 kCyan, kReset, kGreen, config.modelName, kReset);
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

        // ─── /model command ───────────────────────────────────────────
        if (userInput.size >= 6 && MemEq(userInput.data, "/model", 6))
        {
            // /model -- list available models
            // /model <name> -- switch to a model
            byteview arg = {};
            if (userInput.size > 7)
                arg = {userInput.data + 7, userInput.size - 7};

            if (arg.size == 0)
            {
                // List models
                FileWriteFmt(stdoutHandle, "\n%sAvailable models:%s\n"_s, kCyan, kReset);
                for (uint64_t i = 0; i < models.size; ++i)
                {
                    bool isCurrent = (provider == models.data[i].provider &&
                                      strcmp(config.modelName, models.data[i].modelName) == 0);
                    FileWriteFmt(stdoutHandle, "  %s%s%s -- %s%s\n"_s, isCurrent ? kGreen : kReset, models.data[i].name,
                                 isCurrent ? " (current)" : "", kReset, models.data[i].displayName);
                }
                FileWriteFmt(stdoutHandle, "\n%sUsage: /model <name> to switch%s\n"_s, kDim, kReset);
            }
            else
            {
                // Switch model
                bool found = false;
                for (uint64_t i = 0; i < models.size; ++i)
                {
                    byteview modelNameView{(const uint8_t *)models.data[i].name, (uint32_t)strlen(models.data[i].name)};
                    if (arg.size == modelNameView.size && MemEq(arg.data, modelNameView.data, arg.size))
                    {
                        // Check env vars
                        if (models.data[i].envVars && models.data[i].envVars[0])
                        {
                            // Parse space-separated env var names
                            const char *p = models.data[i].envVars;
                            bool allSet = true;
                            while (*p)
                            {
                                while (*p == ' ')
                                    ++p;
                                const char *start = p;
                                while (*p && *p != ' ')
                                    ++p;
                                uint32_t len = (uint32_t)(p - start);
                                byteview val{};
                                if (!TryReadEnvVar({(const uint8_t *)start, len}, val))
                                {
                                    FileWriteFmt(stdoutHandle, "\n%sModel '%s' requires %.*s to be set.%s\n"_s, kRed,
                                                 models.data[i].name, len, start, kReset);
                                    allSet = false;
                                }
                            }
                            if (!allSet)
                            {
                                found = true;
                                break;
                            }
                        }

                        provider = models.data[i].provider;
                        config.modelName = models.data[i].modelName;
                        FileWriteFmt(stdoutHandle, "\n%sSwitched to %s (%s)%s\n"_s, kGreen, models.data[i].name,
                                     models.data[i].displayName, kReset);
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    FileWriteFmt(stdoutHandle, "\n%sUnknown model '%s'. Use /model to list.%s\n"_s, kRed, (int)arg.size,
                                 arg.data, kReset);
                }
            }
            continue;
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
                // Display thinking/reasoning content first (dimmed)
                if (response.thinkingContent.size > 0)
                {
                    FileWriteFmt(stdoutHandle, "\n%s[thinking]\n%.*s\n[/thinking]%s"_s, kDim,
                                 (int)response.thinkingContent.size, response.thinkingContent.data, kReset);
                }

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
                uint64_t convSizeBeforeTools = conv.size;

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

                    // ─── Tool failure repair ───────────────────────────────────
                    if (result.isError && config.enableToolFailureRepair)
                    {
                        FailureLayer layer = ClassifyToolFailure(fnName, result.content);

                        if (layer == FailureLayer::Mechanical)
                        {
                            byteview fixedArgs = TryMechanicalRepair(fnName, fnArgs, result.content, alloc);
                            if (fixedArgs.size > 0)
                            {
                                FileWriteFmt(stdoutHandle, "%s[auto-repairing...]%s "_s, kYellow, kReset);
                                ToolResult repaired = dispatchTool(callId, fnName, fixedArgs, alloc);
                                if (!repaired.isError)
                                {
                                    result = repaired;
                                    FileWriteFmt(stdoutHandle, "%s[repaired -- deterministic fix]%s "_s, kGreen,
                                                 kReset);
                                }
                            }
                        }
                        else if (layer == FailureLayer::Semantic)
                        {
                            // Fork: give the model a fresh turn with the failure as context.
                            // Each fork gets its own memory-pool chunk so it doesn't exhaust
                            // the main loop's arena or overflow stack buffers.
                            FileWriteFmt(stdoutHandle, "%s[forking to fix...]%s "_s, kYellow, kReset);

                            region_alloc forkAlloc = RegionAlloc::Create(MemPagePool::kChunkSize, 0);

                            // Build forked conversation (without the failed assistant msg + tool results).
                            // Byteviews point into alloc (pre-fork messages) -- the provider only reads them.
                            Conversation forkConv{};
                            for (uint64_t i = 0; i < convSizeBeforeTools; ++i)
                                InlineVec::Append(forkConv, conv.data[i]);

                            // Add system message with the failure -- allocated directly in forkAlloc
                            {
                                uint32_t cap = 512 + fnName.size + fnArgs.size + result.content.size;
                                span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(forkAlloc, cap);
                                uint32_t forkLen =
                                    StringWriteFmt(buf,
                                                   "The last assistant attempted to call tool \"%.*s\" with "
                                                   "arguments %.*s but the tool failed: %.*s\n\n"
                                                   "Fix the tool call and try again, or respond appropriately "
                                                   "if the failure is not recoverable."_s,
                                                   (int)fnName.size, fnName.data, (int)fnArgs.size, fnArgs.data,
                                                   (int)result.content.size, result.content.data);
                                Message sysMsg{};
                                sysMsg.role = MessageRole::System;
                                sysMsg.content = {buf.data, forkLen};
                                InlineVec::Append(forkConv, sysMsg);
                            }

                            // Call provider with the forked conversation -- response lands in forkAlloc
                            AgentConfig forkConfig = config;
                            forkConfig.modelName = config.helperModel;
                            ProviderResponse forkResp = provider(forkConv, forkConfig, forkAlloc);

                            if (forkResp.finishReason == ProviderFinishReason::ToolCalls ||
                                (forkResp.finishReason == ProviderFinishReason::Stop && forkResp.content.size > 0))
                            {
                                // Fork succeeded -- copy response data to main alloc before destroying forkAlloc
                                forkResp.content = AllocString(alloc, forkResp.content);
                                forkResp.toolCallsJson = AllocString(alloc, forkResp.toolCallsJson);
                                forkResp.thinkingContent = AllocString(alloc, forkResp.thinkingContent);

                                // Rebuild conversation in main alloc: pre-fork messages + system message
                                conv.size = 0;
                                for (uint64_t i = 0; i < convSizeBeforeTools; ++i)
                                    InlineVec::Append(conv, forkConv.data[i]);
                                {
                                    Message &sysMsg = forkConv.data[convSizeBeforeTools];
                                    Message copy{};
                                    copy.role = MessageRole::System;
                                    copy.content = AllocString(alloc, sysMsg.content);
                                    InlineVec::Append(conv, copy);
                                }

                                // If the fork produced tool calls, dispatch them
                                if (forkResp.toolCallsJson.size > 0)
                                {
                                    Message forkAsst{};
                                    forkAsst.role = MessageRole::Assistant;
                                    forkAsst.content = forkResp.content;
                                    forkAsst.toolCallsJson = forkResp.toolCallsJson;
                                    InlineVec::Append(conv, forkAsst);

                                    span<json_value> fjs = RegionAlloc::AllocArray<json_value>(alloc, 256);
                                    json_parser fp;
                                    JsonParser::Init(fp, forkResp.toolCallsJson, fjs);
                                    json_value *froot = JsonParser::ParseNext(fp);
                                    if (froot && froot->tag == json_tag::ArrayBegin)
                                    {
                                        for (json_value &ftc : *froot)
                                        {
                                            if (ftc.tag != json_tag::ObjectBegin)
                                                continue;
                                            byteview fcid{};
                                            byteview fname{};
                                            byteview fargs{};
                                            json_value *fidV;
                                            if (JsonValue::TryAny(ftc, bv("id"), fidV) && fidV->tag == json_tag::String)
                                                fcid = fidV->val.valStr;
                                            json_value *ffn;
                                            if (JsonValue::TryAny(ftc, bv("function"), ffn) &&
                                                ffn->tag == json_tag::ObjectBegin)
                                            {
                                                json_value *fnv;
                                                if (JsonValue::TryAny(*ffn, bv("name"), fnv) &&
                                                    fnv->tag == json_tag::String)
                                                    fname = fnv->val.valStr;
                                                json_value *fav;
                                                if (JsonValue::TryAny(*ffn, bv("arguments"), fav) &&
                                                    fav->tag == json_tag::String)
                                                    fargs = fav->val.valStr;
                                            }
                                            if (fname.size == 0)
                                                continue;

                                            FileWriteFmt(stdoutHandle, "\n%s[%.*s]%s "_s, kCyan, (int)fname.size,
                                                         fname.data, kReset);
                                            ToolResult fres = dispatchTool(fcid, fname, fargs, alloc);
                                            {
                                                Message tmsg{};
                                                tmsg.role = MessageRole::Tool;
                                                tmsg.toolCallId = fcid;
                                                tmsg.content = fres.content;
                                                InlineVec::Append(conv, tmsg);
                                            }
                                            if (fres.isError)
                                                FileWriteFmt(stdoutHandle, "%sERROR: %.*s%s\n"_s, kRed,
                                                             (int)fres.content.size, fres.content.data, kReset);
                                            else if (fres.content.size > 0)
                                                FileWriteFmt(stdoutHandle, "%sOK%s\n%s%.*s%s\n"_s, kGreen, kReset, kDim,
                                                             (int)fres.content.size, fres.content.data, kReset);
                                            else
                                                FileWriteFmt(stdoutHandle, "%sOK%s\n"_s, kGreen, kReset);
                                        }
                                    }
                                }

                                // Display the fork's text response
                                if (forkResp.content.size > 0)
                                {
                                    FileWriteFmt(stdoutHandle, "\n%.*s"_s, (int)forkResp.content.size,
                                                 forkResp.content.data);
                                }
                                FileWriteFmt(stdoutHandle, "%s[forked -- failure resolved]%s\n"_s, kGreen, kReset);

                                RegionAlloc::Destroy(forkAlloc);
                                goto continueMainLoop;
                            }

                            RegionAlloc::Destroy(forkAlloc);
                            // Fork failed -- fall through to show original error
                            FileWriteFmt(stdoutHandle, "%s[fork failed -- showing original error]%s "_s, kYellow,
                                         kReset);
                        }
                        // Layer::PassThrough -- fall through, main model sees raw error
                    }

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
                    else if (result.content.size > 0)
                    {
                        // Count lines and collapse long outputs
                        uint32_t lineCount = 1;
                        for (uint32_t i = 0; i < result.content.size; ++i)
                            if (result.content.data[i] == '\n')
                                ++lineCount;

                        constexpr uint32_t kMaxPreviewLines = 5;
                        if (lineCount <= kMaxPreviewLines)
                        {
                            FileWriteFmt(stdoutHandle, "%sOK%s\n%s%.*s%s\n"_s, kGreen, kReset, kDim,
                                         (int)result.content.size, result.content.data, kReset);
                        }
                        else
                        {
                            // Show first 3 lines as preview
                            uint32_t previewEnd = 0;
                            uint32_t linesSeen = 0;
                            for (uint32_t i = 0; i < result.content.size && linesSeen < 3; ++i)
                            {
                                previewEnd = i + 1;
                                if (result.content.data[i] == '\n')
                                    ++linesSeen;
                            }
                            FileWriteFmt(stdoutHandle, "%sOK (%u lines)%s\n%s%.*s%s\n%s  ... (%u more lines)%s\n"_s,
                                         kGreen, lineCount, kReset, kDim, (int)previewEnd, result.content.data, kReset,
                                         kDim, lineCount - 3, kReset);
                        }
                    }
                    else
                        FileWriteFmt(stdoutHandle, "%sOK%s\n"_s, kGreen, kReset);
                }

            // Continue tool-call loop -- provider may call more tools
            continueMainLoop:
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

        // ─── Arena GC: rebuild conversation in fresh arena to prevent exhaustion ───
        {
            region_alloc fresh = RegionAlloc::Create(MemPagePool::kChunkSize, 0);
            for (uint64_t i = 0; i < conv.size; ++i)
            {
                Message &msg = conv.data[i];
                auto copyToFresh = [&](byteview src) -> byteview {
                    if (src.size == 0)
                        return {};
                    span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(fresh, src.size);
                    MemCpy(dst.data, src.data, src.size);
                    return {dst.data, src.size};
                };
                msg.content = copyToFresh(msg.content);
                msg.toolCallId = copyToFresh(msg.toolCallId);
                msg.toolCallsJson = copyToFresh(msg.toolCallsJson);
            }
            RegionAlloc::Destroy(alloc);
            alloc = fresh;
        }
    }
}

} // namespace nyla
