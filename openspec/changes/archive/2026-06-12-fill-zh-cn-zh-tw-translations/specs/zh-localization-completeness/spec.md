## ADDED Requirements

### Requirement: Complete zh_CN coverage of POT catalog

Every translatable `msgid` in `localization/i18n/PhrozenOrca.pot` SHALL have a non-empty `msgstr` in `localization/i18n/zh_CN/PhrozenOrca_zh_CN.po` that is not identical to the `msgid` (except for entries intentionally kept untranslated such as keyboard shortcuts, unit symbols, and material codes).

#### Scenario: Validation reports zero zh_CN gaps

- **WHEN** `scripts/check_missing_translations.js` is run against the current POT and zh_CN PO files
- **THEN** the missing zh_CN count SHALL be 0

#### Scenario: Empty msgstr is treated as missing

- **WHEN** a POT msgid exists in zh_CN with `msgstr ""`
- **THEN** the validation script SHALL report it as missing

### Requirement: Complete zh_TW coverage of POT catalog

Every translatable `msgid` in `localization/i18n/PhrozenOrca.pot` SHALL have a non-empty `msgstr` in `localization/i18n/zh_TW/PhrozenOrca_zh_TW.po` that is not identical to the `msgid` (except for entries intentionally kept untranslated such as keyboard shortcuts, unit symbols, and material codes).

#### Scenario: Validation reports zero zh_TW gaps

- **WHEN** `scripts/check_missing_translations.js` is run against the current POT and zh_TW PO files
- **THEN** the missing zh_TW count SHALL be 0

#### Scenario: zh_TW-only gaps are filled

- **WHEN** a msgid is present in POT but missing or empty in zh_TW (e.g., `Ok`, `Skirt`, `Libraries`)
- **THEN** zh_TW SHALL provide an appropriate Traditional Chinese translation

### Requirement: SLA and resin strings are fully localized

All msgids categorized as SLA/resin-related (support gizmo, hollow/drill, exposure/lift/retract parameters, PRZ export, Phrozen branding) SHALL be translated in both zh_CN and zh_TW using domain-appropriate terminology.

#### Scenario: SLA gizmo shows Chinese labels

- **WHEN** the UI language is set to 简体中文 or 繁體中文 and the user opens SLA support, hollow, or overhang gizmos
- **THEN** toolbar labels, tooltips, and progress messages SHALL display in the selected Chinese locale rather than English

#### Scenario: Exposure and lift parameters show Chinese tooltips

- **WHEN** the UI language is Chinese and the user views SLA print settings or printer parameter tooltips for exposure, lift, retract, and rest time
- **THEN** parameter names and descriptions SHALL display in the selected Chinese locale

#### Scenario: Phrozen SLA messages are localized

- **WHEN** the application displays Phrozen SLA-specific messages (e.g., preset not found, save PRZ file dialog title)
- **THEN** the text SHALL appear in the selected Chinese locale

### Requirement: Gettext format integrity is preserved

Translated `msgstr` values SHALL preserve all format placeholders and escape sequences from the corresponding `msgid` (`%1$d`, `%s`, `%u`, `%%`, `\n`, quoted spans).

#### Scenario: Placeholder count matches

- **WHEN** a msgid contains format placeholders
- **THEN** the zh_CN and zh_TW msgstr SHALL contain the same placeholders in valid gettext form

#### Scenario: Multi-line strings preserve line breaks

- **WHEN** a msgid contains `\n` line breaks
- **THEN** the translated msgstr SHALL preserve the same number and position of line breaks

### Requirement: Compiled resources reflect updated translations

After PO files are updated, the project SHALL regenerate compiled gettext resources so runtime UI loads the new translations.

#### Scenario: MO files are rebuilt

- **WHEN** zh_CN and zh_TW PO files are updated and `scripts/run_gettext.bat` (or equivalent) is executed
- **THEN** compiled `.mo` files under `resources/i18n/` SHALL reflect the new translations without build errors

### Requirement: Repeatable localization gap detection

The project SHALL provide a script that compares POT against zh_CN and zh_TW PO files and reports missing translations, usable in future development cycles.

#### Scenario: Script outputs gap summary

- **WHEN** a developer runs `node scripts/check_missing_translations.js`
- **THEN** the script SHALL print counts of missing zh_CN and zh_TW entries and write a machine-readable report
