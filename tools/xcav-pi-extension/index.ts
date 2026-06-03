/**
 * xcav — Structural code editing through tree-sitter
 *
 * Pure passthrough wrapper around the `xcav` CLI binary.
 * All logic lives in C++; TypeScript handles Pi integration only.
 *
 * Tools:
 *   xcav_blocks     — list structural blocks
 *   xcav_read       — read files (entire file un-indented, or structural with --line/--name)
 *   xcav_move       — move a block within a file
 *   xcav_move_into  — move a block across files
 *   xcav_delete     — delete a block
 *   xcav_replace    — replace an entire block with new content (replace-block)
 *   xcav_replace_scoped — scoped replace within a block (oldText unique in block only)
 *   xcav_undo       — restore from backup (up to 20 levels)
 *   xcav_edit       — safe edit (tree-sitter validated for C/C++/Java/TS/JS, plain for others)
 *   xcav_copy       — copy a block across files (with --copy-includes, --show-returns)
 */

import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { Type } from "typebox";
import { execSync } from "node:child_process";
import { existsSync, writeFileSync, mkdirSync, unlinkSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve, join, extname } from "node:path";
import { randomBytes } from "node:crypto";

// ─── Helpers ────────────────────────────────────────────────────────────────

const XCAV_BIN = "/usr/local/bin/xcav";

function xcav(args: string[]): { output: string; ok: boolean } {
  const cmd = [XCAV_BIN, ...args].map((a) => `'${a.replace(/'/g, "\\'")}'`).join(" ") + " 2>&1";
  try {
    const merged = execSync(cmd, { encoding: "utf-8", timeout: 30_000 });
    return { output: merged.trim(), ok: true };
  } catch (e: any) {
    const out = (e.stdout?.toString() ?? "").trim();
    const err = (e.stderr?.toString() ?? e.message ?? "unknown error").trim();
    return { output: [out, err].filter(Boolean).join("\n"), ok: false };
  }
}

/** Format tool args for display in output. Shows exactly what the model provided. */
function fmtArgs(toolName: string, params: Record<string, unknown>): string {
  const entries = Object.entries(params)
    .filter(([, v]) => v !== undefined && v !== null)
    .map(([k, v]) => {
      if (typeof v === "string") {
        // Truncate long string params (e.g. xcav_replace content) to avoid noise
        if (v.length > 80) return `${k}="${v.slice(0, 77)}..."`;
        return `${k}="${v}"`;
      }
      return `${k}=${v}`;
    });
  return `${toolName}(${entries.join(", ")})`;
}

/** Wrap raw xcav output with args header so the agent sees what was passed. */
function wrapOutput(toolName: string, params: Record<string, unknown>, rawOutput: string): string {
  const header = fmtArgs(toolName, params);
  if (!rawOutput) return header;
  return `${header}\n────────────────────────────────────────────────\n${rawOutput}`;
}

const C_CPP_JAVA_EXTS = new Set([
  ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hxx", ".hh", ".java",
  ".js", ".jsx", ".mjs", ".cjs",
  ".ts", ".tsx", ".mts", ".cts",
]);

function isCppOrJava(filePath: string): boolean {
  const ext = extname(filePath).toLowerCase();
  return C_CPP_JAVA_EXTS.has(ext);
}

function tmpPath(): string {
  return join(tmpdir(), `xcav_${randomBytes(8).toString("hex")}`);
}

// ─── Tools ─────────────────────────────────────────────────────────────────

export default function (pi: ExtensionAPI) {

  // ── Skill auto-discovery ──────────────────────────────────────────────

  pi.on("resources_discover", async (_event, _ctx) => {
    return {
      skillPaths: [resolve(__dirname, "skills")],
    };
  });

  // ── xcav_blocks ─────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_blocks",
    label: "xcav blocks",
    description:
      "List structural blocks in a C/C++/Java/TypeScript/JavaScript file. Shows function definitions, " +
      "struct/class/enum definitions, and other top-level constructs. " +
      "Returns block type, 1-indexed line range, and index for use with xcav_move/xcav_delete.",
    promptSnippet: "xcav_blocks(file) — list structural blocks in a C/C++/Java/TS/JS file",
    promptGuidelines: [
      "Use xcav_blocks before xcav_move/xcav_delete to survey file structure and find the right line numbers.",
      "Standard xcav workflow: 1) xcav_blocks to survey structure, 2) xcav_read to inspect code, 3) xcav_move/xcav_delete/xcav_replace to modify, 4) xcav_undo to recover from mistakes.",
    ],
    parameters: Type.Object({
      file: Type.String({ description: "Path to the C/C++/Java/TypeScript/JavaScript source file" }),
    }),
    async execute(_toolCallId, params) {
      const absPath = resolve(params.file);
      if (!existsSync(absPath)) {
        return { content: [{ type: "text", text: `File not found: ${params.file}` }] };
      }
      const r = xcav(["blocks", absPath]);
      return { content: [{ type: "text", text: wrapOutput("xcav_blocks", params, r.output || "(no blocks)") }] };
    },
  });

  // ── xcav_read ──────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_read",
    label: "xcav read",
    description:
      "Read files. Default (no flags) prints the entire file un-indented. " +
      "--all also prints the entire file. --line reads a specific block (C/C++/Java/TS/JS). " +
      "--name finds a block by structural name. --offset/--limit for line ranges. " +
      "--raw for exact indentation. --numbers for line numbers. " +
      "Large files (>500 lines or >50KB) are auto-truncated with a warning.",
    promptSnippet: "xcav_read(file, line?, name?, all?, numbers?, offset?, limit?) — read files",
    promptGuidelines: [
      "Use xcav_read for all file reading. It replaces the regular read tool.",
      "Default output (no flags) prints the entire file un-indented. Use --raw for exact indentation.",
      "For C/C++/Java/TypeScript/JavaScript: use line/name for structural reading (shows function paths, un-indents).",
      "Un-indented output can be used directly as oldText for xcav_edit — leading whitespace is auto-matched.",
      "For any file: use offset/limit for plain reading (like the regular read tool).",
      "Large files (>500 lines, >50KB) are auto-truncated. Use --offset/--limit to read beyond the first 500 lines.",
      "Line numbers are 1-indexed (as shown by xcav_blocks).",
      "Use --numbers when you need to reference specific lines in subsequent move/delete operations.",
    ],
    parameters: Type.Object({
      file:    Type.String({ description: "Path to the file to read (relative or absolute)" }),
      line:    Type.Optional(Type.Number({ description: "Line number (1-indexed) of the block to read structurally (C/C++/Java/TS/JS only)" })),
      name:    Type.Optional(Type.String({ description: "Structural name to find, e.g. 'foo' or 'Point::GetX' (C/C++/Java/TS/JS only)" })),
      all:     Type.Optional(Type.Boolean({ description: "Print entire file content (same as default). For large files, auto-truncates to 500 lines.", default: false })),
      numbers: Type.Optional(Type.Boolean({ description: "Include original line numbers in output", default: false })),
      offset:  Type.Optional(Type.Number({ description: "Line number to start reading from (1-indexed, plain mode)" })),
      limit:   Type.Optional(Type.Number({ description: "Maximum number of lines to read (plain mode)" })),
    }),
    async execute(_toolCallId, params) {
      const absPath = resolve(params.file);
      if (!existsSync(absPath)) {
        return { content: [{ type: "text", text: `File not found: ${params.file}` }] };
      }

      const args: string[] = ["read", absPath];

      if (params.all) {
        args.push("--all");
      } else if (params.name) {
        args.push("--name", params.name);
      } else if (params.line != null) {
        args.push(String(params.line));
      }

      if (params.offset != null) {
        args.push("--offset", String(params.offset));
      }
      if (params.limit != null) {
        args.push("--limit", String(params.limit));
      }
      if (params.numbers) {
        args.push("--numbers");
      }

      const r = xcav(args);
      return { content: [{ type: "text", text: wrapOutput("xcav_read", params, r.output || "(no output)") }] };
    },
  });

  // ── xcav_move ───────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_move",
    label: "xcav move",
    description:
      "Move a structural code block (function, struct, class, etc.) to a new location within " +
      "the same file. Uses tree-sitter for safe block detection. Automatically re-indents. " +
      "Supports C, C++, Java, TypeScript, and JavaScript.",
    promptSnippet: "xcav_move(file, line, destLine) — move a code block to a new location",
    promptGuidelines: [
      "Use xcav_move for block-level code moves instead of manual cut-and-paste.",
      "The destination must be a block boundary (closing brace, file start/end). Never pick a line inside a function body.",
    ],
    parameters: Type.Object({
      file: Type.String({ description: "Path to the source file" }),
      line: Type.Number({ description: "Line number of the block to move (1-indexed)" }),
      destLine: Type.Number({ description: "Destination line (block moved after this line, 1-indexed)" }),
    }),
    async execute(_toolCallId, params) {
      const absPath = resolve(params.file);
      if (!existsSync(absPath)) {
        return { content: [{ type: "text", text: `File not found: ${params.file}` }] };
      }
      const r = xcav(["move", absPath, String(params.line), String(params.destLine)]);
      return { content: [{ type: "text", text: wrapOutput("xcav_move", params, r.output || "Move succeeded.") }] };
    },
  });

  // ── xcav_move_into ─────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_move_into",
    label: "xcav move-into",
    description:
      "Move a structural code block from one file to another. Uses tree-sitter for safe block " +
      "detection. Automatically re-indents. Supports --copy-includes to copy #include/import lines. " +
      "Supports C, C++, Java, TypeScript, and JavaScript.",
    promptSnippet: "xcav_move_into(srcFile, srcLine, dstFile, dstLine, copyIncludes?) — cross-file move",
    promptGuidelines: [
      "Use xcav_move_into for cross-file block moves. Supports --copy-includes.",
      "The destination must be a block boundary (closing brace, file start/end).",
    ],
    parameters: Type.Object({
      srcFile:      Type.String({ description: "Path to the source file" }),
      srcLine:      Type.Number({ description: "Line number of the block to move (1-indexed)" }),
      dstFile:      Type.String({ description: "Path to the destination file" }),
      dstLine:      Type.Number({ description: "Destination line (block moved after this line, 1-indexed)" }),
      copyIncludes: Type.Optional(Type.Boolean({ description: "Copy #include/import lines from source to dest", default: false })),
    }),
    async execute(_toolCallId, params) {
      const srcAbs = resolve(params.srcFile);
      const dstAbs = resolve(params.dstFile);
      if (!existsSync(srcAbs)) {
        return { content: [{ type: "text", text: `Source file not found: ${params.srcFile}` }] };
      }
      if (!existsSync(dstAbs)) {
        return { content: [{ type: "text", text: `Destination file not found: ${params.dstFile}` }] };
      }
      const args = ["move-into", srcAbs, String(params.srcLine), dstAbs, String(params.dstLine)];
      if (params.copyIncludes) args.push("--copy-includes");
      const r = xcav(args);
      return { content: [{ type: "text", text: wrapOutput("xcav_move_into", params, r.output || "Move-into succeeded.") }] };
    },
  });

  // ── xcav_delete ─────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_delete",
    label: "xcav delete",
    description:
      "Delete a structural code block (function, struct, class, comment block, include line) " +
      "from a C/C++/Java/TypeScript/JavaScript file. Uses tree-sitter for safe block detection. Cleans up surrounding " +
      "whitespace. Supports undo via xcav_undo.",
    promptSnippet: "xcav_delete(file, line) — safely delete a code block",
    promptGuidelines: [
      "Use xcav_delete for block-level deletions instead of manual text removal.",
    ],
    parameters: Type.Object({
      file: Type.String({ description: "Path to the source file" }),
      line: Type.Number({ description: "Line number of the block to delete (1-indexed)" }),
    }),
    async execute(_toolCallId, params) {
      const absPath = resolve(params.file);
      if (!existsSync(absPath)) {
        return { content: [{ type: "text", text: `File not found: ${params.file}` }] };
      }
      const r = xcav(["delete", absPath, String(params.line)]);
      return { content: [{ type: "text", text: wrapOutput("xcav_delete", params, r.output || "Delete succeeded.") }] };
    },
  });

  // ── xcav_replace ────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_replace",
    label: "xcav replace",
    description:
      "Replace a structural code block with new content. Uses tree-sitter for block detection. " +
      "Atomic C++ implementation — safe for last-block-in-namespace. " +
      "Supports undo via xcav_undo.",
    promptSnippet: "xcav_replace(file, line, content) — replace a code block with new content",
    promptGuidelines: [
      "Use xcav_replace when replacing an entire function/struct/class with new code.",
      "For small text replacements within a block, use xcav_edit instead.",
    ],
    parameters: Type.Object({
      file: Type.String({ description: "Path to the source file" }),
      line: Type.Number({ description: "Line number of the block to replace (1-indexed)" }),
      content: Type.String({ description: "New content to insert in place of the deleted block" }),
    }),
    async execute(_toolCallId, params) {
      const absPath = resolve(params.file);
      if (!existsSync(absPath)) {
        return { content: [{ type: "text", text: `File not found: ${params.file}` }] };
      }
      // Write content to temp file for the C++ binary
      const tmpDir = join("/tmp", "xcav_tmp");
      mkdirSync(tmpDir, { recursive: true });
      const tmpPath = join(tmpDir, `replace_${Date.now()}_${Math.random().toString(36).slice(2, 8)}.txt`);
      writeFileSync(tmpPath, params.content, "utf-8");
      const r = xcav(["replace-block", absPath, String(params.line), tmpPath]);
      try { unlinkSync(tmpPath); } catch {}
      return { content: [{ type: "text", text: wrapOutput("xcav_replace", params, r.output || "Replace succeeded.") }] };
    },
  });

  // ── xcav_replace_scoped ─────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_replace_scoped",
    label: "xcav replace-scoped",
    description:
      "Scoped text replacement — oldText only needs to be unique within the structural block " +
      "containing <line>. Unlike xcav_edit (which requires file-wide uniqueness) and xcav_replace " +
      "(which replaces the entire block), this replaces specific text within a known block. " +
      "Supports C, C++, Java, TypeScript, and JavaScript.",
    promptSnippet: "xcav_replace_scoped(file, line, oldText, newText) — replace text scoped to a block",
    promptGuidelines: [
      "Use xcav_replace_scoped when oldText appears in multiple blocks but you only want to change one instance.",
      "Prefer xcav_edit when oldText is file-unique; prefer xcav_replace when replacing an entire block.",
    ],
    parameters: Type.Object({
      file: Type.String({ description: "Path to the source file" }),
      line: Type.Number({ description: "Line number within the block to scope matching (1-indexed)" }),
      oldText: Type.String({ description: "Text to find within the block (only needs uniqueness within the block)" }),
      newText: Type.String({ description: "Replacement text" }),
    }),
    async execute(_toolCallId, params) {
      const absPath = resolve(params.file);
      if (!existsSync(absPath)) {
        return { content: [{ type: "text", text: `File not found: ${params.file}` }] };
      }
      const tmpDir = join("/tmp", "xcav_tmp");
      mkdirSync(tmpDir, { recursive: true });
      const ts = `${Date.now()}_${Math.random().toString(36).slice(2, 6)}`;
      const oldFile = join(tmpDir, `scoped_old_${ts}.txt`);
      const newFile = join(tmpDir, `scoped_new_${ts}.txt`);
      writeFileSync(oldFile, params.oldText, "utf-8");
      writeFileSync(newFile, params.newText, "utf-8");
      const r = xcav(["replace", absPath, String(params.line), oldFile, newFile]);
      try { unlinkSync(oldFile); unlinkSync(newFile); } catch {}
      return { content: [{ type: "text", text: wrapOutput("xcav_replace_scoped", params, r.output || "Replace succeeded.") }] };
    },
  });

  // ── xcav_undo ───────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_undo",
    label: "xcav undo",
    description:
      "Undo the last xcav operation (move, delete, or replace) on a file. Restores from " +
      "the most recent backup. Can be called multiple times (up to 20 levels of undo per file).",
    promptSnippet: "xcav_undo(file) — undo the last xcav operation on a file",
    promptGuidelines: [
      "Use xcav_undo immediately if an xcav operation produces unexpected results.",
      "Can be called multiple times (up to 20 levels of undo per file).",
    ],
    parameters: Type.Object({
      file: Type.String({ description: "Path to the file to undo" }),
    }),
    async execute(_toolCallId, params) {
      const absPath = resolve(params.file);
      const r = xcav(["undo", absPath]);
      return { content: [{ type: "text", text: wrapOutput("xcav_undo", params, r.output || "Undo succeeded.") }] };
    },
  });

  // ── xcav_edit ─────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_edit",
    label: "xcav edit",
    description:
      "Safely edit files. For C/C++/Java/TypeScript/JavaScript: line-based matching that " +
      "copies indentation from matched file lines to the replacement. Whitespace-insensitive. " +
      "Unicode normalization (em-dash→--, arrows→->/<-, smart quotes→ASCII). " +
      "For all other files: plain string replacement (sequential, matched against current file state). " +
      "Same API as regular edit tool. Replaces the built-in edit tool for all file types.",
    promptSnippet:
      "xcav_edit(file, edits) -- safe edit (line-based matching for C/C++/Java/TS/JS, plain for others)",
    promptGuidelines: [
      "Use xcav_edit for ALL file edits. It replaces the regular edit tool.",
      "For C/C++/Java/TypeScript/JavaScript files, xcav_edit does whitespace-insensitive line matching.",
      "For non-C/C++/Java/TypeScript/JavaScript files, xcav_edit does plain string replacement.",
      "Use xcav_edit for all edits -- even single-line changes.",
      "For whole-function/struct/class replacements, use xcav_replace instead of xcav_edit.",
    ],
    parameters: Type.Object({
      file: Type.String({ description: "Path to the file to edit (relative or absolute)" }),
      edits: Type.Array(
        Type.Object({
          oldText: Type.String({
            description:
              "Exact text for one targeted replacement. It must be unique in the original file " +
              "and must not overlap with any other edits[].oldText in the same call.",
          }),
          newText: Type.String({ description: "Replacement text for this targeted edit." }),
        }),
        {
          description:
            "One or more targeted replacements. Each edit is matched against the original file, " +
            "not incrementally. Do not include overlapping or nested edits.",
        }
      ),
    }),
    async execute(_toolCallId, params, _signal) {
      const { file, edits } = params;

      if (!existsSync(file)) {
        return {
          content: [{ type: "text", text: `xcav_edit: file not found: ${file}` }],
          isError: true,
          details: {},
        };
      }

      const absPath = resolve(file);

      // ── Non-C/C++/Java: plain string replacement ─────────────────────
      if (!isCppOrJava(file)) {
        let source = readFileSync(absPath, "utf-8");
        const results: string[] = [];
        let allOk = true;

        for (let i = 0; i < edits.length; i++) {
          const { oldText, newText } = edits[i];

          let count = 0;
          let matchPos = -1;
          let idx = 0;
          while ((idx = source.indexOf(oldText, idx)) !== -1) {
            matchPos = idx;
            count++;
            idx += oldText.length;
          }

          if (count === 0) {
            allOk = false;
            results.push(`Edit ${i + 1}/${edits.length} FAILED: oldText not found in file`);
            break;
          }
          if (count > 1) {
            allOk = false;
            results.push(
              `Edit ${i + 1}/${edits.length} FAILED: oldText found ${count} times -- ambiguous, use more context`
            );
            break;
          }
          source =
            source.slice(0, matchPos) +
            newText +
            source.slice(matchPos + oldText.length);
          results.push(`Edit ${i + 1}/${edits.length}: applied.`);
        }

        if (allOk) {
          writeFileSync(absPath, source, "utf-8");
        }

        return {
          content: [{ type: "text", text: results.join("\n") }],
          isError: !allOk,
          details: {},
        };
      }

      // ── C/C++/Java/TS/JS: line-based matching via xcav binary ─────
      // Falls back to plain text replacement if xcav line matching fails
      // (e.g. grammar mismatch for .h files with C++ features, or raw string
      // literals that confuse the C parser).

      const results: string[] = [];
      let allOk = true;

      for (let i = 0; i < edits.length; i++) {
        const { oldText, newText } = edits[i];

        const oldFile = tmpPath();
        const newFile = tmpPath();

        try {
          writeFileSync(oldFile, oldText, "utf-8");
          writeFileSync(newFile, newText, "utf-8");

          const stdout = execSync(
            `${XCAV_BIN} edit "${absPath}" "${oldFile}" "${newFile}"`,
            {
              encoding: "utf-8",
              stdio: ["pipe", "pipe", "pipe"],
              timeout: 30000,
            }
          );

          results.push(`Edit ${i + 1}/${edits.length}: ${stdout.trim()}`);
        } catch (err: any) {
          const stderr = err.stderr?.toString() || err.message;

          // ── Fallback: xcav line matching failed, retry with plain string match
          const source = readFileSync(absPath, "utf-8");
          let count = 0;
          let matchPos = -1;
          let idx = 0;
          while ((idx = source.indexOf(oldText, idx)) !== -1) {
            matchPos = idx;
            count++;
            idx += oldText.length;
          }

          if (count === 0) {
            allOk = false;
            results.push(
              `Edit ${i + 1}/${edits.length} FAILED: oldText not found in file (both xcav and plain text)`
            );
          } else if (count > 1) {
            allOk = false;
            results.push(
              `Edit ${i + 1}/${edits.length} FAILED: oldText found ${count} times in file (both xcav and plain text) -- ambiguous`
            );
          } else {
            const newSource =
              source.slice(0, matchPos) +
              newText +
              source.slice(matchPos + oldText.length);
            writeFileSync(absPath, newSource, "utf-8");
            const xcavMsg = stderr.trim().split("\n")[0];
            results.push(
              `Edit ${i + 1}/${edits.length}: applied via plain text (xcav edit: ${xcavMsg})`
            );
          }
        } finally {
          try { unlinkSync(oldFile); } catch {}
          try { unlinkSync(newFile); } catch {}
        }
      }

      return {
        content: [{ type: "text", text: results.join("\n") }],
        isError: !allOk,
        details: {},
      };
    },
  });

  // ── xcav_copy ─────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_copy",
    label: "xcav copy",
    description:
      "Copy a structural code block from one file to another. Source is unaffected. " +
      "Uses tree-sitter for safe block detection. Automatically re-indents. " +
      "Supports --copy-includes to copy #include/import lines and --show-returns to " +
      "print return statement locations. Supports C, C++, Java, TypeScript, and JavaScript.",
    promptSnippet: "xcav_copy(srcFile, srcLine, dstFile, dstLine, copyIncludes?, showReturns?) — copy a block across files",
    promptGuidelines: [
      "Use xcav_copy to duplicate code blocks across files without cutting the source.",
      "Combine with xcav_delete after copying to achieve an extract-to-new-file pattern.",
      "The destination must be a block boundary (closing brace, file start/end).",
    ],
    parameters: Type.Object({
      srcFile:      Type.String({ description: "Path to the source file" }),
      srcLine:      Type.Number({ description: "Line number of the block to copy (1-indexed)" }),
      dstFile:      Type.String({ description: "Path to the destination file" }),
      dstLine:      Type.Number({ description: "Destination line (block copied after this line, 1-indexed)" }),
      copyIncludes: Type.Optional(Type.Boolean({ description: "Copy #include/import lines from source to dest", default: false })),
      showReturns:  Type.Optional(Type.Boolean({ description: "Print line numbers of return statements in the copied block", default: false })),
    }),
    async execute(_toolCallId, params) {
      const srcAbs = resolve(params.srcFile);
      const dstAbs = resolve(params.dstFile);
      if (!existsSync(srcAbs)) {
        return { content: [{ type: "text", text: `Source file not found: ${params.srcFile}` }] };
      }
      if (!existsSync(dstAbs)) {
        return { content: [{ type: "text", text: `Destination file not found: ${params.dstFile}` }] };
      }
      const args = ["copy", srcAbs, String(params.srcLine), dstAbs, String(params.dstLine)];
      if (params.copyIncludes) args.push("--copy-includes");
      if (params.showReturns) args.push("--show-returns");
      const r = xcav(args);
      return { content: [{ type: "text", text: wrapOutput("xcav_copy", params, r.output || "Copy succeeded.") }] };
    },
  });
}
