## Context

PhrozenOrca uses GNU gettext (`.pot` / `.po` / `.mo`) for UI localization. Source strings live in `PhrozenOrca.pot`; per-locale translations are in `localization/i18n/<locale>/PhrozenOrca_<locale>.po` and compiled to `resources/i18n/`.

A June 2026 audit (`scripts/check_missing_translations.js`) compared POT against zh_CN and zh_TW and found:

| Gap | Count |
|-----|-------|
| zh_CN missing | 356 |
| zh_TW missing | 260 |
| Unique msgids | 369 |
| Both missing | 247 |
| Only zh_CN missing | 109 |
| Only zh_TW missing | 13 |

SLA/resin strings account for 107 of the 369 gaps. A reference translation dictionary (`scripts/translation_suggestions.json`) and report (`missing_translations_report.md`) were produced during exploration.

## Goals / Non-Goals

**Goals:**

- Achieve zero missing translations in zh_CN and zh_TW relative to `PhrozenOrca.pot`.
- Apply SLA/resin strings first so resin-mode UI is fully localized.
- Include Daily Tips (`resources/data/hints.ini`) in POT and keep zh_CN/zh_TW hint translations active (not `#~` obsolete).
- Reuse existing zh_TW translations as the source of truth when backfilling zh_CN (109 entries).
- Preserve gettext placeholders, escape sequences, and formatting.
- Provide a repeatable validation script for future POT updates.

**Non-Goals:**

- Translating other locales (ja, ko, de, etc.).
- Changing source English strings in C++ code.
- Rewriting or rephrasing already-complete translations.
- Adding new UI features or changing application behavior.

## Decisions

### 1. POT as the single source of truth for completeness

**Decision:** A msgid is "missing" if it exists in `PhrozenOrca.pot` and the locale `.po` entry has an empty `msgstr`, `msgstr` identical to `msgid`, or no entry at all.

**Rationale:** Matches standard gettext workflow and the existing `run_gettext` pipeline.

**Alternative considered:** Using `en.po` as baseline — rejected because POT is the canonical msgid catalog.

### 2. Apply translations in priority order

**Decision:** Implementation order:
1. SLA/resin category (107 entries)
2. Entries missing in both locales (remaining from the 247 overlap)
3. zh_CN-only gaps (copy/adapt from existing zh_TW where available)
4. zh_TW-only gaps (13 entries)

**Rationale:** Maximizes user-visible impact for this resin-focused fork.

### 3. Terminology conventions

**Decision:**

| Concept | zh_CN | zh_TW |
|---------|-------|-------|
| Print | 打印 | 列印 |
| Filament | 耗材 | 線材 |
| Support | 支撑 | 支撐 |
| Hollow | 镂空 | 鏤空 |
| Mouse | 鼠标 | 滑鼠 |
| Settings | 设置 | 設定 |
| Export | 导出 | 匯出 |
| Keyboard shortcuts | Keep as-is (`Ctrl+`, `Alt+`) | Same |
| Material codes (PLA, PETG) | Keep as-is | Same |
| Brand names (Phrozen Orca) | Keep as-is | Same |

**Rationale:** Aligns with existing completed entries in both `.po` files.

### 4. Use scripted patch + manual review for long strings

**Decision:** Apply bulk updates via a Node.js patch script reading `translation_suggestions.json`; manually verify multi-line tooltip strings (>200 chars) and strings with `%` placeholders.

**Rationale:** 369 entries is too large for hand-editing without automation; long SLA parameter descriptions need human review for placeholder integrity.

**Alternative considered:** Manual-only editing — rejected due to volume and repeatability needs.

### 5. Validation before compile

**Decision:** After patching, run `node scripts/check_missing_translations.js` and confirm zero gaps; then compile `.mo` files.

**Rationale:** Catches regressions before build.

### 6. POT regeneration MUST include `hints.ini` (Daily Tips)

**Decision:** Regenerating `PhrozenOrca.pot` SHALL use `scripts/run_gettext.bat --full`, which runs:

1. `xgettext` — C/C++ `L()` / `_L()` / `_u8L()` strings from `localization/i18n/list.txt`
2. `python scripts/HintsToPot.py` — appends `resources/data/hints.ini` `text =` entries to the POT
3. `msgmerge -N` — updates every `.po` file against the combined POT

Plain `scripts/run_gettext.bat` (without `--full`) only runs `msgfmt` and MUST NOT be used to regenerate POT.

**Rationale:** Daily Tips load English from `hints.ini` and translate at runtime via `_utf8()` against compiled `.mo`. If hint msgids are absent from POT, `msgmerge` obsoletes their `.po` entries and `msgfmt` omits them — the UI shows English body text even when the rest of the app is localized.

**Regression observed (2026-06):** Commit `23d55cfed` regenerated POT without `HintsToPot.py`. POT hint count went from 37 → 0; zh_TW active hint entries went from 37 → 0 (moved to `#~` obsolete). `check_missing_translations.js` still reported zero gaps because hint msgids were no longer in POT.

**Remediation:** Re-run `--full`, confirm POT contains 37 `resources/data/hints.ini` references, confirm zh_CN/zh_TW each have 37 non-empty hint `msgstr` entries, strip `#, fuzzy` from hint blocks, then compile `.mo`.

**Alternative considered:** Translating `hints.ini` directly in the INI file — rejected; existing Orca/PrusaSlicer architecture uses gettext for hint text.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Placeholder corruption (`%1$d`, `%s`, `%%`) | Script validates placeholder parity between msgid and msgstr; spot-check SLA strings |
| zh_CN/zh_TW terminology inconsistency with existing entries | Cross-reference existing translated neighbors in `.po` files before applying |
| POT updates during implementation add new gaps | Re-run check script; document as follow-up if new strings appear |
| POT regenerated without `HintsToPot.py` silently drops Daily Tips translations | Always use `run_gettext.bat --full` when updating POT; verify POT contains `resources/data/hints.ini` entries (expect 37) |
| `msgfmt` skips `#, fuzzy` hint entries | After `msgmerge`, remove `#, fuzzy` from hint blocks that already have valid `msgstr` before compiling `.mo` |
| Over-long UI strings in narrow panels | Prefer concise phrasing; match existing style for similar labels |
| `.po` merge conflicts | Single focused PR; apply in one commit per locale |

## Migration Plan

1. Patch `PhrozenOrca_zh_CN.po` and `PhrozenOrca_zh_TW.po`.
2. Run validation script — expect 0 missing.
3. If POT was regenerated: run `scripts/run_gettext.bat --full` (not plain `run_gettext.bat`).
4. Compile gettext resources via `scripts/run_gettext.bat`.
5. Smoke-test in app: switch UI language to 简体中文 and 繁體中文; verify SLA gizmo, printer settings dialog, export dialogs, and **Daily Tips** show Chinese text.
6. Rollback: revert `.po` and recompiled `.mo` files.

## Open Questions

- None blocking implementation. Translation suggestions from exploration are complete for all 369 msgids.
- Follow-up (optional): extend `check_missing_translations.js` to assert POT contains all `hints.ini` msgids so hint regressions are caught by automation.
