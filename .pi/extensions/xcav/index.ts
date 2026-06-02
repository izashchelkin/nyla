/**
 * xcav — Structural code editing through tree-sitter
 *
 * Pure passthrough wrapper around the `xcav` CLI binary.
 * All logic lives in C++; TypeScript handles Pi integration only.
 *
 * Tools:
 *   xcav_blocks     — list structural blocks
 *   xcav_read       — read files (structural or plain)
 *   xcav_move       — move a block within a file
 *   xcav_move_into  — move a block across files
 *   xcav_delete     — delete a block
 *   xcav_replace    — replace a block with new content
 *   xcav_undo       — restore from backup (up to 20 levels)
 *   xcav_tidy       — re-indent a file
 *   xcav_extract    -- move a block to a new file
  *   xcav_inline     -- inline a function call at a line
  */

import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { Type } from "typebox";
import { execSync } from "node:child_process";
import { existsSync, writeFileSync, mkdirSync, unlinkSync } from "node:fs";
import { resolve, join } from "node:path";

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

// ─── Tools ─────────────────────────────────────────────────────────────────

export default function (pi: ExtensionAPI) {

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
      return { content: [{ type: "text", text: r.output || "(no blocks)" }] };
    },
  });

  // ── xcav_read ──────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_read",
    label: "xcav read",
    description:
      "Read files. For C/C++/Java/TypeScript/JavaScript: structural mode shows blocks with paths (e.g. 'Point::GetX'), " +
      "un-indents code to save tokens, supports --name/--all/--numbers. " +
      "For all files: plain mode with offset/limit like a regular file reader. " +
      "Structural params (line, name, all) trigger tree-sitter parsing on C/C++/Java/TS/JS; " +
      "offset/limit trigger plain reading on any file type.",
    promptSnippet: "xcav_read(file, line?, name?, all?, numbers?, offset?, limit?) — read files",
    promptGuidelines: [
      "Use xcav_read for all file reading. It replaces the regular read tool.",
      "For C/C++/Java/TypeScript/JavaScript: use line/name/all for structural reading (shows function paths, un-indents).",
      "Un-indented output can be used directly as oldText for xcav_edit — leading whitespace is auto-matched.",
      "For any file: use offset/limit for plain reading (like the regular read tool).",
      "Line numbers are 1-indexed (as shown by xcav_blocks).",
      "Use --numbers when you need to reference specific lines in subsequent move/delete operations.",
    ],
    parameters: Type.Object({
      file:    Type.String({ description: "Path to the file to read (relative or absolute)" }),
      line:    Type.Optional(Type.Number({ description: "Line number (1-indexed) of the block to read structurally (C/C++/Java/TS/JS only)" })),
      name:    Type.Optional(Type.String({ description: "Structural name to find, e.g. 'foo' or 'Point::GetX' (C/C++/Java/TS/JS only)" })),
      all:     Type.Optional(Type.Boolean({ description: "Dump all structural blocks with code (C/C++/Java/TS/JS only)", default: false })),
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
      return { content: [{ type: "text", text: r.output || "(no output)" }] };
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
      return { content: [{ type: "text", text: r.output || "Move succeeded." }] };
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
      return { content: [{ type: "text", text: r.output || "Move-into succeeded." }] };
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
      return { content: [{ type: "text", text: r.output || "Delete succeeded." }] };
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
      return { content: [{ type: "text", text: r.output || "Replace succeeded." }] };
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
      return { content: [{ type: "text", text: r.output || "Undo succeeded." }] };
    },
  });

  // ── xcav_tidy ───────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_tidy",
    label: "xcav tidy",
    description:
      "Re-indent every structural block in a file via brace-counting + whitespace cleanup. " +
      "Use when indentation is broken after refactoring.",
    promptSnippet: "xcav_tidy(file) — re-indent a file",
    promptGuidelines: [
      "Use xcav_tidy when indentation is broken after refactoring.",
    ],
    parameters: Type.Object({
      file: Type.String({ description: "Path to the file to re-indent" }),
    }),
    async execute(_toolCallId, params) {
      const absPath = resolve(params.file);
      if (!existsSync(absPath)) {
        return { content: [{ type: "text", text: `File not found: ${params.file}` }] };
      }
      const r = xcav(["tidy", absPath]);
      return { content: [{ type: "text", text: r.output || "Tidy succeeded." }] };
    },
  });

  // ── xcav_extract ────────────────────────────────────────────────────────

  pi.registerTool({
    name: "xcav_extract",
    label: "xcav extract",
    description:
      "Move a structural block to a new file. Creates #pragma once, namespace wrapping, " +
      "and adds #include in the source file.",
    promptSnippet: "xcav_extract(srcFile, srcLine, newFile) — move block to a new file",
    promptGuidelines: [
      "Use xcav_extract for splitting files during refactoring.",
    ],
    parameters: Type.Object({
      srcFile: Type.String({ description: "Path to the source file" }),
      srcLine: Type.Number({ description: "Line number of the block to extract (1-indexed)" }),
      newFile: Type.String({ description: "Path for the new file to create" }),
    }),
    async execute(_toolCallId, params) {
      const srcAbs = resolve(params.srcFile);
      const newAbs = resolve(params.newFile);
      if (!existsSync(srcAbs)) {
        return { content: [{ type: "text", text: `Source file not found: ${params.srcFile}` }] };
      }
            const r = xcav(["extract", srcAbs, String(params.srcLine), newAbs]);
            return { content: [{ type: "text", text: r.output || "Extract succeeded." }] };
          },
        });

        // ── xcav_inline ─────────────────────────────────────────────────────────

        pi.registerTool({
          name: "xcav_inline",
          label: "xcav inline",
          description:
            "Inline a simple single-return function call at a line into the call site. " +
            "The inlined body is wrapped in a { } block. Parameters are substituted with arguments. " +
            "Return values are NOT captured -- the agent must fix result assignments manually.",
          promptSnippet: "xcav_inline(file, line) -- inline a function call",
          promptGuidelines: [
            "Use xcav_inline for simple single-return functions only. Multi-statement bodies are rejected.",
            "After inlining, manually fix any result variable assignment (return values are not captured).",
            "Use xcav_undo to recover if the inline produces incorrect code.",
          ],
          parameters: Type.Object({
            file: Type.String({ description: "Path to the source file" }),
            line: Type.Number({ description: "Line number of the function call to inline (1-indexed)" }),
          }),
          async execute(_toolCallId, params) {
            const absPath = resolve(params.file);
            if (!existsSync(absPath)) {
              return { content: [{ type: "text", text: `File not found: ${params.file}` }] };
            }
            const r = xcav(["inline", absPath, String(params.line)]);
            return { content: [{ type: "text", text: r.output || "Inline succeeded." }] };
          },
        });
      }
