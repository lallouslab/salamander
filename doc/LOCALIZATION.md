# Localization Guide

Sally keeps the legacy Translator workflow alive, but contributors should use the generated translation workspace instead of trying to create projects manually.

Tracking issue: [#49](https://github.com/0xeb/sally/issues/49)

## Current Status

**Sally ships 10 languages** in the x64 release zip. The release workflow runs `build_language_packs.ps1` after the normal CMake build to produce translated `.slg` files from committed `.slt` text archives.

All 10 languages have been rebased onto the current Sally structure, AI-translated for new strings, and renamed from "Salamander" to "Sally" throughout.

- Sally builds and ships `translator.exe`.
- Sally provides a generated translation workspace flow for local x64 builds.
- The x64 workflow can open generated projects, import archives, export `.slt` archives, and run headless quiet validation.
- Generated `.atp` files currently use machine-local absolute paths, so the workflow is local-first for now.
- The release workflow stages a separate translator workspace zip for translation contributors.

## Translation Coverage

Sally has 31 modules (core + 30 plugins). Translation archives exist for 30 of these across 10 languages:

| Language | Archives | Status |
|---|---:|---|
| Czech | 30 | Fully translated |
| Slovak | 30 | Fully translated |
| Chinese (Simplified) | 30 | Fully translated |
| Dutch | 30 | Fully translated |
| French | 30 | Fully translated |
| German | 30 | Fully translated |
| Hungarian | 30 | Fully translated |
| Romanian | 30 | Fully translated |
| Russian | 30 | Fully translated |
| Spanish | 30 | Fully translated |

All 10 languages are rebased onto the current Sally dialog/menu/string structure. New Sally-specific strings were AI-translated. Legacy "Salamander" references were renamed to "Sally."

**Modules with no translation archives:** `folders`, `portables`, `unole` — these ship English-only for now.

**Archived modules not in the current build:** `ftp`, `pictview` — source exists but these plugins are excluded from the build. Their archives are historical.

## What Gets Committed

Commit updated `.slt` archives under `translations/<language>/`.

Do not commit generated workspace directories, generated `.atp` files, or generated `.slg` files. Those are disposable build artifacts.

## Prerequisites

- Windows
- A populated Sally build output. Example:

```powershell
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Debug --target populate
```

The translation file formats are the same on x86 and x64. The difference today is workflow support, not archive format:

- x64 Translator is the supported automation path for generated project open, preserved Czech/Slovak archive import, quiet validation, and `.slt` export.
- x86 Translator remains a useful fallback for manual/local work, but it is not the release-gated automation path.
- Supported quiet commands now exit with `0` on success, `1` on validation/content failure, and `2` on usage/runtime failure.
- Each supported quiet run overwrites `<project-dir>\<module>.quiet.log` with the Output window contents for that run.
- Today, strict validation still finds real legacy/content issues in the preserved Czech and Slovak core `sally` archives. That is translation debt, not an x64 automation failure.

## Generate a Translation Workspace

From the repository root:

```powershell
pwsh -File .\tools\localization\prepare_translation_workspace.ps1 `
  -BuildRoot .\build-x64\out\sally\Debug_x64 `
  -OutputDir .\out\translation-workspace `
  -Languages czech,slovak `
  -Force
```

The script:

- stages the populated build output under `runtime/`
- copies tracked `.slt` archives under `translations/`
- generates Translator project files under `projects/<language>/<module>/`
- seeds a target `<language>.slg` from the current English `.slg`
- writes `.atp` files with absolute local paths so Translator can resolve files reliably on the current machine
- rewrites `lang.rh` without a BOM because Translator rejects BOM-prefixed symbol files

Existing `.slt` archives are copied into the workspace but are not imported automatically unless you opt in:

```powershell
pwsh -File .\tools\localization\prepare_translation_workspace.ps1 `
  -BuildRoot .\build-x64\out\sally\Debug_x64 `
  -OutputDir .\out\translation-workspace `
  -Languages czech,slovak `
  -ImportArchives `
  -Force
```

If a preserved archive needs to be rebased onto a newer Sally export, use:

```powershell
pwsh -File .\tools\localization\rebase_text_archive.ps1 `
  -CurrentArchive .\build-x64\slt-export-full\sally.slt `
  -LegacyArchive .\translations\czech\sally.slt `
  -OutputArchive .\build-x64\rebased\czech-sally.slt
```

## Workspace Layout

```text
translation-workspace/
  runtime/
  translations/
  projects/<language>/<module>/
```

Each generated project directory contains:

- `<module>.atp`
- `<language>.slg`
- `lang.rh`

## Open a Project in Translator

Example:

```powershell
.\out\translation-workspace\runtime\utils\translator.exe `
  .\out\translation-workspace\projects\czech\sally\sally.atp
```

Project basenames match the module name on purpose. Translator quiet-mode import/export derives `sally.slt`, `webviewer.slt`, and similar names from the project filename.

## Quiet Automation

Translator supports the following headless quiet commands for the generated workspace flow:

- `-quiet-import-slt`
- `-quiet-export-slt`
- `-quiet-export-slt-for-diff`
- `-quiet-validate-all`
- `-quiet-validate-layout`

Contract:

- Exit code `0`: success
- Exit code `1`: validation/content failure
- Exit code `2`: usage/runtime failure
- Log file: `<project-dir>\<module>.quiet.log`

Validate a project:

```powershell
.\out\translation-workspace\runtime\utils\translator.exe `
  -quiet-validate-all `
  .\out\translation-workspace\projects\czech\sally\sally.atp
```

Export a diff-friendly archive:

```powershell
.\out\translation-workspace\runtime\utils\translator.exe `
  -quiet-export-slt-for-diff `
  .\out\translation-workspace\exports `
  .\out\translation-workspace\projects\czech\sally\sally.atp
```

The export directory is created automatically when it does not exist.

## Verify the Workspace

For the supported x64 headless workflow:

```powershell
pwsh -File .\tools\localization\verify_translation_workspace.ps1 `
  -WorkspaceDir .\out\translation-workspace
```

The verifier checks:

- `-quiet-validate-all` for `czech/sally` and accepts either:
  - exit code `0` if the core translation is eventually cleaned up
  - exit code `1` if validation still reports known content issues, as long as the quiet log contains a validation summary
- `-quiet-validate-all` for `slovak/sally` with the same baseline contract
- `-quiet-validate-all` for `czech/webviewer` and requires a clean success summary
- `-quiet-validate-all` for `slovak/automation` and requires a clean success summary
- `-quiet-export-slt` for `czech/sally`
- `-quiet-export-slt` for `slovak/automation`
- `-quiet-export-slt` for `czech/webviewer`

For local/manual UI smoke testing:

```powershell
pwsh -File .\tools\localization\smoke_test_translation_workspace.ps1 `
  -WorkspaceDir .\out\translation-workspace
```

## Contribution Flow

1. Generate or download the translation workspace.
2. Open the module project you want to edit in Translator.
3. If you want to investigate an existing archive, import it manually in Translator or regenerate the workspace with `-ImportArchives`.
4. Make translation changes and save the project.
5. Export the updated `.slt` archive.
6. Copy the exported `.slt` over `translations/<language>/<module>.slt`.
7. Commit only the updated `.slt` archive and any documentation/tooling changes.

## Current Audit Findings

- The current x64 Czech/Slovak audit imports all 56 preserved archives successfully.
- The preserved core archives originally drifted at dialog `270`, where the old Open Salamander about box no longer matched current Sally's GitHub/license controls.
- The preserved `checkver.slt` archives also drifted and needed rebasing onto the current Sally CheckVer structure.
- `tools/localization/rebase_text_archive.ps1` now rebases a legacy archive onto a current exported skeleton by preserving translated text/state for matching dialog control IDs, menu item IDs, and string IDs.
- After rebasing, `translations/czech/sally.slt`, `translations/slovak/sally.slt`, `translations/czech/checkver.slt`, and `translations/slovak/checkver.slt` all import cleanly in the generated workspace flow.
- Quiet validation now exits headlessly on x64 with deterministic logs for the verified Czech/Slovak sample projects.
- The current preserved Czech and Slovak core `sally` archives still fail strict validation with real menu/layout/text findings, so the verifier treats those two core validations as a known-content baseline rather than a release-blocking clean pass.
- The audited plugin samples `czech/webviewer` and `slovak/automation` validate cleanly on x64.
- Quiet `.slt` export produces current archives on x64 for the tested Czech core plus `webviewer` and `automation` plugin projects, and on x86 for the tested Czech core project.

## Known Limitations

- The old Translator "New Project" GUI path is commented out, so generated workspaces are the supported path.
- Generated `.atp` files are machine-local because they currently use absolute paths.
- Quiet archive import is now validated on x64 for the current Czech/Slovak preserved set, including the rebased core `sally.slt` and `checkver.slt` files.
- Translator project files must use CRLF line endings, and symbol files must be BOM-free. The generator handles both quirks for you.
- Legacy validation sidecars such as `SalMenu`, `IgnoreList`, and `CheckList` are not wired into the generated projects unless a canonical source file is added later.
- The current required smoke-tested languages are Czech and Slovak. Other preserved language folders can still be generated, but the public readiness bar for this slice is based on Czech/Slovak plus one plugin path.
