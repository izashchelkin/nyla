# Commons Audit — Phase 1 Findings

Audit of `nyla/commons/` to find where apps bypass or duplicate commons abstractions.
Cross-referenced against `wm/`, `terminal/`, `breakout/`, `shipgame/`, `3d_ball_maze/`,
`screen_inhibitor/`, `git_ui/`, `asset_packer/`, `asset_tool/`, `psf2_glsl/`, and `xcav/`.

**Key insight**: `xcav/` is the closest existing tool to what the agent harness will be
(CLI, headless, deals with files + strings + subprocesses). Its escape hatches are
the most relevant signals.

---

## What apps do right ✅

All apps consistently use commons for:
- **File I/O**: `FileOpen`/`FileRead`/`FileWrite`/`FileClose`/`FileReadFully` — no raw `open`/`fopen` anywhere
- **Memory**: `region_alloc` / `RegionAlloc::Alloc<T>` / `AllocArray` — no ad-hoc malloc
- **Containers**: `inline_vec<T,N>` / `inline_string<N>` — no raw arrays with manual size tracking
- **String comparison**: `Span::Eq`, `Span::StartsWith`, `Span::EndsWith`, `MemEq`
- **Subprocess**: `Spawn()` (wm) and `RunSync()` (git_ui) used correctly

---

## Escape hatches: where apps bypass commons

### 1. `getenv()` — used raw in 4 places

| Location | Context |
|---|---|
| `wm/window_manager.cc:2112` | `getenv("NYLA_WM_NO_DAEMONS")` — feature flag |
| `xcav/backup.cc:20,84` | `getenv("HOME")` — find backup directory |
| `xcav/usage_log.cc:39` | `getenv("HOME")` — find usage log directory |
| `nyla/commons/platform_x11_linux.cc:83` | `getpid()` — platform impl, expected |

**Verdict**: Commons needs a `ReadEnvVar(name) -> Optional<byteview>` or equivalent.
Every app that needs env vars reaches for `<stdlib.h>`.

---

### 2. `stat()` — used raw in xcav for file/dir existence checks

| Location | What it does |
|---|---|
| `xcav/main.cc:135` | `stat() + S_ISDIR` — check if blocks arg is a directory |
| `xcav/block_query.cc:89` | `stat()` — check if file exists and is a regular file |
| `xcav/backup.cc:59` | `stat() + S_ISDIR` — check if `.git` exists |

**Verdict**: Commons needs `FileExists(path) -> bool` and `IsDirectory(path) -> bool`.
The `file_metadata` struct has `file_attribute::Directory` from `DirIter`, but
there's no stat-a-single-path API. xcav works around it with raw `<sys/stat.h>`.

---

### 3. `mkdir()` — used raw in xcav

| Location | What it does |
|---|---|
| `xcav/usage_log.cc:293,295` | `mkdir("~/.xcav/...", 0755)` — create usage log directories |

**Verdict**: Commons needs `CreateDirectory(path)` (and possibly recursive variant).
xcav creates log dirs with raw POSIX.

---

### 4. `unlink()` — used raw in xcav

| Location | What it does |
|---|---|
| `xcav/main.cc:501-503,1359,1370,1402` | `unlink()` — clean up temp files (stdin, old, new) |
| `xcav/backup.cc:232,305` | `unlink()` — remove old backup entries |

**Verdict**: Commons needs `FileDelete(path)`. xcav cleans up temp files with raw POSIX.

---

### 5. `getpid()` — used raw in xcav

| Location | What it does |
|---|---|
| `xcav/main.cc:392,1333` | Construct temp file names: `/tmp/xcav_edit_<pid>_old.txt` |
| `xcav/usage_log.cc:322` | Session ID in usage log events |

**Verdict**: Commons needs `GetProcessId() -> uint32_t`. xcav needs it for temp file
naming (no `mkstemp`-like utility exists either).

---

### 6. Manual JSON serialization with `snprintf` — xcav/usage_log.cc

xcav's usage logging builds JSONL records entirely by hand (~20 `snprintf` calls
across ~130 lines). A representative snippet:

```c
snprintf(buf, size,
    "{\"schema_version\":1,\"ts\":\"%.*s\",\"session_id\":\"%u-%.36s\","
    "\"seq\":%d,\"cmd\":\"%.*s\",\"cat\":\"%s\"", ...);
// ... then conditional fields with more snprintf, manual comma-tracking ...
```

This is the **most significant gap**. Commons has `json_parser` + `json_value` for
reading JSON, but **zero JSON serialization**. The one app that emits JSON had to
build it byte-by-byte with `snprintf` and manual comma/brace tracking.

The agent harness will need to build much more complex JSON (chat completions
request bodies with nested messages, tool definitions, parameters schemas).

---

### 7. Utility functions defined in xcav that belong in commons

| Function | File | Should be in |
|---|---|---|
| `ByteviewEq(byteview, const char*)` | `xcav/text_util.h` | `nyla/commons/span.h` |
| `StrEq(const char*, const char*)` | `xcav/text_util.h` | `nyla/commons/mem.h` or `span.h` |
| `LineStartOffset(source, line)` | `xcav/text_util.h` | xcav-specific (ok) |
| `LineEndOffset(source, line)` | `xcav/text_util.h` | xcav-specific (ok) |
| `LineIndent(source, lineStart)` | `xcav/text_util.h` | xcav-specific (ok) |
| `NormalizeText(inline_vec&)` | `xcav/text_util.h` | xcav-specific (ok) |

`ByteviewEq` and `StrEq` are generic and used heavily in xcav (20+ call sites each).
Apps that don't need C-string comparison against byteviews simply don't have this
pattern — but it would be useful for any CLI-like tool.

---

### 8. JSON parser limitations

The existing `json_parser` (used by `gltf.cc` for GLTF loading) has two issues:

1. **No escaped string support**: The backslash escape path is commented out
   (`//  && prevch != '\\'` in `ParseString`). LLM API responses are full of
   `\"`, `\\n`, `\\t` inside JSON strings.

2. **ASSERT on parse failure**: `ParseNext()` calls `ASSERT(false)` for unknown
   tokens. Not suitable for network input (malformed responses happen).

These aren't currently causing problems because no app parses untrusted JSON —
`gltf.cc` loads local asset files. But the harness will need a recoverable parser.

---

## What's missing entirely (no app workaround exists because no app needs it yet)

| Primitive | Why no app has tried |
|---|---|
| **HTTP client** | No app talks to the network (except X11 which has its own xcb transport) |
| **JSON writer/serializer** | Only xcav emits JSON, and it does so manually |
| **Temp file helper** | xcav constructs `/tmp/xcav_<pid>_<purpose>.txt` ad-hoc |
| **Deadline/timer** | `deadline` struct in `time.h` wraps `GetMonotonicTimeMillis()` |

---

## Summary of findings

### Gaps causing raw POSIX calls in apps (must-fix for cleanliness)

| Missing abstraction | Raw calls in | Fix |
|---|---|---|
| `ReadEnvVar(name)` | wm, xcav (2 files) | Add to `platform.h` |
| `FileExists(path)` | xcav/main.cc, block_query.cc | Add to `file.h` |
| `IsDirectory(path)` | xcav/main.cc, backup.cc | Add to `file.h` |
| `FileDelete(path)` | xcav/main.cc, backup.cc | Add to `file.h` |
| `CreateDirectory(path)` | xcav/usage_log.cc | Add to `file.h` |
| `GetProcessId()` | xcav/main.cc, usage_log.cc | Add to `platform.h` |
| `ByteviewEq(bv, cstr)` | xcav/text_util.h (local def) | Promote to `span.h` |
| `StrEq(cstr, cstr)` | xcav/text_util.h (local def) | Promote to `mem.h` |

### Gaps that xcav works around with manual code

| Missing abstraction | xcav's workaround | Severity |
|---|---|---|
| **JSON serialization** | ~20 `snprintf` calls, manual comma/brace tracking, custom `EscapeJson` | **Critical** — harness needs much more JSON |
| **JSON parser: escaped strings** | Not needed yet (xcav doesn't parse JSON) | **Critical** — harness parses API responses |
| **JSON parser: recoverable errors** | Not needed yet | **Critical** — harness parses network input |

### Gaps with no workaround yet (greenfield for harness)

| Primitive | Notes |
|---|---|
| HTTP client | No app has needed it. Curl subprocess via `RunSync` is the fastest path |
| Temp file helper | xcav ad-hoc approach works but is fragile |
| String Split / Trim / Find | No app currently needs them (xcav operates on whole blocks) |
| Deadline abstraction | `GetMonotonicTimeMillis()` exists, just needs thin wrapper |

---

## Recommendations

### Before building the harness (fix in commons)

1. **Add system wrappers**: `ReadEnvVar`, `FileExists`, `IsDirectory`, `FileDelete`,
   `CreateDirectory`, `GetProcessId` in `platform.h` / `file.h`. These are small,
   each is a 3-line wrapper around a POSIX call. xcav can then drop its `<stdlib.h>`
   and `<sys/stat.h>` includes.

2. **Promote `ByteviewEq` / `StrEq`** to commons. Remove the xcav-local definitions.

3. **Fix JSON parser**: add escaped string handling, add `TryParse` variant that
   returns errors instead of ASSERTing. This is critical for parsing API responses.

4. **Add JSON serialization**: a minimal writer that builds `{key:value,...}` from
   typed inputs. The harness needs this for request bodies; xcav's manual approach
   doesn't scale.

### For the harness (Phase 2+3 as planned)

5. **HTTP client**: start with curl subprocess via `RunSync`. Replace with raw TLS later.
6. **String helpers**: `Byteview::Find`, `Split`, `Trim` — add when needed during harness build.
7. **Temp file**: either add a `TempFile` helper or reuse xcav's ad-hoc pattern.
