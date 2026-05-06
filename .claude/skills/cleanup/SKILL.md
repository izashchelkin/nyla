---
name: cleanup
description: Sweep nyla/commons for AI-slop tripwires — duplicated helpers, manual span construction, dead code, naming drift, half-finished refactors. Trigger when user says "clean up", "cleanup pass", "review for slop", "fix slop", or "/cleanup". Auto-trigger when editing files that hit any tripwire below — fix it in the same pass, do not defer.
---

# Cleanup pass for nyla

## Posture

The codebase accumulates AI-generated slop from rapid iteration. Before adding new surface area, kill what is already broken. Cleanup is not a separate ticket — it happens whenever a tripwire below is violated in code you are touching.

A "cleanup pass" means: pick one tripwire, grep the whole `nyla/commons` for it, fix every hit, build between renames, then move to the next tripwire.

**Auto-trigger scope:** when this skill auto-triggers during ordinary editing, fix only the single tripwire you encountered in the file you are editing. Do not expand to grep the rest of `nyla/commons` — that is for explicit invocation.

**Build invocation:** `ninja -C build/linux-debug` (cap output to ~20 lines per CLAUDE.md token economy).

## Tripwires

Each tripwire is a pattern + a fix. If you see the pattern, fix it.

### 1. Half-finished refactors

A helper got deleted but a call site was missed. A struct got renamed in some places but not others. An unused identifier was removed but its include or forward-declare lingered.

**Find:** when you delete a helper, `grep -rn '<helper-name>' nyla --include='*.cc' --include='*.h'` to verify zero hits before the build. After any rename, build immediately.

### 2. Local helpers that duplicate `nyla/commons` primitives

If a local `CopyByteview` / `StoreName` / `AsCode` / similar exists in a `.cc` file and the same job is already done by `region_alloc.h` / `inline_string.h` / `span.h`, delete the local copy and call the primitive. Add a primitive to `nyla/commons` only when the same pattern appears in three or more places.

Known primitives:

- `RegionAlloc::CopyByteView(alloc, src) -> byteview` — copy bytes, no null term.
- `RegionAlloc::CopyByteViews(alloc, srcs...) -> span<uint8_t>` — variadic concat, no null term.
- `RegionAlloc::CopyByteViews(alloc, cstr_term, srcs...) -> byteview` — concat + null term, size excludes null.
- `InlineString::Assign(self, src)` — clamp-to-capacity + memcpy + set size.
- `Span::Cast<K>(span<T>)` — re-typed span, size adjusted.
- `Span::FromCStr(ptr, maxLen)` — null-terminated cstr → byteview.

### 3. Manual span construction with size division

```cpp
span<T>{(T*)x.data, x.size / sizeof(T)}
```

→ `Span::Cast<T>(x)`. The wrapper exists; use it.

### 4. Manual `alloc + memcpy + wrap` of byteviews

Three lines that allocate from a region, memcpy bytes in, and wrap a byteview around them. Replace with `RegionAlloc::CopyByteView` (no null), `CopyByteViews(alloc, cstr_term, srcs...)` (concat + null term), or `CopyByteViews(alloc, srcs...)` (pure concat).

### 5. Manual `clamp + memcpy + set size` on `inline_string`

```cpp
uint64_t n = Min<uint64_t>(src.size, Capacity);
if (n) MemCpy(dst.data.data, src.data, n);
dst.size = n;
```

→ `InlineString::Assign(dst, src)`.

### 6. Singleton naming drift

Singleton state in `nyla/commons` lives behind a file-scope `manager` pointer. `g_xxx` is the older convention. When touching the file, rename to `manager`.

Find offenders: `grep -rEn '^[a-z_]+_state \*g_' nyla/commons --include='*.cc'`.

Exceptions (deliberate `g_` retention): `engine.cc::g_engine`, `region_alloc.cc::g_BootstrapAlloc`.

### 7. Dead null-checks and ASSERTs

ASSERTs that guard invariants the engine already enforces (e.g., a subsystem's `Bootstrap` is unconditional in `Engine::Bootstrap`, so its `manager` pointer is never null at runtime). Drop them — they don't catch real bugs and they hide intent.

`Span::CStr` and other dev-loop hot paths should use `DASSERT` not `ASSERT`. Release builds shouldn't pay the check.

### 8. Dead-code blocks and AI artifacts

Kill on sight in the file you are touching:

- `#if 0` blocks. Default = remove. Keep only if block has named reason inline (TODO with date + owner). Git history preserves prior attempts; don't hoard via `#if 0`.
- Struct fields written but never read.
- Self-assignment no-ops (`else ch = ch;`).
- Header functions with zero callers across the repo (grep first).
- Trailing-newline missing on diff files (editor artifact; repo expects trailing `\n`). Vendored headers (`stb_image.h`, `renderdoc_app.h`) excluded.

### 9. Comments narrating WHAT not WHY

Per `CLAUDE.md`: comments explain non-obvious WHY (a hidden constraint, a workaround, a surprising invariant). Drop comments that restate what the function name and body already convey.

## Sweep methodology

When user invokes the skill explicitly ("clean up", "/cleanup"):

1. Pick one tripwire from §1-§9. Default: the topmost violation visible in the current `git status` diff. Otherwise whichever surfaced most recently.
2. `grep -rn` the pattern across `nyla/commons`. Cross-check the §-local exception list before acting on a hit.
3. Fix every hit in one pass. Build (`ninja -C build/linux-debug`) between renames.
4. Surface ambiguous cases to the user before acting (don't silently rename a singleton if the file pattern is unusual).
5. Move to the next tripwire only after the current one is clean.
6. After the sweep, report what was changed and which "Known pending" instances remain.

## Out of scope

- Refactors that change behavior. Cleanup preserves semantics.
- Adding new abstractions. Promote a local helper to `nyla/commons` only when the same pattern appears 3+ times.
- Hot-reload / feature work. That belongs in `ITERATION.md`'s "What's pending" queue.
- Code outside `nyla/commons` unless the same tripwire appears there too. Apps (`shipgame`, `breakout`, etc.) follow the same rules but are lower priority.
