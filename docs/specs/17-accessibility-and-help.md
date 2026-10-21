# 17. Accessibility, options and Help/About (CDL 2.4 and 7)

## Purpose & user value

Make the editor usable without a mouse and without relying on colour or motion, keep tooltips optional (already done), and give users one in-repo Help document plus a complete Help/About panel. This spec records what exists (read from the code, nothing changed by this spec) and defines the remaining work. `docs/COMPLIANCE.md` currently rates 2.3 and 2.4 Partial; 7.1 and 7.2 Implemented.

## What exists today (read-only audit of the editor code)

| Area | State | Evidence |
|---|---|---|
| Tooltips on/off | Implemented, persisted | `RoboDrummerAudioProcessorEditor::applyTooltipSetting` creates/destroys a `juce::TooltipWindow(this, 600)` (600 ms delay) and writes `tooltips` to `settings.xml` in `<userApplicationData>/Circuit Drift Labs/RoboDrummer/`; SETTINGS has the "Show tooltips" toggle |
| Follow platform accessibility preference for tooltips | Not implemented | no OS query |
| Help | Implemented as an in-editor overlay | `kHelpText` in `PluginEditor.cpp` (quick start, tabs, presets, default MIDI commands, automation, troubleshooting), HELP button in the header and in SETTINGS, `showHelp()` shows a read-only multi-line `TextEditor` plus CLOSE HELP |
| About | Implemented | SETTINGS `about_` label: name + `JucePlugin_VersionString`, "Circuit Drift Labs", formats line, privacy statement (no network/telemetry/licensing/updates), real-time statement, hyperlink to the CDL website. The formats line says "VST3, CLAP, Standalone" and omits AU: **fix** |
| Colour not the only cue | Largely | state text labels, toggle states; `BarMeter` is colour/length only but adjacent text shows values (verify per panel in the review below) |
| Keyboard navigation | JUCE defaults only | no `keyPressed`, `setWantsKeyboardFocus`, `setExplicitFocusOrder` or `createFocusTraverser` in `src/plugin`. Standard JUCE widgets (`ComboBox`, `Slider`, `TextButton`, `ToggleButton`) are focusable by default; whether Tab traverses all, and whether a plug-in editor receives keys, depends on the host (many hosts swallow keys for the transport) |
| Visible focus | JUCE default focus outline only where `LookAndFeel_V4` draws it; `RoboLookAndFeel` does not customise it | `src/plugin/EditorWidgets.h` |
| Screen reader names | Not set | no `setTitle`, `setDescription`, or accessibility handler overrides; `ParamSlider`/`ParamCombo` wrap a captioned `juce::Label` and the control, so the control itself has no title |
| Reduced motion | Not provided | the only motion is the 20 Hz `timerCallback` repainting meters, confidence history and onset marks (`startTimerHz(20)`); no decorative animation |
| Scaling/HiDPI | Resizable 900x640 to 1600x1100; JUCE handles host scale | `setResizeLimits(900, 640, 1600, 1100)` |
| Right-click menus | Sliders only | see `15-combo-context-menu.md` |

## Behavior (precise)

### A. Keyboard focus and operation

1. **Focus order:** header (preset type, preset list, SAVE, DELETE, HELP), tab buttons, then the active panel's controls in reading order (left to right, top to bottom), implemented with a custom `juce::ComponentTraverser` on the editor (`createKeyboardFocusTraverser`) that returns the visible panel's focusable children sorted by position; the Help overlay, when open, traps focus (Tab cycles between the text area and CLOSE HELP; Escape closes).
2. **Visible focus:** `RoboLookAndFeel` draws a 2 px focus ring in `kAccentAlt` (contrast >= 3:1 against the panel colour; verify) on focused sliders, combos, toggles and buttons via `drawFocusOutline`-style overrides. Focus is never conveyed by colour only: the ring is a shape.
3. **Operation:** Slider: Left/Right/Up/Down fine step, PageUp/PageDown coarse, Home/End min/max, Backspace/Delete resets to default; Enter opens numeric text entry only where the slider shows a text box. Combo: Space/Enter opens, Up/Down changes. Toggle: Space toggles. Tabs: Ctrl+Tab / Ctrl+Shift+Tab cycles tabs when the editor has focus; keys 1 to 8 are **not** used (hosts use them). Context menu key / Shift+F10 opens the control menu (`15-combo-context-menu.md`).
4. **Host key capture:** document that some hosts (FL Studio, Ableton) do not forward keys to plug-in windows; the mouse-only paths must remain complete. No feature may require the keyboard.
5. **Names for assistive tech:** every parameter control sets `setTitle(caption)` and `setDescription(tooltip)` (JUCE accessibility), so screen readers announce "Intensity, slider, 70 %". Panels set `setTitle("<Tab name> tab")`. `BarMeter` sets `setTitle` and exposes its value text through a value interface or an adjacent label announced as read-only text.

### B. Options (SETTINGS tab, new group "Interface")

Stored in the existing `settings.xml` (per user, not per project, not host state). Writing must **read-modify-write** the XML so unknown attributes survive (today `applyTooltipSetting` rewrites the whole file with only `tooltips`; change before adding fields).

| Option | Key | Values | Default | Effect |
|---|---|---|---|---|
| Show tooltips | `tooltips` | on/off | on | exists |
| Tooltip delay | `tooltipDelayMs` | 300, 600, 1000 | 600 | `TooltipWindow` delay |
| Reduced motion | `reducedMotion` | on/off | follows the OS preference at first run, else off | meters and confidence history refresh at 5 Hz instead of 20 Hz; meter decay shown as steps; the onset-mark scroll in ANALYSIS freezes to snapshot-on-click ("Update" button); no other animation exists |
| High-contrast outlines | `highContrast` | on/off | off | thicker panel outlines (2 px) and text colour `kText` instead of `kDim` for captions |

Platform preference defaults: JUCE has no cross-platform reduced-motion query, so read it once with a small per-platform shim (`NSWorkspace.accessibilityDisplayShouldReduceMotion` on macOS, `SPI_GETCLIENTAREAANIMATION` on Windows, otherwise off); a failed read means off. No OS tooltip-suppression setting exists on macOS or Windows, so the user option is the control (satisfies CDL 2.4 "suppressible globally").

### C. Help / About panel contents

Replace the plain text overlay with a scrollable, sectioned Help (still a read-only JUCE component; content generated from `docs/HELP.md` at build time into a C++ string resource so the repo document and in-app help cannot drift; build step: a small CMake `file(READ)` to `configure_file` generating `HelpText.h`). About (SETTINGS, kept, plus):

1. Product name, **version string including the pre-release tag** (for example `0.4.0-beta.1`), build date/commit short hash (compile definitions set by CMake/CI), formats built into this binary.
2. Copyright and licence line; link to `LICENSE` text in the installer folder; third-party notices (JUCE, clap-juce-extensions) and their licences.
3. Privacy: no network access, telemetry, analytics, licensing or update checks (exact wording from COMPLIANCE section 8).
4. Links: website, repository issues page (beta feedback), `docs/INSTALL.md` anchor, Circuit Drift Labs site. External links open only on click.
5. "Copy diagnostics" button: copies plain text (version, format, host name via `PluginHostType`, sample rate, buffer size, OS, tracker state summary) to the clipboard for bug reports. No file or network access.
6. The unsigned/signed note (`13-code-signing-notarization.md`).

### D. `docs/HELP.md` outline (to be written in the implementation step; keep under 6 pages)

1. Quick start (the 4 steps from `kHelpText`).
2. Routing the guitar: instrument track, sidechain/input bus per host (links to `INSTALL.md`), Guitar Monitor, standalone input-mute note.
3. Timing modes and what each does; Listen/Join behaviour; what the confidence bars mean.
4. Tabs reference (one subsection per tab: LIVE, KIT, DRUMMER, STYLE, SONG, MIDI, ANALYSIS, SETTINGS).
5. Styles list with one line each; manual meter; odd meters.
6. Fills, intensity, song mode, programmed sections.
7. MIDI: default command map (notes 36 to 50, channel 16), MIDI Learn for notes/CC, expression pedal defaults, drum output (channel 10), tempo sources and MIDI clock.
8. Kits: user folder naming for velocity layers and round robin; formats.
9. Presets and sessions: types, location, host state, factory presets.
10. Controls and gestures: drag, double-click reset, right-click menu, keyboard shortcuts, tooltips and reduced motion options.
11. Troubleshooting: no sound, no lock, unstable tempo, doubled guitar, CPU, sample rate, host-specific notes (from the host matrix).
12. Privacy and licensing; how to report a bug (diagnostics button).
13. Glossary (onset, lock, hypothesis, phase, lookahead, leadership).
Also new feature sections added by the v0.3 specs (drum bus, transient shaper, riff lock, practice mode, MIDI capture, auto setup) when they are integrated.

## Parameters

None (host). UI options live in `settings.xml` as above, not in the plug-in state, and are not automatable.

## Actions/MIDI mapping

None new.

## State serialization

`settings.xml` per user: `tooltips`, `tooltipDelayMs`, `reducedMotion`, `highContrast`; missing or invalid values fall back to defaults; unknown keys preserved.

## UI placement & labels & tooltips

SETTINGS tab "Interface" group: "Show tooltips", "Tooltip delay" (combo, ms), "Reduce motion", "High-contrast outlines". Each has a tooltip stating the product effect, for example "Reduce motion: updates meters and graphs less often and stops scrolling displays." HELP button remains in the header and SETTINGS. Labels contain text and the toggle state is shown by the checkbox shape.

## Threading/real-time

Message thread only. Reduced motion changes the editor timer rate (`startTimerHz`) and never touches the audio thread. The OS preference read happens once at editor construction.

## Test plan

1. Unit/harness: settings read-modify-write keeps unknown keys; corrupt XML falls back to defaults; invalid numbers rejected.
2. Headless editor snapshot test (existing `--screenshots`): focus ring rendered on a focused slider, combo and toggle in each tab; reduced motion and high-contrast variants produce expected outputs (pixel hash or visual review recorded).
3. Focus traversal test: programmatically walk `getKeyboardFocusTraverser()->getNextComponent` through each tab; every parameter control is reached exactly once, no focus trap, Help overlay traps and releases.
4. Accessibility tree check: iterate `getAccessibilityHandler()` children; every parameter control has a non-empty title (add as a harness assertion).
5. Manual: VoiceOver (macOS), Narrator/NVDA (Windows) read the main LIVE controls; keyboard-only run of load preset, change style, start Listen, open Help, close Help in Reaper and the standalone; record which hosts forward keys in `12-host-matrix-qa.md`.
6. Contrast check of text and focus ring colours against panel colours (WCAG 2.1 AA: 4.5:1 text, 3:1 non-text).

## Acceptance criteria

* Every control reachable and operable by keyboard in the standalone and in at least Reaper; focus always visible.
* Every parameter control has a screen-reader title and description.
* Reduced motion and high-contrast options persist and work; defaults follow the platform where readable.
* Help content comes from `docs/HELP.md`; About shows version with pre-release tag and the correct format list (including AU on macOS builds).
* `docs/COMPLIANCE.md` rows 2.4 (and 2.3 together with spec 15) updated from Partial to Implemented with evidence.

## CDL compliance notes

CDL 2.4: colour not sole cue, keyboard coverage, visible focus, reduced motion option where motion is prominent (motion here is modest, option added anyway because meters repaint continuously). CDL 7.1/7.2: options persist; Help/About contain version, privacy and links. CDL 8: Copy diagnostics is local only.

## Integration hook points

`PluginEditor.cpp` (`kHelpText` replaced by generated text, `applyTooltipSetting` becomes read-modify-write, `startTimerHz` rate from reduced motion, `createKeyboardFocusTraverser` override in `PluginEditor.h`), `EditorWidgets.h` (focus ring in `RoboLookAndFeel`, `setTitle/setDescription` in `ParamSlider`/`ParamCombo`/`ParamToggle`, `BarMeter` accessibility), `EditorPanels.h` `SettingsPanel` (Interface group, About text), `CMakeLists.txt` (help text generation, version/commit definitions), `docs/HELP.md` (new).
