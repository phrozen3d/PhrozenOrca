## 1. Preparation and Tooling

- [x] 1.1 Confirm baseline gap counts by running `node scripts/check_missing_translations.js` and recording current zh_CN/zh_TW missing totals
- [x] 1.2 Review `scripts/translation_suggestions.json` and `missing_translations_report.md` for SLA/resin entries (107) before applying
- [x] 1.3 Add or refine `scripts/apply_translation_suggestions.js` to patch `.po` files from the translation dictionary with placeholder validation

## 2. SLA / Resin Translations (Priority)

- [x] 2.1 Apply SLA gizmo strings (support, hollow/drill, overhang detect, keyboard shortcuts) to zh_CN and zh_TW
- [x] 2.2 Apply exposure/lift/retract/rest-time parameter names and tooltips to zh_CN and zh_TW
- [x] 2.3 Apply Phrozen branding and PRZ export dialog strings to zh_CN and zh_TW
- [x] 2.4 Apply SLA support geometry parameter tooltips (pillar, pinhead, contact, raft) to zh_CN and zh_TW

## 3. Remaining zh_CN and zh_TW Gaps

- [x] 3.1 Backfill 109 zh_CN-only gaps using existing zh_TW translations as reference (simplified conversion where needed)
- [x] 3.2 Fill 13 zh_TW-only gaps (`Ok`, `Skirt`, `Libraries`, `WebView2 Runtime`, etc.)
- [x] 3.3 Apply remaining UI short strings and FDM-related tooltips missing in both locales
- [x] 3.4 Apply long description strings (gap fill, fuzzy skin, bridge layers, infill rotation) with manual review of `\n` and `%%` placeholders

## 4. Validation and Compile

- [x] 4.1 Run `node scripts/check_missing_translations.js` and confirm zero missing zh_CN and zh_TW entries
- [x] 4.2 Spot-check placeholder parity on all multi-line and `%`-containing msgids
- [x] 4.3 Run `scripts/run_gettext.bat` to compile updated `.mo` resources
- [x] 4.4 Manual smoke test: switch UI to 简体中文 and 繁體中文; verify SLA gizmo, print settings, and export dialogs show Chinese text
