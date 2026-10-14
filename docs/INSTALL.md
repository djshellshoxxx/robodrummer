# Installing RoboDrummer

Each release package contains three builds of the same plug-in:

| File | Format | Use it in |
|---|---|---|
| `RoboDrummer.vst3` | VST3 plug-in | Cubase, Ableton Live, FL Studio, Studio One, Reaper, Bitwig and most other hosts |
| `RoboDrummer.component` | Audio Unit (macOS only) | Logic Pro, GarageBand, Ableton Live, MainStage |
| `RoboDrummer.clap` | CLAP plug-in | Bitwig, Reaper, FL Studio, Studio One and other CLAP hosts |
| `RoboDrummer.exe` / `RoboDrummer.app` / `RoboDrummer` | Standalone app | No host needed; pick your audio/MIDI devices in its Options window |

Load RoboDrummer as an **instrument**. Route your guitar into its audio input (sidechain/input bus) so it can follow your tempo and dynamics.

## Windows

**Installer (recommended):** run `RoboDrummer-<version>-Windows-x64-Setup.exe`. It installs the VST3 to `C:\Program Files\Common Files\VST3`, the CLAP to `C:\Program Files\Common Files\CLAP` and the standalone app to `C:\Program Files\Circuit Drift Labs\RoboDrummer`; every component is optional. The installer is not code-signed yet, so Windows SmartScreen may ask you to confirm ("More info" → "Run anyway").

**Manual (zip):**

1. Copy the `RoboDrummer.vst3` folder to `C:\Program Files\Common Files\VST3\`.
2. Copy `RoboDrummer.clap` to `C:\Program Files\Common Files\CLAP\` (create the folder if it does not exist).
3. Run `RoboDrummer.exe` from anywhere for the standalone version.
4. Rescan plug-ins in your host.

## macOS

1. Copy `RoboDrummer.vst3` to `~/Library/Audio/Plug-Ins/VST3/`.
2. Copy `RoboDrummer.clap` to `~/Library/Audio/Plug-Ins/CLAP/` and `RoboDrummer.component` to `~/Library/Audio/Plug-Ins/Components/`.
3. Copy `RoboDrummer.app` to `/Applications/`.
4. The builds are not notarised yet. If macOS blocks them, remove the quarantine flag once:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/RoboDrummer.vst3 ~/Library/Audio/Plug-Ins/CLAP/RoboDrummer.clap /Applications/RoboDrummer.app
```

## Linux

Run this from inside the unpacked `RoboDrummer-Linux-x64` folder:

```bash
mkdir -p ~/.vst3 ~/.clap ~/.local/bin && cp -R RoboDrummer.vst3 ~/.vst3/ && cp RoboDrummer.clap ~/.clap/ && cp RoboDrummer ~/.local/bin/ && chmod +x ~/.local/bin/RoboDrummer && echo "RoboDrummer installed"
```
