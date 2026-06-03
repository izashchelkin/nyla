#pragma once

// Re-export header for backward compatibility.
// The old monolithic editor.h/editor.cc has been split into:
//   xcav/language.h     — source_language, DetectLanguage, GrammarForLanguage
//   xcav/text_util.h    -- LineStartOffset, LineEndOffset, LineIndent, StrEq, NormalizeText
//   xcav/block_query.h  — block_info, read_block_info, block_loc, ListBlocks, LocateBlock, ReadBlock, BlockTypeLabel
//   xcav/edit_ops.h     -- MoveBlock, MoveBlockInto, DeleteBlock, ReplaceInBlock, EditSafe

#include "xcav/block_query.h"
#include "xcav/edit_ops.h"
#include "xcav/language.h"
#include "xcav/text_util.h"
