# Skills

Procedural knowledge for AI coding agents working on this repository.
These are the source of truth — the agent's skill memory should be regenerated
from these files when they change.

## Keeping skills in sync

Skills are versioned in git alongside the code they describe. When you change
xcav behavior, two things must be updated:

1. **`xcav/onboard.inl`** — the authoritative agent documentation, used by `xcav onboard`
2. **`skills/xcav/SKILL.md`** — the agent skill, regenerated from onboard output

Run `xcav onboard` to see the current documentation, then update the skill.

## Skill list

| Skill | Scope | What it covers |
|-------|-------|----------------|
| `xcav` | global | Full xcav tool surface: blocks, read, edit, move, delete, replace, copy, move-into, undo |
| `review-before-acting` | project | Investigate first, propose with honesty, confirm before implementing |
| `remove-xcav-command` | project | Remove a CLI command from the xcav tool |
| `analyze-xcav-usage` | project | Analyze xcav usage logs to understand agent behavior |

## How agents load skills

Agents use the `skill` tool with `scope='global'` or `scope='project'`.
Project skills are scoped to this repository. Global skills (like `xcav`)
are portable across repositories.
