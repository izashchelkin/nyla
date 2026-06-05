# Native Coding Agent — TODO

Roadmap for a C++23 coding agent harness that talks to ChatGPT Codex / Ollama
and uses xcav for structural code editing. No pi runtime dependency, no JavaScript,
no STL.

---

## North stars

1. **Default-pi parity without extensions**
   - Build a usable native harness at the level of default `pi` without extensions:
     good REPL UX, reliable tool calling, prompt/project context, context management,
     clear errors, safe editing, and integration tests.
   - `../pi` contains the pi agent source folder. It is not a dependency, but it is
     the reference implementation to inspect when we need to understand how pi handles
     provider loops, tool schemas, prompt construction, compaction, or UX.

2. **Multi-model assistance**
   - Treat local models as cheap helper workers around the main model, not as a full
     replacement for it.
   - Candidate uses:
     - auto-repair malformed/broken tool-call JSON before giving up;
     - summarize tool failures for the main model so it does not repeat a long chain
       of exploratory tool calls;
     - compress large tool outputs into short factual notes;
     - preflight risky edits with a second opinion before applying them.

---

## Current shape / review notes

- Phases 1 and 2 are complete and well documented.
- Phase 3 is complete: REPL, three providers (ChatGPT Codex, DeepSeek, Ollama),
  runtime model switching via `/model`, tool dispatch for 11 xcav + 3 system tools,
  prompt construction, context management, SSE streaming for Codex, ANSI escape
  filtering, and 11 e2e integration tests.
- Credentials auto-loaded from `~/.pi/agent/auth.json` — ChatGPT Codex available
  out of the box if pi is authenticated.
- Phase 4 (hardening) is the next step: pi parity audit, tool-call repair,
  configuration polish, regression harness.

---

## Phase 1: Audit nyla/commons ✅ DONE

**Goal**: Find every place where repo apps work around commons limitations.

### Tasks

- [x] 1.1 Survey subprocess usage — `Spawn()`/`RunSync()` cover everything; 2 raw `system()` in wm are minor
- [x] 1.2 Survey string manipulation — `ByteviewEq`/`StrEq` defined locally in xcav, no Split/Trim/Find/Join
- [x] 1.3 Survey file I/O — apps use `FileOpen`/etc. cleanly; missing `FileExists`/`IsDirectory`/`FileDelete`/`CreateDirectory`
- [x] 1.4 Survey JSON — `json_parser` exists for reading; NO serialization (xcav/usage_log.cc builds JSON with raw `snprintf`)
- [x] 1.5 Survey HTTP/network — greenfield, nothing exists
- [x] 1.6 Survey memory/arena — `region_alloc`/`inline_vec`/`inline_string` adequate; 8KB limit in `RunSync`
- [x] 1.7 Survey error handling — pattern is established, need recoverable paths for API responses
- [x] 1.8 Document findings in `coding_agent/audit.md`
  - Key finding: xcav is the canary — it reveals all gaps (system wrappers, JSON serialization)

---

## Phase 2: Primitives in nyla/commons ✅ DONE

**Goal**: Introduce (or prepare stubs for) every primitive the harness needs.
Some may be thin wrappers, some may be new. Windows compatibility can be stubbed.

**Completed (2026-06-03)**: All tasks done. System wrappers, subprocess runner,
HTTP client (curl-based), JSON parser fixes, API key support, Deadline wrapper.
All 195 xcav tests pass.

### Tasks

- [x] 2.0a Escape hatch cleanup (unplanned — emerged from audit)
- [x] 2.0b `Exit()` semantics — changed from `quick_exit` to `_Exit`
- [x] 2.0c xcav improvements (2026-06-03)
- [x] 2.1 Subprocess runner
- [x] 2.2 HTTP client
- [x] 2.3 JSON parser fixes
- [x] 2.4 String helpers — **DEFERRED to Phase 3**
- [x] 2.5 API key / secrets
- [x] 2.6 Simple timer / deadline
- [x] 2.7 Temp file helper — **DEFERRED to Phase 3**
- [x] 2.8 Build integration

---

## Phase 3: Harness ✅ DONE

**Goal**: Build the agent loop in a new `coding_agent/` directory.
Uses xcav CmdXxx functions directly. Talks to ChatGPT Codex / DeepSeek / Ollama.

### Tasks

- [x] 3.1 Project skeleton ✅
- [x] 3.2 Agent loop ✅ — REPL, tool-call loop (25 iterations), exit/quit/Ctrl-D
- [x] 3.3 Tool dispatcher ✅ — `tool_registry.h/cc`, 11 xcav + 3 system tools
- [x] 3.4 Tool definitions (xcav tools) ✅ — blocks, read, move, move_into, delete, replace, replace_block, undo, edit, copy, insert
- [x] 3.5 Tool definitions (system tools) ✅ — bash, read_file, write_file
- [x] 3.6 Provider abstraction ✅ — DeepSeekProvider, OpenAIProvider, OllamaProvider, StubProvider (shared OpenAI-compatible format)
- [x] 3.7 JSON serialization ✅ — BufferBuilder-based request bodies
- [x] 3.8 JSON parsing ✅ — OpenAI/Ollama response parsing, error handling
- [x] 3.9 System prompt ✅ — `~/.config/coding_agent/prompt.md` or embedded default, tool descriptions, project context
- [x] 3.10 Display and UX ✅ — colored output, tool status, token counts
- [x] 3.11 Context window management ✅ — token estimation, warnings, auto-compaction
- [x] 3.12 Integration tests ✅ — 11 e2e tests via `scripts/test_ca.sh` (5 error-handling + 5 integration + /model)
- [x] 3.13 ChatGPT Codex provider ✅
  - `provider_openai_codex.h/cc` — Codex Responses API (SSE streaming)
  - OAuth token + JWT account ID extraction
  - `chatgpt.com/backend-api/codex/responses` endpoint
  - SSE event parsing: text deltas, function call arguments, completion events
  - Fallback: extracts text from `response.output_item.done` when deltas are skipped
  - Model: `gpt-5.4-mini`
- [x] 3.14 Runtime model switching ✅
  - `/model` command — lists available models, switches with `/model <name>`
  - `ModelDef` struct in `agent_loop.h`
  - Auto-detection: ChatGPT if pi's auth.json has OAuth token, Ollama always available
  - Credentials auto-loaded from `~/.pi/agent/auth.json` — no env vars needed
  - ANSI escape sequence filtering (arrow keys don't corrupt input)

---

## Phase 4: Default-pi parity / hardening

**Goal**: Make `coding_agent` dependable enough to use as the default native coding
harness, comparable to default `pi` without extensions.

### Tasks

- [ ] 4.1 Compare against default pi behavior
  - Inspect `../pi` as a reference, not a dependency
  - Note gaps in provider loop, tool-call protocol, prompt layout, context compaction,
    display UX, and error handling
  - Capture findings in `coding_agent/pi_parity.md`

- [ ] 4.2 Tool-call failure interceptor (three-layer design)
  - **Problem**: When a tool fails, the raw error is fed back to the main model as a
    `MessageRole::Tool` message. The main model goes into fix-it mode — re-reading files,
    re-trying with tweaked args, debugging — burning tokens and iterations.
  - **Solution**: Intercept failures before they reach the main model. Route through a
    three-layer pipeline:
    - **Layer 0 — Deterministic mechanical fix** (no LLM, zero latency): JSON unescape
      (already done in agent_loop.cc), missing braces, arg type coercion, known typo
      fixes. If fix works, retry the tool call and feed the successful result to the
      main model with an `[auto-repaired: ...]` annotation.
    - **Layer 1 — Local LLM repair or summarize**: For semantic failures a regex can't
      fix ("oldText not found", "destLine is inside a function body", "file doesn't
      exist"). Send a compact context blob to a cheap local model (qwen3:4b via Ollama):
      tool name, original args, error message, and the last assistant message for intent
      context. Model responds with `{"action": "retry", "fixedArgs": {...}}` or
      `{"action": "summarize", "summary": "..."}`. Retry on success; feed summary on
      failure. Always annotate what was changed.
    - **Layer 2 — Legitimate pass-through**: Some errors the main model SHOULD see
      (test failures, build errors, user-visible problems). Classified by tool name +
      error pattern.
  - **Helper model context**: Minimal — only last assistant message (intent), tool name,
    original args, error message, and optionally a 10-line file snippet if relevant. Do
    NOT send the full conversation — defeats the purpose.
  - **Latency budget**: ~0.6–2.5s (Ollama 0.5–2s + tool retry 0.1–0.5s). Compare to
    2–4 wasted main-model iterations at 2–10s each.
  - **Configuration**: Gated behind `AgentConfig::enableToolFailureRepair`;
    `helperModel` selects which Ollama model to use.

- [ ] 4.3 Tool output compression
  - When a tool *succeeds* but returns huge output (>2000 bytes), send to the helper
    model for structured compression before feeding to the main model.
  - Input: tool name + raw output. Output: key facts, file structure, error locations.
  - Gated behind `AgentConfig::enableToolOutputCompression` and
    `toolOutputCompressThreshold`.
  - Preserve raw output when small; compress only when useful.

- [ ] 4.4 Safer edit workflow
  - Require clearer user-facing summaries before destructive edits
  - Show concise diffs after edits and before continuing
  - Prefer build/test verification suggestions after code changes

- [ ] 4.5 Configuration polish
  - Provider/model selection without recompiling
  - Config file/env overrides for context thresholds, max tool iterations, temperature,
    provider URL, and local helper model
  - Sensible defaults for this repo

- [ ] 4.6 Regression harness
  - Golden tests for prompt construction and request JSON
  - Fake-provider tests for tool-call loops, repair, compaction, and failure summaries
  - Keep `@scripts/test_ca.sh` as the user-facing integration test entrypoint

- [ ] 4.7 Evaluate Markdown support in xcav
  - Slightly separate from the harness, but relevant because agent docs and TODOs are markdown-heavy
  - Investigate whether xcav should expose structural Markdown blocks (headings/sections/code fences)
  - Decide if this belongs in xcav core or remains plain text editing

- [x] 4.8 Fix JSON string unescaping (2026-06-05)
  - JSON parser stores strings with raw escape sequences (\n → literal backslash-n)
  - Added `UnescapeJsonString` helper in `provider.h`
  - Applied to all content/delta/error extraction points in DeepSeek, Codex, and Ollama providers
  - Response text now renders with proper newlines instead of literal `\n` characters

- [x] 4.9 Thinking/reasoning token support (2026-06-05)

- [x] 4.10 Fork memory-pool isolation (2026-06-05)
  - Each tool-failure fork now gets its own `region_alloc` chunk instead of
    sharing the main loop's arena. The system message is allocated directly
    in forkAlloc (no 4096-byte stack buffer). After the fork, results are
    copied back to main alloc before forkAlloc is destroyed.
  - Fixes `ASSERT(written + dataSize <= out.size)` crash when `fnArgs` or
    `result.content` is large enough to overflow the 4096-byte stack buffer.
  - Added `thinkingContent` field to `ProviderResponse`
  - Codex SSE parser handles `response.reasoning_text.delta` events
  - Agent loop displays thinking text with dimmed `[thinking]`/`[/thinking]` brackets
  - Thinking content is NOT included in conversation history (keeps context clean)

---

## Deliverables

```
coding_agent/
├── TODO.md                      ← this file
├── audit.md                     ← Phase 1 findings
├── pi_parity.md                 ← Phase 4 findings (future)
├── main.cc                      ← UserMain: model registry, credential auto-load
├── agent_loop.h                 ← Message/Conversation/AgentConfig/ModelDef types
├── agent_loop.cc                ← REPL, /model, tool-call loop, context compaction
├── tool_registry.h              ← ToolDef, InitToolRegistry, dispatch
├── tool_registry.cc             ← 11 xcav + 3 system tool handlers
├── provider.h                   ← shared declarations (DeepSeek, OpenAI, Codex, Ollama, Stub)
├── provider_deepseek.cc         ← DeepSeek + OpenAI (shared OpenAI format)
├── provider_ollama.cc           ← Ollama local provider
├── provider_ollama.h            ← re-exports provider.h
├── provider_openai_codex.h      ← ChatGPT Codex provider declaration
├── provider_openai_codex.cc     ← Codex Responses API (SSE streaming, OAuth, JWT)
├── system_prompt.h              ← BuildSystemPrompt
├── system_prompt.cc             ← prompt loading, tool descriptions, project context
├── terminal_colors.h            ← ANSI terminal color constants
└── CMakeListsGenerated.txt

scripts/test_ca.sh               ← 11 e2e tests (ChatGPT Codex, auto-loads creds from pi)
```

---

## Decisions made

- **Streaming**: Codex requires SSE streaming (curl -N); DeepSeek/Ollama use non-streaming.
- **HTTP**: curl subprocess via `SubprocessRun` — no mbedtls/openssl dependency.
- **JSON**: Hand-rolled `BufferBuilder` for serialization; `json_parser` for parsing.
- **Windows**: Stub only.
- **Tool iterations**: Cap at 25 before returning to user.
- **Credentials**: Auto-loaded from `~/.pi/agent/auth.json` at startup.
- **Model selection**: Runtime via `/model` command; ChatGPT Codex and Ollama supported.
- **Tool failure routing**: Three-layer interceptor (mechanical fix → local LLM repair/summarize → pass-through).
  Separates "architect" (main model plans) from "mechanic" (helper model fixes execution issues).
  Layer 0 is free (deterministic); Layer 1 uses cheap local model; Layer 2 lets legitimate errors through.

## Decisions to make later

- How much pi parity is desirable vs keeping the native harness simpler
- Whether to add config file support or keep env-var-only
- Markdown block support in xcav — separate tool or xcav core?
