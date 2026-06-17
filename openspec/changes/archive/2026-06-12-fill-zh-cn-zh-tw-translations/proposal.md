## Why

An audit of `localization/i18n` against `PhrozenOrca.pot` found **356 missing zh_CN** and **260 missing zh_TW** entries (369 unique msgids), including SLA/resin UI strings, exposure/lift parameters, support gizmo labels, and Phrozen-specific messages. Untranslated or empty strings degrade UX for Chinese users and leave resin-mode features showing English in the UI.

## What Changes

- Fill all identified missing translations in `PhrozenOrca_zh_CN.po` and `PhrozenOrca_zh_TW.po`.
- Prioritize SLA/resin-related strings (107 entries): support gizmo, hollow/drill, exposure/lift/retract, PRZ export, Phrozen branding.
- Backfill 109 entries where zh_TW already has translations but zh_CN is still empty.
- Complete 13 entries missing only in zh_TW (short UI labels and technical terms).
- Preserve gettext conventions: `%1$d`, `%s`, `\n`, `%%`, and keyboard shortcuts (`Ctrl+`, `Alt+`) where appropriate.
- Add a repeatable check script and translation reference so future POT updates can be validated.
- Regenerate compiled `.mo` resources via existing `run_gettext` workflow after PO updates.

### Follow-up fix (2026-06): Daily Tips / `hints.ini` regression

The initial backfill commit (`23d55cfed`) regenerated `PhrozenOrca.pot` with **xgettext only** and did **not** run `HintsToPot.py`. That dropped all 37 `resources/data/hints.ini` msgids from the POT catalog. `msgmerge` then marked the existing zh_CN/zh_TW Daily Tips translations as `#~` obsolete. Recompiling `.mo` with plain `scripts\run_gettext.bat` caused Daily Tips body text to fall back to English while UI chrome (e.g. "每日提示") stayed Chinese.

**Remediation applied:**

- Run `scripts\run_gettext.bat --full` so POT includes both C++ strings (xgettext) and Daily Tips (`HintsToPot.py`), then `msgmerge` all `.po` files.
- Restore zh_CN/zh_TW hint translations from merged obsolete entries (37/37 filled for both locales).
- Remove `#, fuzzy` from hint entries before `msgfmt` (fuzzy strings are not compiled into `.mo`).
- Re-run `scripts\run_gettext.bat` to compile `.mo`.

**Workflow rule going forward:** any POT regeneration MUST use `--full`; plain `run_gettext.bat` is only for recompiling `.mo` after `.po` edits.

## Capabilities

### New Capabilities

- `zh-localization-completeness`: Requirements for complete Simplified and Traditional Chinese coverage of all POT msgids, with SLA/resin strings prioritized and validation tooling.

### Modified Capabilities

- None.

## Impact

- Affected files: `localization/i18n/PhrozenOrca.pot`, `localization/i18n/zh_CN/PhrozenOrca_zh_CN.po`, `localization/i18n/zh_TW/PhrozenOrca_zh_TW.po`, compiled resources under `resources/i18n/`.
- Daily Tips source: `resources/data/hints.ini` (appended to POT via `scripts/HintsToPot.py` during `--full` run).
- Reference artifacts from exploration: `scripts/check_missing_translations.js`, `scripts/translation_suggestions.json`, `missing_translations_report.md`.
- No application logic, API, or build-system changes beyond standard gettext compile step.
- No breaking changes.
