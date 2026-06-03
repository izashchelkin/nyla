# Native Coding Agent — TODO

Roadmap for a C++23 coding agent harness that talks to DeepSeek/OpenAI/Ollama
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
- Phase 3 has reached MVP shape: REPL, providers, tools, prompt, display, and context
  management are implemented.
- The remaining immediate gap is confidence: Ollama integration tests and scripted
  end-to-end checks via `@scripts/test_ca.sh`.
- After 3.12, the TODO should shift from "build the MVP" to "make it dependable
  enough to use as the default coding harness".

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
All 195 xcav tests pass. String helpers and temp file helper deferred to Phase 3
when concrete needs emerge from JSON serialization and harness tool implementations.

### Tasks

- [x] 2.0a Escape hatch cleanup (unplanned — emerged from audit)
  - xcav was using raw `getenv`, `stat`, `mkdir`, `unlink`, `rmdir`, `getpid`, `getcwd`, `_exit`
  - Added wrappers: `file.h` (5 functions), `platform.h` (3 functions), `span.h` (`ByteviewEq`), `mem.h` (`StrEq`)
  - Updated xcav to use all wrappers, dropped `<stdlib.h>`, `<sys/stat.h>`, `<unistd.h>`
  - All 184 xcav tests pass

- [x] 2.0b `Exit()` semantics — changed from `quick_exit` to `_Exit` (no atexit handlers)

- [x] 2.0c xcav improvements (2026-06-03)
  - Fixed `undo` error reporting — `CmdUndo` now checks `RestoreBackup` return value
  - Improved `insert` UX — accepts positional `before`/`after` in addition to `--before`/`--after`
  - Disabled usage logging in tests via `XC_NO_LOG=1` env var
  - Split `operation_failed` into 9 specific error tags (move_failed, edit_failed, etc.)

- [x] 2.1 Subprocess runner
  - `SubprocessRun(command, alloc, stdin_data, timeout_ms) -> subprocess_result`
  - Linux: fork+pipe (separate stdout/stderr pipes, poll-based I/O with timeout)
  - Windows: CreateProcess stub
  - Timeout support via SIGKILL after deadline
  - Environment inherited from parent via execvp
  - 64 KB per-stream output cap
  - Location: `nyla/commons/subprocess.h` + `subprocess_linux.cc` + `subprocess_windows.cc`

- [x] 2.2 HTTP client
  - `HttpPostJson(url, json_body, api_key, alloc) -> http_response`
  - Linux: curl subprocess via `SubprocessRun` (`curl -s -D /dev/stderr -o -`)
    separates headers (stderr) from body (stdout), parse status from HTTP line
  - Windows: stub
  - 30s timeout, API key passed as Bearer token
  - Location: `nyla/commons/http_client.h` + `http_client_linux.cc` + `http_client_windows.cc`

- [x] 2.3 JSON parser fixes (replaced extractor approach)
  - Fixed escape handling: `ParseString` now skips escaped chars (`\"`, `\\`, etc.)
  - Error recovery: replaced all `ASSERT`s with `nullptr` returns — safe for network input
  - Fixed null literal: now properly sets `json_tag::Null` (was `Invalid`)
  - Added `HasNext` guards before all `Read`/`Peek` calls for EOF safety
  - Exported symbols: added `API` to `ParseNext`, `GetNext`, `TryXxx` functions
  - Tests: 53 tests covering basic types, arrays, objects, navigation, whitespace,
    escape sequences, gltf-style parsing, and error recovery
  - Location: tests in `tests/json_parser/`, ctest-registered

- [x] 2.4 String helpers — **DEFERRED to Phase 3** (add when JSON serialization needs them)

- [x] 2.5 API key / secrets
  - `TryReadEnvVar(name, out) -> bool` — added to `platform.h`
  - `FileReadFully` already exists in `file_utils.h` for file slurping

- [x] 2.6 Simple timer / deadline
  - `deadline` struct in `time.h`: `FromMillis()`, `Never()`, `IsExpired()`, `RemainingMs()`
  - `SubprocessRun` updated to use `deadline` internally

- [x] 2.7 Temp file helper — **DEFERRED to Phase 3** (CmdEdit handles temp files internally; no current need)

- [x] 2.8 Build integration — all new files already in CMakeListsGenerated.txt

---

## Phase 3: Harness

**Goal**: Build the agent loop in a new `coding_agent/` directory.
Uses xcav CmdXxx functions directly (no shelling out). Talks to DeepSeek/OpenAI.

### Tasks

- [x] 3.1 Project skeleton ✅
  - `coding_agent/main.cc` — UserMain entry point
  - `coding_agent/CMakeLists.txt` + `CMakeListsGenerated.txt`
  - Root `CMakeLists.txt`: add `coding_agent/` glob + `add_subdirectory`
  - Builds and runs (`cmake --build build/linux-debug --target coding_agent`)

- [x] 3.2 Agent loop ✅
  - `agent_loop.h` — Message types, Conversation, AgentConfig, ProviderResponse, ToolResult, DispatchToolFn
  - `agent_loop.cc` — ReadLine, StubProvider, StubDispatchTool, RunAgentLoop
  - REPL: prompt → read line → append user msg → stub provider → display response → repeat
  - Tool-call loop with max iterations (25 cap), exit/quit commands, Ctrl-D to quit
  - Provider and dispatch are stubbed (real impl in 3.6 + 3.3/3.4)

- [x] 3.3 Tool dispatcher (`tool_registry.h` / `tool_registry.cc`) ✅
  - Registry: `inline_vec<ToolDef, 32>` — name, description, parameters JSON, handler fn ptr
  - Dispatch by name: look up tool, parse arguments JSON, call handler
  - Tool result: `{ content: string, isError: bool }`

- [x] 3.4 Tool definitions (xcav tools) ✅
  - Registered: xcav_blocks, xcav_read, xcav_move, xcav_move_into, xcav_delete,
    xcav_replace, xcav_replace_block, xcav_undo, xcav_edit, xcav_copy, xcav_insert
  - Each calls the corresponding CmdXxx function from xcav directly
  - xcav core split into `xcav_lib` static library for reuse

- [x] 3.5 Tool definitions (system tools) ✅
  - `bash` — Subprocess::Run("bash", "-c", command), 30s timeout
  - `read_file` — FileReadFully(path), return content
  - `write_file` — write content to path, create dirs as needed

- [x] 3.6 Provider abstraction (`provider.h`)
  - Single header: `provider.h` declares DeepSeekProvider, OpenAIProvider, OllamaProvider, StubProvider
  - Shared OpenAI-compatible implementation in `provider_deepseek.cc` (DeepSeek + OpenAI)
  - Request includes `tools` field from tool registry
  - Response parsed from OpenAI `choices[0].message` format (content + tool_calls + finish_reason)
  - Auth: Bearer token from `DEEPSEEK_API_KEY` / `OPENAI_API_KEY` env vars

- [x] 3.7 JSON serialization (request body) ✅
  - BuildChatRequestBody in provider_deepseek.cc and provider_ollama.cc
  - Conversation array → JSON messages[], tools[] from GetToolDefs(), stream=false
  - All raw BufferBuilder-based, no external JSON library needed

- [x] 3.8 JSON parsing (response) ✅
  - ParseChatResponse in provider_deepseek.cc and provider_ollama.cc
  - JsonValue::TryString/TryBool/Object/Array for navigating OpenAI/Ollama responses
  - Error responses (4xx, 5xx) handled with readable messages
  - Malformed JSON → returns ProviderFinishReason::Error (no crash)

- [x] 3.9 System prompt and configuration ✅
  - Load system prompt from `~/.config/coding_agent/prompt.md` or embedded default
  - Include xcav tool descriptions dynamically from GetToolDefs()
  - Include project context (cwd + `find .` file tree summary)
  - Config expanded: modelName, maxToolIterations, maxTokens, temperature, apiBaseUrl override
  - system_prompt.h/cc module; apiBaseUrl wired into providers

- [x] 3.10 Display and UX ✅
  - Streaming deferred (non-streaming works fine for now)
  - Color output: green prompt, cyan tool names, green OK/red ERROR, dim token count
  - Tool calls shown as `[tool_name] OK` or `[tool_name] ERROR` with color
  - Token usage displayed per turn: `[N tokens]` (parsed from usage.total_tokens)
  - terminal_colors.h with ANSI escape constants

- [x] 3.11 Context window management ✅
  - Track approximate conversation tokens (bytes / 4) plus provider-reported `totalTokens`
  - Warn when approaching the configured context limit
  - Local auto-compaction: summarize early conversation into a system message, keep recent messages

- [ ] 3.12 Integration tests against Ollama (requires local ollama serve)
  - Basic text: "Hello" → model replies with text
  - Tool trigger: "List blocks in agent_loop.cc" → model calls `xcav_blocks`
  - Bash tool: "What files are in coding_agent/?" → model calls `bash ls`
  - Read file: "Show me the TODO" → model calls `read_file`
  - Multi-turn: tool call → result → model uses result in final response
  - Ollama not running → graceful error, no crash
  - Malformed/empty response → no crash
  - Location: `tests/coding_agent/` + `scripts/test_ca.sh` (run via `@scripts/test_ca.sh`)

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

- [ ] 4.2 Robust tool-call repair
  - Detect malformed tool-call JSON / bad arguments before dispatch
  - Try deterministic repair first (JSON unescape, missing braces, arguments object vs string)
  - Optional local-LLM repair path: produce valid tool name + arguments or a concise failure
  - Never silently execute repaired calls without logging what changed

- [ ] 4.3 Tool failure summarization
  - When a tool fails or returns huge output, optionally ask a local model to summarize:
    what was attempted, what failed, important output snippets, and next suggested action
  - Feed the summary to the main model instead of forcing it to re-run many tools
  - Preserve raw output when it is small; summarize only when useful

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

---

## Deliverables

```
coding_agent/
├── TODO.md                 ← this file
├── audit.md                ← Phase 1 findings
├── pi_parity.md            ← Phase 4 findings (future)
├── main.cc                 ← UserMain entry point
├── agent_loop.h
├── agent_loop.cc
├── tool_registry.h
├── tool_registry.cc
├── provider.h              ← shared provider declarations (DeepSeek, OpenAI, Ollama, Stub)
├── provider_deepseek.cc    ← DeepSeek + OpenAI implementations (shared OpenAI format)
├── provider_ollama.cc      ← Ollama local provider
├── provider_ollama.h       ← re-exports provider.h
├── system_prompt.h         ← BuildSystemPrompt — user prompt + tool descs + project context
├── system_prompt.cc
├── terminal_colors.h       ← ANSI terminal color constants
└── CMakeListsGenerated.txt

tests/coding_agent/ (tests — Phase 3.12):
├── test_ca.cc              ← e2e integration tests (requires Ollama)
└── CMakeLists.txt

scripts/test_ca.sh          ← runs tests (skips if Ollama not running)
```

---

## Decisions to make later

- Streaming vs non-streaming responses (start non-streaming)
- Raw TLS (mbedtls/openssl) vs curl subprocess for HTTP
- General JSON parser vs hand-rolled extractor (hand-rolled is fine for now)
- Windows support: stub or implement? (stub initially)
- Multi-turn tool calling loop — how many iterations before forcing user input? (cap at 25)
- How much pi parity is desirable vs keeping the native harness simpler
- Which local model is reliable enough for repair/summarization helper duties
