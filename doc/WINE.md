# Running Sally on Linux with Wine

Sally (`sally.exe`) is a native 64-bit Windows application and runs well under
[Wine](https://www.winehq.org/) — **no Windows and no virtual machine required**.

Sally does **not** need .NET, Wine Mono, or Wine Gecko. If Wine offers to install
those on first launch, just click **Cancel**. The runtime ships its own Visual C++
libraries (`msvcp140.dll`, `vcruntime140.dll`, `concrt140.dll`, ...), so no separate
VC++ redistributable is required.

## Verified Environment

| Component | Value |
|-----------|-------|
| OS        | Ubuntu 24.04 LTS (x86_64) |
| Wine      | WineHQ **wine-11.0** stable (`winehq-stable`) |
| Sally     | 1.0.22 (x64) |

Any reasonably recent Wine (8.x or newer) should work. The screenshot in the
[README](../README.md#running-on-linux-with-wine) shows this exact setup.

## 1. Install Wine

### Option A — WineHQ official build (recommended)

This is the tested path.

```bash
# Wine needs 32-bit support even for 64-bit apps
sudo dpkg --add-architecture i386

# Add the WineHQ repository key and source
sudo mkdir -pm755 /etc/apt/keyrings
sudo wget -O /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key
sudo wget -NP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/ubuntu/dists/noble/winehq-noble.sources

sudo apt update
sudo apt install --install-recommends winehq-stable
```

> Ubuntu 24.04 is codename **noble**. For other releases, replace `noble` in the
> URLs above with your codename (`lsb_release -c`), e.g. Debian 12 = `bookworm`.
> See the [WineHQ Debian/Ubuntu guide](https://gitlab.winehq.org/wine/wine/-/wikis/Debian-Ubuntu).

### Option B — distribution package (simpler, may be older)

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install wine wine64
```

Verify the install:

```bash
wine --version      # e.g. wine-11.0
```

## 2. Get Sally

Download the latest x64 runtime zip straight from
[GitHub Releases](https://github.com/0xeb/sally/releases/latest) and unpack it,
keeping `plugins/`, `lang/`, `toolbars/`, and `utils/` next to `sally.exe`:

```bash
VER=1.0.22   # latest release — see https://github.com/0xeb/sally/releases/latest
wget https://github.com/0xeb/sally/releases/download/v$VER/Sally-v$VER-x64.zip
unzip Sally-v$VER-x64.zip -d Sally-$VER
cd Sally-$VER
```

## 3. Run Sally

```bash
wine sally.exe
```

To launch it detached so it does not tie up the terminal:

```bash
WINEDEBUG=-all nohup wine sally.exe >/dev/null 2>&1 &
```

- `WINEDEBUG=-all` silences Wine's diagnostic output.
- The first launch is slower because Wine initializes its prefix (`~/.wine`).
  Subsequent launches are fast.

## 4. Optional: a `sally` launcher command

Drop this wrapper on your `PATH` (e.g. `~/.local/bin/sally`), adjusting the path
to wherever Sally lives:

```bash
#!/bin/bash
cd ~/tools/sally || exit 1
WINEDEBUG=-all nohup wine sally.exe "$@" >/dev/null 2>&1 &
```

Then `chmod +x ~/.local/bin/sally` and just type `sally`.

## Troubleshooting

- **"Wine Mono / Gecko installer" dialog on first run** — click **Cancel**. Sally
  does not use .NET or an embedded browser engine.
- **Nothing happens / no window** — run `wine sally.exe` directly (without `nohup`)
  in a terminal to see any error output.
- **Plugins missing** — make sure the `plugins/` folder sits in the same directory
  as `sally.exe`. The launcher wrapper `cd`s into that directory first so relative
  paths resolve.
