# User Guide

- **Install:** see [installation](../INSTALL.md) and [0.4.0 release notes](../RELEASE_NOTES_0.4.0.md).
- **In-app help:** the Help/About screen is generated from [HELP.md](../HELP.md).
- **Modes:** Drummer Leads, Hybrid and Guitarist Leads. Tempo Source selects Host / Internal, Internal BPM only or MIDI Clock.
- **Optional MIDI control:** fills, section changes, intensity, half/double time, crash, break, stop/resume and listening reset. The [Help MIDI map](../HELP.md#midi) lists the default note commands and learnable bindings.
- **Tabs (0.4.0):** LIVE, KIT, DRUMMER, STYLE, SONG, PRACTICE, MIDI, ANALYSIS, SETTINGS.
- **Parameters:** [generated parameter reference](../PARAMETERS.md).

Practice provides a count-in, bar or section-range loops, and a tempo trainer. Default MIDI **note numbers** on **channel 16** are:

| Note number | Action |
| --- | --- |
| 51 | Practice on/off |
| 52 | Loop on/off |
| 53 | Trainer on/off |
| 54 | Trainer restart |
| 55 | Count-in on/off |

Use Learn on the MIDI tab to change a command's note/channel binding. See [practice Help](../HELP.md#practice-mode) for operation and [the practice specification](../specs/04-practice-mode.md) for timing behavior.

Native macOS CI builds AU and runs `auval`; Linux and Windows CI run VST3 `pluginval`. Automated validation is distinct from real guitar, DAW-host, clean-machine and screen-reader qualification. Consult [QA](../QA.md) for dated results and open checks, and [compliance](../COMPLIANCE.md) for release requirements.
