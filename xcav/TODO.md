# xcav — Java test coverage TODO

## Context

A developer proposed adding "contract-aware refactor views" to xcav for Java —
surfacing return types, throws clauses, annotations, and scope capture during
block operations to reduce refactor breakage.

**What we built** (CST-based, zero semantic analysis, honest about limits):
- ✅ Method signatures in `xcav blocks`: return type, param types+names, modifiers
- ✅ `throws` clause display (parse-only — does not distinguish checked vs unchecked)
- ✅ Annotations in `blocks` output (already existed, now regression-tested)
- ✅ Constructors as separate blocks (new in this change)

**What we explicitly didn't build** (and why):
- ❌ Checked vs unchecked exception resolution → requires classpath, not a parser job
- ❌ Return-path enumeration → "3 paths" is misleading without control-flow analysis
- ❌ Scope capture (field vs local) → requires symbol tables, not tree-sitter's domain
- ❌ "Optional compile metadata enrichment" → scope trap; ship what the parser can see
- ❌ Separate `xcav contract` command → the data belongs in `blocks` output, no extra step

**The developer's core ask** was: make refactor risk visible before the edit.
The signature+throws enhancement does that for the parse-visible portion of the contract.
The tests below are designed to prove this holds up under real-world Java patterns.

## Ultimate goal

A Java developer running `xcav blocks` on their project should see:

```
    15-  17 func public int add(int a, int b)
    27-  32 func public double divide(double a, double b) throws IOException
    34-  36 func public static void helper(String name)
     7-  11 func public Calculator(int base)
    41-  43 @Override func public String toString()
```

And trust that:
1. The signature shown is exactly what the parser sees — no guesses, no semantic lies
2. Move/delete/copy operations preserve annotations and throws metadata
3. Constructors, generics, interfaces, and enums all work identically
4. Cross-file moves (`move-into`) carry the full signature

If any of these break silently, the developer's refactor confidence breaks too.

## Rich fixture: `realistic.java`

Create `xcav/tests/fixtures/realistic.java` — one file exercising every Java
construct xcav claims to support. Target ~50-60 lines.

```java
// realistic.java — exercises all xcav Java features
package com.example;

import java.io.IOException;
import java.util.List;

public class DataService {

    // Constructor with throws
    public DataService(String configPath) throws IOException {
    }

    // Basic method
    public int compute(int a, int b) {
        return a + b;
    }

    // Throws declaration
    public double divide(double a, double b) throws ArithmeticException {
        return a / b;
    }

    // Static method
    public static void initialize() {
    }

    // Annotations + final modifier
    @Override
    public final String toString() {
        return "DataService";
    }

    // Multiple annotations, same line
    @Deprecated @SuppressWarnings("unchecked")
    public List<String> legacyMethod() {
        return null;
    }

    // Generic method
    public <T> T firstOrNull(List<T> items) {
        return items.isEmpty() ? null : items.get(0);
    }

    // Varargs
    public void log(String format, Object... args) {
    }

    // Abstract method — no body
    public abstract void validate();
}

interface DataProcessor {
    void process(byte[] data) throws IOException;

    // Default method
    default String name() {
        return "DataProcessor";
    }
}

enum Status {
    ACTIVE,
    INACTIVE;

    public boolean isActive() {
        return this == ACTIVE;
    }
}
```

### What this fixture exercises

| Construct | Line | xcav feature tested |
|---|---|---|
| Package + imports | 2-5 | Language detection, import filtering |
| Constructor with throws | 8 | `constructor_declaration`, `throws` in signature |
| Basic method | 12 | Baseline `method_declaration` |
| Throws declaration | 17 | `throws` clause in signature output |
| Static method | 22 | Modifier extraction (`static`) |
| Annotation + final | 27 | `@Override` annotation, `final` modifier |
| Multi-annotation same line | 32 | `@Deprecated @SuppressWarnings(...)` |
| Generic method | 38 | `<T>`, type parameter handling |
| Varargs | 43 | `spread_parameter` branch |
| Abstract method | 47 | No-body method (`;` not `{}`) |
| Interface | 51 | `interface_declaration`, default method |
| Enum with method | 60 | `enum_declaration`, enum body methods |
| Annotation on new line | 27-28 | Annotation preceding method on separate line |

### Annotation comment-style regression

Java annotations appear in two common styles:

```java
// Style A — annotation on same line as method declaration
@Override public int add(int a, int b) { ... }

// Style B — annotation on line above the method declaration
@Override
public int add(int a, int b) { ... }
```

Both styles are already handled correctly by xcav's annotation extraction
(walks `modifiers` and direct `annotation`/`marker_annotation` children
regardless of line position). The rich fixture includes both styles.
A regression test must verify they survive `move`, `delete`, `copy`, and
`move-into` without mangling.

## Test plan

### Phase 1 — Survey and read (proves signature accuracy)

- [ ] `blocks` on realistic.java → verify all 12+ blocks listed with correct
      signatures, annotations, and line ranges
- [ ] `read --name DataService` → returns the class, not the constructor
      (container preference)
- [ ] `read --name toString` → returns method with `@Override` annotation
- [ ] `read --name firstOrNull` → returns generic method
- [ ] `read --name process` → returns interface method with `throws IOException`
- [ ] `read --name Status` → returns enum
- [ ] `read --all` on realistic.java → output includes all methods/interfaces/enum

### Phase 2 — Single-file mutations (proves metadata survives edits)

- [ ] `move` legacyMethod before toString → annotations and modifiers preserved
- [ ] `move` DataService constructor after compute → constructor signature intact
- [ ] `delete` validate (abstract method) → clean removal, no leftover artifacts
- [ ] `delete` isActive (enum method) → enum structure preserved
- [ ] `edit` within divide body → throws clause unchanged in blocks output
- [ ] `replace-block` compute with new body → signature unchanged

### Phase 3 — Cross-file operations (proves metadata survives file boundaries)

Create `xcav/tests/fixtures/realistic_target.java` (minimal stub class).

- [ ] `copy` divide from realistic.java into target → signature including `throws`
      appears in target's blocks output
- [ ] `move-into` legacyMethod from realistic.java into target → annotations
      (`@Deprecated @SuppressWarnings(...)`) preserved in target
- [ ] `move-into` with `--copy-includes` → `java.io.IOException` import copied
      when moving method with throws clause

### Phase 4 — Edge cases (proves nothing breaks silently)

- [ ] Empty Java file (only package + imports) → `blocks` reports 0 blocks, no error
- [ ] Interface-only file → lists interface and its methods, no class label
- [ ] Enum-only file → lists enum and its methods
- [ ] File with only abstract methods → all listed as `func`, no body shown
- [ ] Constructor name collision with class → `--name` returns class, warns
      about ambiguity (already implemented, add regression)
- [ ] Method with type parameter `<T>` → `--name` still finds it by method name

### Phase 5 — Non-Java language backfill (nice to have)

- [ ] JavaScript: add `read`, `edit`, `move-into` tests (currently only blocks/move/delete)
- [ ] TypeScript: add blocks test for interfaces, type aliases, enums
- [ ] TSX: add at least one blocks test
- [ ] C: add cross-file move-into/copy tests

## What this does NOT test (and why)

| Feature | Reason |
|---|---|
| Checked vs unchecked exception resolution | Requires classpath; parser can't do it |
| Return-path completeness | Control-flow analysis; misleading without it |
| Field vs local variable capture | Symbol table; tree-sitter has no scoping |
| LSP-based enrichment | Separate project; no infra in xcav for subprocess/LSP |
| Compile-after-edit validation | Compiler's job, not xcav's |

These are explicitly documented as parse-only / best-effort. The `throws` output
shows what the source declares, not what the type system proves. The developer
is responsible for distinguishing `throws IOException` (checked, contract
obligation) from `throws IllegalArgumentException` (unchecked, style choice)
using their own Java knowledge.

## Estimated effort

- Rich fixture creation: 30 minutes
- Phase 1 (survey+read tests): 1 hour
- Phase 2 (single-file mutations): 1 hour
- Phase 3 (cross-file operations): 1 hour
- Phase 4 (edge cases): 1 hour
- Phase 5 (non-Java backfill): 2 hours
- **Total: ~1 day**
