# Sally

[![Release](https://img.shields.io/github/v/release/0xeb/sally)](https://github.com/0xeb/sally/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/0xeb/sally/pr-cmake.yml?label=build)](https://github.com/0xeb/sally/actions)
[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)

Sally is a fast, actively maintained dual-panel file manager for Windows. It preserves the classic [Open Salamander](https://github.com/OpenSalamander/salamander) workflow and extends it for modern Windows: Unicode-first file operations, long path support, dark mode, native ARM64 builds, WebView2 viewing, release automation, and maintained language packs.

<!-- Screenshot: this is the classic annotated UI image from the help docs. Replace with a fresh Windows 11 screenshot when available. -->
<p align="center">
  <img src="help/src/hh/salamand/images/main.png" alt="Sally screenshot" width="800">
</p>

## Why Sally

Open Salamander became loved because it made serious file work feel fast: two panels, keyboard-first commands, rich viewers, archives, plugins, and a dense Win32 UI that stays out of the way. Sally keeps that feel, but moves the project forward where the original codebase had started to show its age.

- **Modern Windows path handling**: Unicode and long path work across copy, move, delete, rename, drag/drop, panel state, directory history, Find, viewers, editors, startup paths, and network/UNC navigation.
- **Classic speed, current polish**: dark mode for the core UI and bundled first-party plugins, with `Light`, `Dark`, and `System` theme modes.
- **Native builds**: x64, x86, and ARM64 packages, including Windows on ARM support.
- **Better built-in viewing**: WebView2-based Markdown viewing replaces the legacy Internet Explorer control, and BOM-marked Unicode text files open correctly in Auto/Text mode.
- **Real release pipeline**: GitHub release packages include runtime zips, symbols, language packs, and a translator workspace.
- **Localization is alive**: 10 maintained UI languages ship across supported platforms.

## Highlights

### Unicode And Long Paths

Sally's biggest modernization effort is path correctness. Recent releases moved more of the file manager onto wide Windows APIs and hardened places where lossy ANSI conversions used to leak through.

- File operations support Unicode filenames and Windows long paths up to 32,767 characters.
- Panel paths, directory history, Find dialog seed paths, snooper refresh, F4 edit, Alt+F3 external view, drag/drop, shell paste, and status updates preserve Unicode paths.
- Non-ASCII install paths are supported for startup, language loading, `config.reg`, and crash reporter launch.
- Network and UNC path checks remain cancellable so unreachable saved paths do not hang startup.

### Dark Mode

Sally includes early dark-mode support for the core UI and bundled first-party plugins.

- Configure it under `Options` > `Configuration...` > `Appearance` > `Theme` > `Mode`.
- Choose `Light`, `Dark`, or `System` to follow the Windows app theme.
- Windows High Contrast remains authoritative and disables custom dark painting.

### Viewers And Plugins

- WebView2 renders Markdown instead of the old Internet Explorer WebBrowser control.
- BOM-marked UTF-8, UTF-16LE, and UTF-16BE text files open decoded in Auto/Text mode while Hex mode still shows raw bytes.
- Legacy Altap Salamander 4.0 plugins are loaded only after user consent.
- Plugin dependency loading resolves side-by-side dependencies from the plugin DLL directory.

### Builds, Releases, And Translation

- Modern CMake builds cover Sally, bundled plugins, trace server, translator, shell extension, and release packaging.
- GitHub Actions publish x64, x86, ARM64, symbols, language packs, and translator workspace artifacts.
- The release updater checks GitHub Releases over HTTPS.
- Included languages: Chinese (Simplified), Czech, Dutch, French, German, Hungarian, Romanian, Russian, Slovak, and Spanish.

## Downloads

Pre-built binaries for x64, x86, and ARM64 are available on the [Releases](https://github.com/0xeb/sally/releases) page. For each current release, pick the runtime zip for your CPU architecture. Symbols and the translator workspace are optional downloads for debugging and localization work.

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

Contributions are welcome. See the [Developer Guide](doc/DEV.md) for repository structure, build targets, and internals.

If you want to help with UI translations and language packs, see the [Localization Guide](doc/LOCALIZATION.md).

## About

Sally is an independent fork of [Open Salamander](https://github.com/OpenSalamander/salamander), maintained by [Elias Bachaalany](https://github.com/0xeb). The original Open Salamander authors are not responsible for this project and do not provide support for it. Development is AI-assisted using [Claude Code](https://docs.anthropic.com/en/docs/claude-code). See [Origin](doc/ORIGIN.md) for the full project history.

## License

Sally is based on Open Salamander, open source software licensed under [GPLv2](LICENSE). Individual components and libraries have separate but compatible licenses; see [third_party.txt](doc/third_party.txt) for details.

## Resources

[Altap Website](https://www.altap.cz/) | [Features](https://www.altap.cz/salamander/features/) | [Documentation](https://www.altap.cz/salamander/help/) | [Community Forum](https://forum.altap.cz/) | [Upstream Repository](https://github.com/OpenSalamander/salamander) | [Wikipedia](https://en.wikipedia.org/wiki/Altap_Salamander)
