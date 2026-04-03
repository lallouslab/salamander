#!/usr/bin/env python3
"""
translate_slt.py - AI-powered translation of Sally .slt archives.

Translates all state=0 (untranslated) entries in .slt files using Claude API.
Preserves format specifiers, dialog layout coordinates, and section structure.

Usage:
    # Translate a single file
    python translate_slt.py translations/german/automation.slt --language german

    # Translate all modules for a language
    python translate_slt.py translations/german/ --language german

    # Translate all languages
    python translate_slt.py translations/ --all-languages

    # Dry run (show what would be translated)
    python translate_slt.py translations/german/automation.slt --language german --dry-run

Requires ANTHROPIC_API_KEY environment variable.
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# Language metadata
# ---------------------------------------------------------------------------

LANGUAGES = {
    "chinesesimplified": {"name": "Simplified Chinese", "langid": 2052},
    "czech":             {"name": "Czech",              "langid": 1029},
    "dutch":             {"name": "Dutch",              "langid": 1043},
    "french":            {"name": "French",             "langid": 1036},
    "german":            {"name": "German",             "langid": 1031},
    "hungarian":         {"name": "Hungarian",          "langid": 1038},
    "romanian":          {"name": "Romanian",           "langid": 1048},
    "russian":           {"name": "Russian",            "langid": 1049},
    "slovak":            {"name": "Slovak",             "langid": 1051},
    "spanish":           {"name": "Spanish",            "langid": 3082},
}

# ---------------------------------------------------------------------------
# .slt parser
# ---------------------------------------------------------------------------

# Section types
SEC_NONE = "none"
SEC_EXPORTINFO = "exportinfo"
SEC_TRANSLATION = "translation"
SEC_DIALOG = "dialog"
SEC_MENU = "menu"
SEC_STRINGTABLE = "stringtable"
SEC_RELAYOUT = "relayout"

SECTION_RE = re.compile(r"^\[(EXPORTINFO|TRANSLATION|DIALOG|MENU|STRINGTABLE|RELAYOUT)(?:\s+(\d+))?\]$", re.IGNORECASE)

# Match translatable lines:
#   DIALOG header:  width,height,STATE,"TEXT"
#   DIALOG control: id,x,y,width,height,STATE,"TEXT"
#   MENU item:      id,STATE,"TEXT"
#   STRINGTABLE:    id,STATE,"TEXT"
#
# Common pattern: the line ends with  ,STATE,"TEXT"  where STATE is 0 or 1.
# We capture everything before the state as "prefix", then state and text.
ENTRY_RE = re.compile(r'^(.*),([01]),"(.*)"$')


@dataclass
class SltEntry:
    """A single translatable entry in an .slt file."""
    line_index: int          # Line number in the file (0-based)
    section_type: str        # SEC_DIALOG, SEC_MENU, SEC_STRINGTABLE
    section_id: int          # The numeric ID after the section name
    prefix: str              # Everything before ,state,"text"
    state: int               # 0=untranslated, 1=translated
    text: str                # The quoted text content
    is_dialog_header: bool   # True for dialog dimension line (first entry in DIALOG)


def parse_slt(content: str) -> tuple[list[str], list[SltEntry]]:
    """Parse an .slt file into raw lines and extracted entries.

    Returns:
        lines: All lines of the file (for reconstruction)
        entries: Parsed translatable entries with line references
    """
    lines = content.split("\n")
    entries = []

    current_section = SEC_NONE
    current_section_id = 0
    is_first_in_dialog = False

    for i, line in enumerate(lines):
        stripped = line.rstrip("\r")

        # Check for section header
        m = SECTION_RE.match(stripped)
        if m:
            sec_name = m.group(1).lower()
            sec_id = int(m.group(2)) if m.group(2) else 0
            current_section = sec_name
            current_section_id = sec_id
            is_first_in_dialog = (sec_name == SEC_DIALOG)
            continue

        # Skip non-translatable sections
        if current_section not in (SEC_DIALOG, SEC_MENU, SEC_STRINGTABLE):
            continue

        # Skip blank lines
        if not stripped:
            continue

        # Try to match a translatable entry
        m = ENTRY_RE.match(stripped)
        if not m:
            continue

        prefix = m.group(1)
        state = int(m.group(2))
        text = m.group(3)

        entry = SltEntry(
            line_index=i,
            section_type=current_section,
            section_id=current_section_id,
            prefix=prefix,
            state=state,
            text=text,
            is_dialog_header=is_first_in_dialog,
        )
        entries.append(entry)
        is_first_in_dialog = False

    return lines, entries


def reconstruct_line(entry: SltEntry) -> str:
    """Reconstruct an .slt line from an entry."""
    return f'{entry.prefix},{entry.state},"{entry.text}"'


# ---------------------------------------------------------------------------
# Format specifier validation
# ---------------------------------------------------------------------------

# Matches: %s, %d, %u, %lu, %llu, %ld, %x, %X, %02d, %-10s, %1, %2, etc.
FORMAT_SPEC_RE = re.compile(r'%(?:[-+0 #]*\d*(?:\.\d+)?(?:l{0,2}|h{0,2})?[diouxXeEfFgGaAcspn%]|\d+)')

# Matches \t, \r, \n, \r\n as literal text (not actual control chars)
ESCAPE_RE = re.compile(r'\\[trn]')


def extract_format_specifiers(text: str) -> list[str]:
    """Extract format specifiers from a string."""
    return FORMAT_SPEC_RE.findall(text)


def validate_translation(english: str, translated: str) -> list[str]:
    """Validate that a translation preserves format specifiers and structure.

    Returns a list of warning messages (empty if valid).
    """
    warnings = []

    # Check format specifiers
    eng_specs = extract_format_specifiers(english)
    trl_specs = extract_format_specifiers(translated)
    if eng_specs != trl_specs:
        warnings.append(
            f"Format specifiers differ: English has {eng_specs}, "
            f"translation has {trl_specs}"
        )

    # Check that \t, \r\n, \n are preserved (as literal escape sequences)
    eng_escapes = sorted(ESCAPE_RE.findall(english))
    trl_escapes = sorted(ESCAPE_RE.findall(translated))
    if eng_escapes != trl_escapes:
        warnings.append(
            f"Escape sequences differ: English has {eng_escapes}, "
            f"translation has {trl_escapes}"
        )

    # Check tab-separated hotkey hints (menu items: "Label\tCtrl+C")
    if "\\t" in english:
        eng_parts = english.split("\\t")
        trl_parts = translated.split("\\t")
        if len(eng_parts) != len(trl_parts):
            warnings.append("Tab-separated sections count differs")
        elif len(eng_parts) > 1 and eng_parts[-1] != trl_parts[-1]:
            warnings.append(
                f"Hotkey hint changed: '{eng_parts[-1]}' → '{trl_parts[-1]}'"
            )

    # Check plural markers {!} are preserved
    eng_plural = english.count("{!")
    trl_plural = translated.count("{!")
    if eng_plural != trl_plural:
        warnings.append(
            f"Plural marker count differs: {eng_plural} vs {trl_plural}"
        )

    return warnings


# ---------------------------------------------------------------------------
# AI translation
# ---------------------------------------------------------------------------

def build_translation_prompt(
    entries: list[SltEntry],
    language_name: str,
    module_name: str,
    context_entries: list[SltEntry] | None = None,
) -> str:
    """Build a prompt for translating .slt entries."""

    # Collect context: already-translated entries from the same file
    context_block = ""
    if context_entries:
        samples = context_entries[:30]  # Limit context size
        context_lines = []
        for e in samples:
            context_lines.append(f"  {e.text}")
        context_block = (
            "\n\nHere are some already-translated strings from this file "
            "for tone and terminology reference:\n"
            + "\n".join(context_lines)
        )

    # Build the list of strings to translate
    items = []
    for i, entry in enumerate(entries):
        items.append({"id": i, "english": entry.text})

    prompt = f"""Translate the following UI strings from English to {language_name}.
This is for Sally, a dual-panel file manager for Windows (fork of Open Salamander).

Module: {module_name}

Rules:
1. Return ONLY a JSON array of objects with "id" and "translated" fields.
2. Preserve ALL format specifiers exactly: %s, %d, %u, %lu, %%, %1, %2, etc.
3. Preserve ALL escape sequences exactly: \\n, \\r\\n, \\t
4. Preserve keyboard shortcut hints after \\t (e.g., "\\tCtrl+C") — do NOT translate the shortcut itself.
5. Place the & accelerator prefix on an appropriate letter in the translated text. Each & marks a keyboard accelerator (Alt+letter). Choose a letter that makes sense and try to avoid duplicates within the same dialog/menu.
6. Preserve the Sally plural syntax exactly: {{!}} markers and the {{text|N|text|N|text}} pattern. Translate the text parts inside the curly braces but keep the numeric thresholds and pipe structure. Adapt the plural forms to {language_name} grammar.
7. Keep empty strings ("") as empty strings.
8. Do not add or remove quotes. Return just the inner text (without surrounding quotes).
9. For technical terms (path, file, directory, archive, plugin, panel), use standard {language_name} OS/software terminology.
10. Translate "[POPUP]" literally as "[POPUP]" — it is a structural marker, not user-visible text.
{context_block}

Strings to translate:
{json.dumps(items, ensure_ascii=False, indent=2)}"""

    return prompt


def translate_batch(
    entries: list[SltEntry],
    language_name: str,
    module_name: str,
    context_entries: list[SltEntry] | None = None,
    model: str = "sonnet",
) -> dict[int, str]:
    """Translate a batch of entries using claude CLI.

    Returns a dict mapping entry index (in the input list) to translated text.
    """
    import subprocess
    import shutil

    claude_path = shutil.which("claude")
    if not claude_path:
        print("ERROR: 'claude' CLI not found in PATH.", file=sys.stderr)
        sys.exit(1)

    prompt = build_translation_prompt(entries, language_name, module_name, context_entries)

    result = subprocess.run(
        [claude_path, "-p", "--model", model, "--output-format", "text",
         "--no-session-persistence"],
        input=prompt.encode("utf-8"),
        capture_output=True,
        timeout=120,
    )

    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace")
        print(f"ERROR: claude CLI failed (exit {result.returncode}): {stderr[:300]}", file=sys.stderr)
        return {}

    # Parse the response — extract JSON array
    response_text = result.stdout.decode("utf-8").strip()

    # Handle markdown code blocks
    if response_text.startswith("```"):
        lines = response_text.split("\n")
        # Remove first and last lines (```json and ```)
        lines = [l for l in lines[1:] if not l.strip().startswith("```")]
        response_text = "\n".join(lines)

    try:
        translations = json.loads(response_text)
    except json.JSONDecodeError as e:
        print(f"ERROR: Failed to parse API response as JSON: {e}", file=sys.stderr)
        print(f"Response was:\n{response_text[:500]}", file=sys.stderr)
        return {}

    result = {}
    for item in translations:
        idx = item.get("id")
        translated = item.get("translated", "")
        if idx is not None:
            result[idx] = translated

    return result


# ---------------------------------------------------------------------------
# Main processing
# ---------------------------------------------------------------------------

def process_file(
    filepath: Path,
    language_key: str,
    dry_run: bool = False,
    model: str = "sonnet",
) -> dict:
    """Process a single .slt file: translate state=0 entries.

    Returns a stats dict.
    """
    language_name = LANGUAGES[language_key]["name"]
    module_name = filepath.stem  # e.g., "automation", "sally"

    # Read file
    content = filepath.read_text(encoding="utf-8-sig")  # Handle BOM
    lines, entries = parse_slt(content)

    # Separate translated (context) from untranslated (work)
    translated_entries = [e for e in entries if e.state == 1]
    untranslated_entries = [e for e in entries if e.state == 0]

    stats = {
        "file": str(filepath),
        "total_entries": len(entries),
        "already_translated": len(translated_entries),
        "to_translate": len(untranslated_entries),
        "translated": 0,
        "warnings": [],
    }

    if not untranslated_entries:
        return stats

    if dry_run:
        print(f"\n  {filepath.name}: {len(untranslated_entries)} strings to translate")
        for e in untranslated_entries[:10]:
            print(f"    [{e.section_type} {e.section_id}] \"{e.text}\"")
        if len(untranslated_entries) > 10:
            print(f"    ... and {len(untranslated_entries) - 10} more")
        return stats

    # Translate in batches (max 100 entries per API call)
    BATCH_SIZE = 100
    all_translations = {}

    for batch_start in range(0, len(untranslated_entries), BATCH_SIZE):
        batch = untranslated_entries[batch_start:batch_start + BATCH_SIZE]
        batch_num = batch_start // BATCH_SIZE + 1
        total_batches = (len(untranslated_entries) + BATCH_SIZE - 1) // BATCH_SIZE

        if total_batches > 1:
            print(f"    Batch {batch_num}/{total_batches} ({len(batch)} strings)...", end=" ", flush=True)

        translations = translate_batch(
            batch,
            language_name,
            module_name,
            context_entries=translated_entries,
            model=model,
        )

        if total_batches > 1:
            print(f"got {len(translations)}")

        # Map batch-local indices back to global entry references
        for batch_idx, translated_text in translations.items():
            if 0 <= batch_idx < len(batch):
                entry = batch[batch_idx]

                # Validate translation
                warns = validate_translation(entry.text, translated_text)
                for w in warns:
                    warning = f"  {filepath.name} line {entry.line_index + 1}: {w}"
                    stats["warnings"].append(warning)

                # Apply translation
                entry.state = 1
                entry.text = translated_text
                lines[entry.line_index] = reconstruct_line(entry)
                stats["translated"] += 1

    # Write back
    output = "\n".join(lines)
    # Preserve original line ending style
    if "\r\n" in content:
        output = output.replace("\n", "\r\n")
    filepath.write_text(output, encoding="utf-8-sig")

    return stats


def find_slt_files(path: Path, language_key: str | None = None) -> list[tuple[Path, str]]:
    """Find .slt files to process.

    Returns list of (filepath, language_key) tuples.
    """
    results = []

    if path.is_file() and path.suffix == ".slt":
        # Single file — language must be specified or inferred from path
        if language_key:
            lang = language_key
        else:
            lang = path.parent.name
            if lang not in LANGUAGES:
                print(f"ERROR: Cannot infer language from path '{path}'. Use --language.", file=sys.stderr)
                sys.exit(1)
        results.append((path, lang))

    elif path.is_dir():
        if language_key:
            # Specific language directory
            lang_dir = path if path.name == language_key else path / language_key
            if lang_dir.is_dir():
                for f in sorted(lang_dir.glob("*.slt")):
                    results.append((f, language_key))
            else:
                print(f"ERROR: Directory not found: {lang_dir}", file=sys.stderr)
                sys.exit(1)
        else:
            # All languages under translations/
            for lang_key in sorted(LANGUAGES.keys()):
                lang_dir = path / lang_key
                if lang_dir.is_dir():
                    for f in sorted(lang_dir.glob("*.slt")):
                        results.append((f, lang_key))

    return results


def main():
    parser = argparse.ArgumentParser(
        description="Translate Sally .slt archives using Claude API",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "path",
        type=Path,
        help="Path to .slt file, language directory, or translations/ root",
    )
    parser.add_argument(
        "--language", "-l",
        choices=sorted(LANGUAGES.keys()),
        help="Target language (inferred from path if not specified)",
    )
    parser.add_argument(
        "--all-languages", "-a",
        action="store_true",
        help="Process all languages under the given directory",
    )
    parser.add_argument(
        "--dry-run", "-n",
        action="store_true",
        help="Show what would be translated without calling the API",
    )
    parser.add_argument(
        "--model", "-m",
        default="sonnet",
        help="Claude model to use (default: sonnet)",
    )
    args = parser.parse_args()

    # Determine language
    language_key = args.language
    if args.all_languages:
        language_key = None
    elif not language_key and args.path.is_dir():
        # Try to infer from directory name
        if args.path.name in LANGUAGES:
            language_key = args.path.name

    if not args.all_languages and not language_key and args.path.is_dir():
        # If pointing at translations/ root without --all-languages, require it
        if any((args.path / lang).is_dir() for lang in LANGUAGES):
            print("ERROR: Multiple languages found. Use --language or --all-languages.", file=sys.stderr)
            sys.exit(1)

    # Find files
    files = find_slt_files(args.path, language_key)
    if not files:
        print("No .slt files found.", file=sys.stderr)
        sys.exit(1)

    # Process
    total_stats = {
        "files": 0,
        "total_entries": 0,
        "already_translated": 0,
        "to_translate": 0,
        "translated": 0,
        "skipped": 0,
        "warnings": [],
    }

    current_lang = None
    for filepath, lang_key in files:
        if lang_key != current_lang:
            current_lang = lang_key
            lang_name = LANGUAGES[lang_key]["name"]
            print(f"\n{'='*60}")
            print(f"  {lang_name} ({lang_key})")
            print(f"{'='*60}")

        label = filepath.name
        if not args.dry_run:
            print(f"  {label}...", end=" ", flush=True)

        stats = process_file(filepath, lang_key, dry_run=args.dry_run, model=args.model)

        total_stats["files"] += 1
        total_stats["total_entries"] += stats["total_entries"]
        total_stats["already_translated"] += stats["already_translated"]
        total_stats["to_translate"] += stats["to_translate"]
        total_stats["translated"] += stats["translated"]
        total_stats["warnings"].extend(stats["warnings"])

        if stats["to_translate"] == 0:
            if not args.dry_run:
                print("already complete")
            total_stats["skipped"] += 1
        elif not args.dry_run:
            print(f"translated {stats['translated']}/{stats['to_translate']}")

    # Summary
    print(f"\n{'='*60}")
    print(f"  Summary")
    print(f"{'='*60}")
    print(f"  Files processed:     {total_stats['files']}")
    print(f"  Already complete:    {total_stats['skipped']}")
    print(f"  Total entries:       {total_stats['total_entries']}")
    print(f"  Already translated:  {total_stats['already_translated']}")
    print(f"  Newly translated:    {total_stats['translated']}")

    if total_stats["warnings"]:
        print(f"\n  Warnings ({len(total_stats['warnings'])}):")
        for w in total_stats["warnings"]:
            print(f"    {w}")

    if total_stats["to_translate"] > 0 and args.dry_run:
        print(f"\n  Would translate:     {total_stats['to_translate']} strings")
        print(f"  (Run without --dry-run to translate)")


if __name__ == "__main__":
    main()
