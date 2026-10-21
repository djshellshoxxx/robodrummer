# 15. Right-click menu for combo boxes and toggles (CDL 2.3)

Status: Implemented (ParamCombo, ParamToggle and ParamSlider share `showParamContextMenu`)

## Purpose & user value

Sliders already offer a right-click menu (reset to default, copy value, paste value; `ParamSlider::mouseDown`, `src/plugin/EditorWidgets.h`). Combo boxes and toggles have none, which `docs/COMPLIANCE.md` lists as Partial for CDL 2.3 and the audit lists as P3. This spec gives choice and toggle parameters the same predictable gestures: reset to default, copy and paste of the value (validated), and MIDI Learn entries only where a mapping really exists.

## Behavior (precise)

### Widgets in scope (`src/plugin/EditorWidgets.h`)

* `ParamCombo` (class at the `ParamCombo` definition; owns `juce::ComboBox combo_`, `APVTS::ComboBoxAttachment attachment_`).
* `ParamToggle` (derives from `juce::ToggleButton`; owns `APVTS::ButtonAttachment attachment_`).
* Not in scope: `ParamSlider` (unchanged), `BarMeter`, plain `juce::ComboBox`/`juce::ToggleButton` members that are not parameter-bound (Song panel rows `OverrideRow`, `style_/feel_/transition_/personality_` in `EditorPanels.h`, the MIDI target combo, tab buttons, command buttons). Those have no single host parameter to reset or copy. They may adopt the helper later.

### Menu content

Shown on right click (`e.mods.isPopupMenu()`), and on the keyboard context-menu key / Shift+F10 where JUCE reports it (see `17-accessibility-and-help.md`).

| Item | Shown when | Action |
|---|---|---|
| Reset to default | always | Set the parameter to its default via begin/end change gesture (host-visible, undoable in hosts that record gestures) |
| Copy value | always | Copy the current option's display text for combos ("Rock"), or `On`/`Off` for toggles, to the system clipboard |
| Paste value | always | Parse the clipboard text; apply only if valid (rules below) |
| separator | when MIDI items are shown | |
| MIDI Learn | the parameter id is in `robodrummer::kLearnTargets` (34 targets, `PluginProcessor.cpp`) | Arms CC learn for that target: `processor.getMidiLearn().armTarget(index)` (`MidiLearnMap::armTarget`, the call the MIDI tab "Learn CC" uses; `armedTarget()` tells whether this target is armed) |
| Clear MIDI mapping | the target has a CC bound (`ccForTarget(index) >= 0`) | `unbindTarget(index)` |
| Cancel MIDI Learn | this target is currently armed | `cancelLearn()` |

MIDI items are omitted entirely (not disabled) when the parameter is not a learn target, per CDL 2.3 "do not show nonfunctional menu items". Learn targets that include choice and switch parameters today: Timing Mode (`leadershipMode`), Jam Style, Output Mode, Drummer Personality, Kit, Tempo Source, Follow Response, Fill Length, Programmed Arrangement (`arrangementEnabled`), Guitar Silence, Learn This Jam (`jamMemoryEnabled`). Mute/solo toggles and manual-meter controls are not learn targets and show no MIDI items. When a parameter becomes a learn target in future its menu gains the items automatically because the check is data driven.

### Paste validation (never silently alters)

* Combos: trim; match case-insensitively against the choice list text, or accept a decimal integer index only if it is within `[0, numChoices)` expressed as the plain index (1-based entries are rejected to avoid ambiguity). Anything else leaves the value unchanged.
* Toggles: accept (case-insensitive) `on`, `off`, `true`, `false`, `1`, `0`. Anything else is rejected.
* On reject: the value does not change and no parameter change or host gesture is emitted. The Paste item stays enabled so the behaviour is predictable.
* The pasted text from a different parameter (for example a slider number into a combo) is rejected unless it satisfies the rules; there is no cross-type coercion.

### Gestures documented

Add to the Help text (`docs/HELP.md`, `17-accessibility-and-help.md`): double-click on sliders resets; right-click on any parameter control opens its menu.

## Parameters

None (UI only).

## Actions/MIDI mapping

Reuses existing MIDI Learn: arming a target from the context menu is identical to choosing the target in the MIDI tab and pressing "Learn CC". The result appears in the MIDI tab binding list.

## State serialization

None. Learn bindings are already serialized by the existing MIDI map state.

## UI placement & labels & tooltips

* Menu labels exactly as in the table; keyboard accelerators not required.
* Menu styling follows `RoboLookAndFeel` PopupMenu colours.
* Tooltips of the widgets gain a trailing sentence only if tooltips remain concise: "Right-click for reset, copy and paste." Do not repeat on every control if width is limited; the Help panel documents it.
* State shown by text, not colour only: armed learn state appears as the menu item "Cancel MIDI Learn" and in the MIDI tab as today.

## Threading/real-time

Message thread only. Parameter changes go through the `AudioProcessorParameter` API with `beginChangeGesture`/`setValueNotifyingHost`/`endChangeGesture` (same as the MIDI learn drain). No new audio-thread code. Menus are asynchronous (`showMenuAsync`) with a `SafePointer` guard, as in `ParamSlider`.

## Implementation sketch

1. New small helper in `EditorWidgets.h`: `inline void showParamContextMenu(juce::Component& target, juce::RangedAudioParameter& parameter, RoboDrummerAudioProcessor* learnHost, std::function<juce::String(float)> toText, std::function<bool(const juce::String&, float&)> fromText)`.
2. `ParamCombo` and `ParamToggle` take an optional `RoboDrummerAudioProcessor*` (default null = no MIDI items), store the `RangedAudioParameter*` from `state.getParameter(id)`, and call the helper from `mouseDown` (`combo_.addMouseListener(this, true)` like `ParamSlider`; for `ParamToggle` override `mouseDown` and skip the base class action for popup clicks).
3. Panels pass the processor so MIDI items appear: constructor arguments in `EditorPanels.h` are updated by the integrator (this repo's rule: the worker does not edit `EditorPanels.h`).
4. Keep the helper free of `PluginProcessor.h` by passing small lambdas (`findLearnTarget(id) -> int`, `arm(int)`, `ccFor(int)`, `unbind(int)`), so `EditorWidgets.h` stays light.

## Test plan

* Headless harness (`tests/harness/PluginHarness.cpp`) or a small editor test: construct each widget kind, invoke the helper's pure functions (`fromText` parsing, `toText`, reset target, learn-target lookup) without showing a menu. Cases: valid and invalid choice names, case variants, index in/out of range, NaN-like strings, empty clipboard, very long strings, `on/off` variants, reset value equals `getDefaultValue`.
* Assert that rejected pastes do not change the parameter value or fire a gesture (attach a `ParameterListener` counter).
* Assert MIDI items appear only for ids in `kLearnTargets` (iterate all combo/toggle ids in the panels).
* Manual: right click each combo and toggle on every tab at the minimum editor size; menu stays on screen; Escape closes it.

## Acceptance criteria

* Every `ParamCombo` and `ParamToggle` offers Reset, Copy and Paste; invalid paste never changes anything.
* MIDI Learn / Clear items appear only for learn targets and work end to end with a hardware or virtual controller.
* `docs/COMPLIANCE.md` row 2.3 changes from Partial to Implemented; the audit backlog item P3 is closed.
* Zero new audio-thread allocations (audit unchanged).

## CDL compliance notes

CDL 2.3: context menu provides reset and copy/paste with type validation; MIDI Learn shown only where mapping exists; no nonfunctional items. CDL 2.4: menu reachable by keyboard where the platform provides the context-menu key.

## Integration hook points

`EditorWidgets.h` (new helper, `ParamCombo`, `ParamToggle`), `EditorPanels.h` constructors (pass the processor pointer), `PluginProcessor.h` accessors already public (`getMidiLearn()`, `kLearnTargets`). Order: widget constructed on the message thread; all menu callbacks on the message thread.
