#pragma once

// Re-export hub for all xcav edit operations.
// Individual operations live in their own files:
//   xcav/move.h       — MoveBlock
//   xcav/move_into.h  — MoveBlockInto
//   xcav/copy.h       — CopyBlock
//   xcav/delete.h     — DeleteBlock
//   xcav/replace.h    — ReplaceBlock, ReplaceInBlock
//   xcav/edit_safe.h  -- EditSafe

#include "xcav/move.h"
#include "xcav/move_into.h"
#include "xcav/copy.h"
#include "xcav/delete.h"
#include "xcav/replace.h"
#include "xcav/edit_safe.h"
