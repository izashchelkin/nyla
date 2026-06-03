# Native Coding Agent — TODO

Three-phase plan to build a C++23 coding agent harness that talks to DeepSeek/OpenAI
and uses xcav for structural code editing. No pi, no JavaScript, no STL.

---

## Phase 1: Audit nyla/commons

**Goal**: Find every place where repo apps (wm, terminal, breakout, shipgame, etc.)
work around commons limitations. The harness will need HTTP, JSON, subprocesses,
string ops, file I/O — none of these should need escape hatches.

### Tasks

- [ ] 1.1 Survey subprocess usage
  - grep for `popen`, `fork`, `exec`, `system`, `posix_spawn` across all app dirs
  - check if `platform_linux.h` / `platform_windows.h` have pipe/subprocess wrappers
  - note any raw POSIX calls that should be behind commons abstractions

- [ ] 1.2 Survey string manipulation
  - grep for manual char-by-char parsing, `strstr`, `strchr`, `sprintf`, `snprintf`
  - check if `inline_string` / `byteview` / `span` cover all use cases
  - note missing ops: join, split, trim, starts_with, contains, replace, format

- [ ] 1.3 Survey file I/O
  - grep for `open`, `read`, `write`, `fopen`, `fread`, `fwrite`, `mmap`
  - check `platform_linux.h` file wrappers — are they sufficient?
  - note: temp files, atomic writes, directory listing, file exists

- [ ] 1.4 Survey JSON usage
  - find any JSON parsing/serialization (likely ad-hoc in WM serialize, test fixtures)
  - note: no general JSON in commons — this will be new work

- [ ] 1.5 Survey HTTP/network usage
  - find any HTTP calls, socket usage, curl invocations
  - check if there's anything reusable in commons or apps
  - note: this is almost certainly greenfield

- [ ] 1.6 Survey memory/arena patterns
  - check `region_alloc` usage across apps — any lifetime mismatches?
  - check if `inline_vec` / `array` capacity limits are painful anywhere
  - note patterns that the harness will need (growing buffers, temp allocations)

- [ ] 1.7 Survey error handling
  - grep for `ASSERT`, `TRAP`, `UNREACHABLE`, raw `exit()` across apps
  - check if any app needs recoverable error paths that commons doesn't provide
  - note: harness will need graceful error handling (API errors, timeouts, parse failures)

- [ ] 1.8 Document findings in `tools/coding-agent/audit.md`
  - list every escape hatch / missing primitive
  - classify each as: must-fix-for-harness, should-fix, nice-to-have

---

## Phase 2: Primitives in nyla/commons

**Goal**: Introduce (or prepare stubs for) every primitive the harness needs.
Some may be thin wrappers, some may be new. Windows compatibility can be stubbed.

### Tasks

- [ ] 2.1 Subprocess runner
  - `Subprocess::Run(command, stdin_data) -> { stdout, stderr, exit_code }`
  - Linux: popen wrapper. Windows: CreateProcess wrapper (can stub).
  - Timeout support (kill after N seconds).
  - Environment variable passthrough.
  - Location: `nyla/commons/subprocess.h` + platform impls.

- [ ] 2.2 HTTP client
  - `HttpClient::Post(url, headers, body) -> { status, body }`
  - Initial impl: subprocess wrapper around `curl -s`. Replace with raw TLS later.
  - Only needs POST with JSON bodies initially.
  - ApiKey loading from env var or file (`$DEEPSEEK_API_KEY`, `$OPENAI_API_KEY`).
  - Location: `nyla/commons/http_client.h` + platform impls.

- [ ] 2.3 JSON extractor (minimal)
  - NOT a general JSON parser. Extracts specific string/integer values by key path.
  - `JsonExtract(json, "choices[0].message.content") -> Optional<span<const char>>`
  - `JsonExtractInt(json, "usage.total_tokens") -> Optional<int>`
  - Handles nested objects, arrays, strings, numbers. Ignores everything else.
  - Handles escaped strings (\\", \\n, \\t, \\\\).
  - Does NOT handle: unicode escapes, arbitrary precision numbers, bool/null.
  - Location: `nyla/commons/json_extract.h`

- [ ] 2.4 String helpers (if Phase 1 finds gaps)
  - `Byteview::Find(string)`, `Byteview::StartsWith(string)`, `Byteview::Trim()`
  - `InlineString::Append(byteview)`, `InlineString::Format(...)` (printf-style)
  - `StringSplit(byteview, delimiter) -> inline_vec<span<const char>>`
  - Only add what Phase 1 proves is missing.

- [ ] 2.5 API key / secrets
  - `ReadEnvVar(name) -> Optional<span<const char>>`
  - `ReadFileToString(path) -> Optional<inline_string<N>>` (whole file slurp)
  - Location: may already exist, audit in Phase 1.

- [ ] 2.6 Simple timer / deadline
  - `Deadline::FromSeconds(n)` — for HTTP and subprocess timeouts
  - `Deadline::Expired()` — monotonic clock check
  - Location: `nyla/commons/time_util.h` or existing timer infra

- [ ] 2.7 Temp file helper (if not already in commons)
  - `TempFile::Create(prefix) -> path` — auto-cleans on destruction
  - Needed for xcav_edit (oldText/newText temp files)

- [ ] 2.8 Build integration
  - Each new commons file goes in `nyla/commons/CMakeListsGenerated.txt`
  - Platform files follow `*_linux.cc` / `*_windows.cc` convention
  - Windows stubs can be `TRAP("not implemented")` initially

---

## Phase 3: Harness

**Goal**: Build the agent loop in a new `tools/coding-agent/` directory.
Uses xcav CmdXxx functions directly (no shelling out). Talks to DeepSeek/OpenAI.

### Tasks

- [ ] 3.1 Project skeleton
  - `tools/coding-agent/main.cc` — UserMain entry point
  - `tools/coding-agent/CMakeLists.txt` + `CMakeListsGenerated.txt`
  - Root `CMakeLists.txt`: add `tools/coding-agent/` glob

- [ ] 3.2 Agent loop (`agent_loop.h` / `agent_loop.cc`)
  - Read user input from stdin (line-based, Ctrl-D to exit)
  - Maintain conversation array: `inline_vec<Message, 256>`
  - Message types: system, user, assistant, tool_result
  - Loop: user input → append → call model → handle response → repeat
  - Handle: text response (display, wait for input), tool_calls (execute, append results, loop)

- [ ] 3.3 Tool dispatcher (`tool_registry.h` / `tool_registry.cc`)
  - Registry: `inline_vec<ToolDef, 32>` — name, description, parameters JSON, handler fn ptr
  - Dispatch by name: look up tool, parse arguments JSON, call handler
  - Tool result: `{ content: string, isError: bool }`

- [ ] 3.4 Tool definitions (xcav tools)
  - Register: xcav_blocks, xcav_read, xcav_move, xcav_move_into, xcav_delete,
    xcav_replace, xcav_replace_scoped, xcav_undo, xcav_edit, xcav_copy
  - Each calls the corresponding CmdXxx function from xcav directly
  - CmdXxx functions need reusable signatures (may need minor refactoring from main.cc)
  - xcav_edit: write oldText/newText to temp files, call CmdEdit

- [ ] 3.5 Tool definitions (system tools)
  - `bash` — Subprocess::Run("bash", "-c", command), return stdout+stderr, truncated
  - `read_file` — ReadFileToString(path), return content
  - `write_file` — write content to path, create dirs as needed
  - `web_fetch` — curl a URL, return text (simple GET)
  - `web_search` — curl a search engine, return results (or stub with "use web_fetch")

- [ ] 3.6 Provider abstraction (`provider.h`)
  - Single interface: `Provider::Chat(messages, tools) -> Response`
  - Response: `{ text: optional<string>, tool_calls: optional<array<ToolCall>>, finish_reason, usage }`
  - DeepSeek impl: POST to `api.deepseek.com/v1/chat/completions`
  - OpenAI impl: POST to `api.openai.com/v1/chat/completions`
  - Auth: Bearer token from env var

- [ ] 3.7 JSON serialization (request body)
  - Build the chat completions JSON body: model, messages[], tools[], stream=false
  - Serialize conversation array to JSON
  - Serialize tool definitions to JSON (parameters schema from ToolDef)
  - This is the only JSON you need to WRITE — much simpler than parsing

- [ ] 3.8 JSON parsing (response)
  - Use `JsonExtract` from Phase 2 to pull: content, tool_calls[], finish_reason, usage
  - Handle error responses (4xx, 5xx) with readable messages
  - Handle malformed JSON gracefully (provider bugs happen)

- [ ] 3.9 System prompt and configuration
  - Load system prompt from `~/.config/coding-agent/prompt.md` or embedded default
  - Include xcav tool descriptions in prompt
  - Include project context (cwd, file tree summary)
  - Config: model name, API base URL, max tokens, temperature

- [ ] 3.10 Display and UX
  - Stream assistant text to stdout as it arrives (optional — start with non-streaming)
  - Color output? Simple `\033[...m` escapes or just plain text
  - Show tool calls as they execute (e.g., `[xcav_blocks foo.cc] ...`)
  - Show token usage after each turn

- [ ] 3.11 Context window management (future)
  - Track token counts, warn when approaching limit
  - Auto-compaction: summarize early conversation, keep recent messages
  - Can be post-MVP

---

## Deliverables

```
tools/coding-agent/
├── TODO.md            ← this file
├── audit.md           ← Phase 1 findings
├── main.cc            ← UserMain entry point
├── agent_loop.h
├── agent_loop.cc
├── tool_registry.h
├── tool_registry.cc
├── provider.h
├── provider_deepseek.cc
├── provider_openai.cc
├── json_serialize.h   ← request body builder
├── json_serialize.cc
├── display.h          ← terminal output
├── display.cc
└── CMakeListsGenerated.txt

nyla/commons/ (new or modified):
├── subprocess.h + _linux.cc + _windows.cc (stub)
├── http_client.h + _linux.cc + _windows.cc (stub)
├── json_extract.h
├── string_util.h      (if Phase 1 finds gaps)
└── time_util.h        (if deadline not already available)
```

---

## Decisions to make later

- Streaming vs non-streaming responses (start non-streaming)
- Raw TLS (mbedtls/openssl) vs curl subprocess for HTTP
- General JSON parser vs hand-rolled extractor (hand-rolled is fine for now)
- Windows support: stub or implement? (stub initially)
- Multi-turn tool calling loop — how many iterations before forcing user input? (cap at 25)
