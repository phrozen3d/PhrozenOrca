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

## Capabilities

### New Capabilities

- `zh-localization-completeness`: Requirements for complete Simplified and Traditional Chinese coverage of all POT msgids, with SLA/resin strings prioritized and validation tooling.

### Modified Capabilities

- None.

## Impact

- Affected files: `localization/i18n/zh_CN/PhrozenOrca_zh_CN.po`, `localization/i18n/zh_TW/PhrozenOrca_zh_TW.po`, compiled resources under `resources/i18n/`.
- Reference artifacts from exploration: `scripts/check_missing_translations.js`, `scripts/translation_suggestions.json`, `missing_translations_report.md`.
- No application logic, API, or build-system changes beyond standard gettext compile step.
- No breaking changes.
