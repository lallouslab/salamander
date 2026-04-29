# Sally

[![Release](https://img.shields.io/github/v/release/0xeb/sally)](https://github.com/0xeb/sally/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/0xeb/sally/pr-cmake.yml?label=build)](https://github.com/0xeb/sally/actions)
[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)

A fast, modern dual-panel file manager for Windows - forked from [Open Salamander](https://github.com/OpenSalamander/salamander) and extended with dark mode, Unicode and long path file operations, native ARM64 builds, and modern release tooling.

<!-- Screenshot: this is the classic annotated UI image from the help docs. Replace with a fresh Windows 11 screenshot when available. -->
<p align="center">
  <img src="help/src/hh/salamand/images/main.png" alt="Sally screenshot" width="800">
</p>

## About

Sally is an independent fork of [Open Salamander](https://github.com/OpenSalamander/salamander), maintained by [Elias Bachaalany](https://github.com/0xeb). The original Open Salamander authors are not responsible for this project and do not provide support for it. Development is AI-assisted using [Claude Code](https://docs.anthropic.com/en/docs/claude-code). See [Origin](doc/ORIGIN.md) for the full project history.

## What Sally Adds

Sally keeps the classic Open Salamander dual-panel workflow, but it is not just a rebuild. It carries forward active Windows work that upstream Open Salamander does not ship:

- Dark mode for the core UI and bundled first-party plugins, with `Light`, `Dark`, and `System` theme modes
- Unicode filename support throughout file operations, including safer handling of names that cannot round-trip through the system ANSI code page
- Long path support for modern Windows paths up to 32,767 characters
- Native x64, x86, and ARM64 builds, including Windows on ARM support
- WebView2-based Markdown viewing instead of the legacy Internet Explorer WebBrowser control
- Viewer support for BOM-marked Unicode text files in Auto/Text mode
- Modern CMake builds for Sally, bundled plugins, trace server, translator, and shell extension
- Automated GitHub release packages with runtime zips, symbols, language packs, and a translator workspace
- Consent-based loading for legacy Altap Salamander 4.0 plugins

## What's New

**Dark Mode Support**
- Early dark-mode support for Sally core UI and bundled first-party plugins
- Choose `Options` > `Configuration...` > `Appearance` > `Theme` > `Mode`
- Modes: `Light` (default), `Dark`, and `System` (follows the Windows app theme)
- Windows High Contrast remains authoritative and disables custom dark painting

**Unicode & Long Path Support**
- Full Unicode filename support for all file operations
- Long path support — up to 32,767 characters
- Viewer support for BOM-marked Unicode text files in Auto/Text mode
- Unicode filename preservation during disk drag operations

**Modern Build System**
- Complete CMake port — all 25+ plugins, trace server, translator, and shell extension
- Native x64, x86, and ARM64 targets
- GitHub Actions CI/CD with automated releases
- Release packages include runtime zips, symbols, and a translator workspace

**Architecture Improvements**
- Decoupled UI dialogs from file operation logic for better testability
- WebView2 markdown viewer replaces the legacy IE WebBrowser control

**Localization**
- 10 languages included: Chinese (Simplified), Czech, Dutch, French, German, Hungarian, Romanian, Russian, Slovak, Spanish
- Language packs ship on all platforms (x64, x86, ARM64)
- Translator workspace artifact for community translators

**Platform & Compatibility**
- ARM64 native build for Windows on ARM
- Runs under Wine on Linux
- Consent-based loading of legacy Altap Salamander 4.0 plugins

## Downloads

Pre-built binaries for x64, x86, and ARM64 are available on the [Releases](https://github.com/0xeb/sally/releases) page. Each release includes language packs for 10 languages and a translator workspace for community contributors.

## Building

### Prerequisites

- [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) with the **Desktop development with C++** workload
- [Windows 11 SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) (10.0.26100 or newer)

### Build Commands

```bash
# Configure and build (x64 default)
cmake -S . -B build
cmake --build build --config RelWithDebInfo

# Populate output directory
cmake --build build --config RelWithDebInfo --target populate
```

Output: `build/out/sally/<Config>_<Arch>/`

When you run Sally from `build/out`, rebuild and repopulate that exact configuration first. For example, use `cmake --build build --config Release --target populate` before launching `build/out/sally/Release_x64/` so plugin DLLs and `.slg` language files stay in sync.

## Contributing

Contributions are welcome! See the [Developer Guide](doc/DEV.md) for repository structure and internals.

If you want to help with UI translations and language packs, see the [Localization Guide](doc/LOCALIZATION.md).

## License

Sally is based on Open Salamander, open source software licensed under [GPLv2](LICENSE). Individual components and libraries have separate but compatible licenses — see [third_party.txt](doc/third_party.txt) for details.

## Resources

[Altap Website](https://www.altap.cz/) | [Features](https://www.altap.cz/salamander/features/) | [Documentation](https://www.altap.cz/salamander/help/) | [Community Forum](https://forum.altap.cz/) | [Upstream Repository](https://github.com/OpenSalamander/salamander) | [Wikipedia](https://en.wikipedia.org/wiki/Altap_Salamander)
