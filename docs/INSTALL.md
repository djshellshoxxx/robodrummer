# Installing RoboDrummer

Each release package contains the same plug-in in several formats:

| File | Format | Use it in |
|---|---|---|
| `RoboDrummer.vst3` | VST3 plug-in | Cubase, Ableton Live, FL Studio, Studio One, Reaper, Bitwig and most other hosts |
| `RoboDrummer.clap` | CLAP plug-in | Bitwig, Reaper, FL Studio, Studio One and other CLAP hosts |
| `RoboDrummer.component` | Audio Unit (macOS packages that include it) | Logic Pro, GarageBand, MainStage |
| `RoboDrummer.exe` / `RoboDrummer.app` / `RoboDrummer` | Standalone app | No host needed; pick your audio/MIDI devices in its Options window |

Load RoboDrummer as an **instrument**. Route your guitar into its audio input (sidechain/input bus) so it can follow your tempo and dynamics.

## Which download?

| Platform | File |
|---|---|
| Windows 64-bit | `RoboDrummer-<version>-windows-x64-setup.exe` (installer) or `RoboDrummer-<version>-windows-x64.zip` (manual copy) |
| macOS, Apple silicon (M1 and later) | `RoboDrummer-<version>-mac-arm64.zip` |
| macOS, Intel | `RoboDrummer-<version>-mac-x64.zip` |
| Linux 64-bit | `RoboDrummer-<version>-linux-x64.tar.gz` |

Optional: check the download against `SHA256SUMS.txt` on the release page (or the `.sha256` file next to it).

## Windows

Easiest: run the `-setup.exe` installer. It puts VST3 and CLAP in the shared plug-in folders and the standalone app in Program Files.

Manual install from the zip:

1. Copy the `RoboDrummer.vst3` folder to `C:\Program Files\Common Files\VST3\`.
2. Copy `RoboDrummer.clap` to `C:\Program Files\Common Files\CLAP\` (create the folder if it does not exist).
3. Run `RoboDrummer.exe` from anywhere for the standalone version.
4. Rescan plug-ins in your host.

**SmartScreen warning.** The builds are not signed with a paid code-signing certificate, so Windows may show "Windows protected your PC". Click **More info**, then **Run anyway**. If you downloaded a zip, you can also right-click the zip, open **Properties**, tick **Unblock**, and extract it again.

## macOS

1. Unzip the package. Copy `RoboDrummer.vst3` to `~/Library/Audio/Plug-Ins/VST3/`.
2. Copy `RoboDrummer.clap` to `~/Library/Audio/Plug-Ins/CLAP/`.
3. If the package has `RoboDrummer.component`, copy it to `~/Library/Audio/Plug-Ins/Components/`.
4. Copy `RoboDrummer.app` to `/Applications/`.

**Gatekeeper.** The bundles are ad-hoc signed but not notarised, so macOS may say they cannot be opened. Either:

- Right-click (or Control-click) `RoboDrummer.app` and choose **Open**, then confirm. For plug-ins, open System Settings, **Privacy & Security**, and click **Open Anyway** after the host reports the block; or
- Remove the quarantine flag once from Terminal:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/RoboDrummer.vst3 ~/Library/Audio/Plug-Ins/CLAP/RoboDrummer.clap /Applications/RoboDrummer.app
```

(Add the `.component` path too if you installed the Audio Unit.) Rescan plug-ins in your host afterwards.

## Linux

Run this from inside the unpacked `RoboDrummer-<version>-linux-x64` folder:

```bash
mkdir -p ~/.vst3 ~/.clap ~/.local/bin && cp -R RoboDrummer.vst3 ~/.vst3/ && cp RoboDrummer.clap ~/.clap/ && cp RoboDrummer ~/.local/bin/ && chmod +x ~/.local/bin/RoboDrummer && echo "RoboDrummer installed"
```

System-wide alternatives are `/usr/lib/vst3` and `/usr/lib/clap`. Linux builds are made on Ubuntu 22.04 and need a glibc at least that new, plus ALSA and X11 libraries (present on any desktop distro).
