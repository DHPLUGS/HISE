# HALO-2 compatibility review of the APEX changes

**Date:** 2026-11-09
**Range reviewed:** `da1589ed2..1d2d67d28` on `apex` (140 commits, ~115 files). `da1589ed2` is the HISE commit HALO-2 2.0.6 was last exported with (2026-08-28).
**Question:** APEX features were meant to be guarded by flags only APEX sets. Does HALO-2, built from the same HISE without those flags, still behave exactly as before?

**Status (updated 2026-11-09):** Acted on. #2, #3 and #8 are gated behind per-project flags (`d79be6f58`), the LFO Delay regression is fixed (`1d2d67d28`), and APEX opts in to all three flags (APEX `71bea020`). The owner rebuilt HISE and confirmed HALO and APEX both behave as expected. See "What was done" at the end.

**Method:** Four read-only code reviews covered tempo/LFO/envelopes, modulation/engine, DSP and UI/scripting/presets. Each finding was then checked against HALO's real module tree (`XmlPresetBackups/HALO 2.0.6.xml`), its UI (`HALO 2.0.6UIData/...Desktop.xml`) and its scripts. Nothing was built or run for this review. Binary user presets (.hip/.preset contents) were not inspected beyond a `RequiredExpansions` grep.

## Flags

HALO-2 defines: `HISE_SAMPLE_DIALOG_SHOW_INSTALL_BUTTON=1`, `HISE_SAMPLE_DIALOG_SHOW_LOCATE_BUTTON=0`, `ENABLE_ALL_PEAK_METERS=1`, `HISE_USE_OPENGL_FOR_PLUGIN=1`, `HISE_DEACTIVATE_OVERLAY=1`, `HISE_DEFAULT_OPENGL_VALUE=1`, `HISE_USE_EXTENDED_TEMPO_VALUES`, `HISE_INCLUDE_BEATPORT=1`, `USE_SCRIPT_COPY_PROTECTION=1`, `JUCE_ALLOW_EXTERNAL_UNLOCK=1`, `JUCE_VST3_CAN_REPLACE_VST2=1`.

APEX adds:
- **Tempo:** `HISE_TEMPO_LIST=3`
- **EQ:** `HISE_USE_SVF_FOR_CURVE_EQ`
- **Module/voice counts:** the `HISE_NUM_*` / `NUM_HARDCODED_*` counts, `NUM_POLYPHONIC_VOICES=16`
- **Parameters:** `JUCE_FORCE_USE_LEGACY_PARAM_IDS=1`
- **Voice behaviour:** `HISE_FREEZE_MONO_MODS_WITHOUT_VOICES=0`, `HISE_SUSPENSION_TAIL_MS=2000`, `HISE_HOLD_MODS_WITHOUT_VOICES=1`
- **Presets:** `CONFIRM_PRESET_OVERWRITE=0`, `HISE_ALL_NOTES_OFF_ON_USER_PRESET_LOAD=0`, `HISE_USER_PRESETS_IN_MUSIC_FOLDER=1` (macOS)
- **Sample banks:** `HISE_APEX_BANK_FORMAT=1`
- **Display:** OpenGL off

## Confirmed safe for HALO

- **Tempo list.** `HISE_USE_EXTENDED_TEMPO_VALUES` maps to `HISE_TEMPO_LIST=1`, which is the exact old 24-entry list in the old order, names and factors.
  - In an export, the bare `/D` define compiles as 1.
  - In the HISE app, the runtime switch (`HiseSettings.cpp`, `BackendProcessor::refreshExpansionType`) treats the bare define with no value as on.
  - Every former enum-as-index cast now goes through `getNearestIndex`/`...At` and gives the same numbers under list 1.
- **LFO Delay parameter.** It is appended after `IgnoreNoteOn`, so no attribute index moved. Old presets without it load as "no delay", but only since `1d2d67d28`. Before that fix (from `62880daa4`, 2026-10-07) a missing attribute loaded as 0, which a synced LFO reads as tempo list position 0, the shortest note. Every tempo-synced LFO in HALO (11 of 15) started a split second late on each note. The fix loads a missing `Delay` as -1 when synced and 0 ms when free.
- **`tempoFamilyLock`, 1/128 values.** These exist only in list 3.
- **Flag-guarded paths.**
  - `HISE_ALL_NOTES_OFF_ON_USER_PRESET_LOAD` defaults to 1, which is identical to the old behaviour.
  - The bank provider is only compiled when `HISE_APEX_BANK_FORMAT` is set (it defaults to `USE_BACKEND`).
  - The Music-folder presets, the hold/freeze mods, hold-to-drag and `macroOnlyTarget` are all guarded.
  - The `NUM_*` arrays are indexed safely at stock values.
- **Built-in filters/EQ.** The `MultiChannelFilter` base is untouched, so PolyFilter, CurveEq, ParametricEQ and HarmonicFilter behave as before. `HISE_USE_SVF_FOR_CURVE_EQ` gates nothing new.
- **`range.h`.** The skew clamp is the same within 0..1, so no knob mappings shift.
- **Interpreter.** The changes are debug/editor only.
- **Engine API.** Nothing was removed.
- **Script components.** New ScriptComponent properties default to their old values, so saved UI JSON is unchanged.
- **JUCE bump.** The relevant changes are macOS-only, or the repaint hook, which is off unless enabled.

## Unguarded changes that reach HALO, with decisions

| # | Change | Commit | HALO exposure | Decision |
|---|---|---|---|---|
| 1 | `matchesAvailableExpansions` returns true when 0 expansions are installed (fast path) | eb60943de | Only matters for presets carrying `RequiredExpansions`. **None of HALO's presets do** (expansion presets live in the expansion folders). | **No action** |
| 2 | Arpeggiator adds the incoming transpose to arp notes. Step wraps when the step count drops. | 4a33eccf9, d26147cb2 | HALO has 5 arps under a root `Transposer1` driven by the **MASTER TRANSPOSE** slider. Before: arp notes ignored MASTER TRANSPOSE (`Synth.addNoteOn` skips the chain). Now: arp follows it. A fix, but audible for users with transpose set plus the arp. | **Gated** (`HISE_ARP_CARRY_TRANSPOSE`, `d79be6f58`). TRANSPOSE is saved in presets: seven CYBERFUNK SEQUENCES presets have the arp on with TRANSPOSE -12 and would have dropped an octave. The step wrap is a plain fix and stays ungated. |
| 3 | LFO Custom tables publish a playhead, and `TableEditor` runs in "playhead mode" | 7ddf3b675, f9afde373 | HALO has 6 Custom LFOs. Details below. | **Gated** (`HISE_TABLE_PLAYHEAD`, `d79be6f58`) |
| 4 | LFO Saw holds its last value over the final 1/512 of the cycle instead of ramping back | b7d57b6e1 | Inaudible on filter LFOs. Fixes a chorus glitch. | **No action** |
| 5 | `jdsp.jdelay` DelayTime ramps across the block instead of stepping (same commit: `core.gain` with Smoothing 0) | d34d3a4c3 | `delay_sfx` has 4 jdelays. A one-block glide on tempo/knob changes; steady state is identical. All HALO `core.gain` nodes use Smoothing 20 (unchanged path). | **No action** |
| 6 | Table curve range widened from 0..1 to -1..2 (editor right-drag/touch, `setTablePointsFromArray` no longer clamps) | b1d4d122c | Any HALO table. Curves saved beyond 0..1 sound different in older builds. | **Left ungated.** Existing presets are identical; the only risk is a preset made in a new build with an extreme curve played in an older build. `HISE_EXTENDED_TABLE_CURVES` remains an option. |
| 7 | ScriptPanel `mouseWheelMove` forwards to its parent | 164aa5da8 | HALO has 5 viewports and no script wheel handlers. The wheel over a panel inside a viewport now scrolls it, which is the expected behaviour. | **No action** |
| 8 | Script floating tile with an opaque `bgColour` becomes an opaque component and fills its background | f28368b58 | Only `FloatingTile13` (CustomSettings, `bgColour=0xFF...`) now paints as a solid box. | **Gated** (`HISE_OPAQUE_FLOATING_TILES`, `d79be6f58`). Zeroing the tile's `bgColour` in HALO was rejected: a HALO script comment says the tile's combo boxes paint with it. |
| 9 | Ring buffer stops sending updates once static. The EQ FFT overlay does not use the `isSettled()` step that `Analyser.h` uses. | f28368b58 | `EQANA` → `ANAEQ` (the only EQ with `FFTEnabled=1`). The spectrum may freeze mid-decay after note-off. Looks fine in testing. | **No action for now**. Proper fix: mirror `isSettled()` in `FilterDragOverlay`. |
| 10 | CSS popup @font-face, submenu parent ticks, FilterGraph easing (45 ms via DisplayLinkClock), meter threshold, performance label at 1 s | 67a942c57, 9b6674d33, 7ddf3b675 | HALO uses no CSS. The easing is acceptable. | **No action** |

### #3 in detail: table playhead mode

- `LfoModulator` (`LFOModulator.cpp` ~833) calls `Table::publishPlayhead(...)` every block for Custom waveforms. A `TableEditor` whose table has published a playhead switches its ruler to **playhead mode**: it is driven by `DisplayLinkClock`, extrapolated between blocks and **drawn natively** in `TableEditor::Ruler::paint`. The colour is `rulerColour`, which is the ScriptTable `textColour`.
- That branch **bypasses the look-and-feel `drawTableRuler` entirely**, so HALO's `Scripts/HLAF TABLES.js` no longer applies:
  - On Table1/2 it drew a faint `bgColour` 0.1 band plus a 0.5 px line (the old "grey" look). Now there is a solid white line.
  - On the other tables (modulators panel, Table 4, etc.) it drew nothing, to hide the ruler. Now they show one.

  **A look-and-feel cannot fix this.**
- The playhead is shown only while `keysPressed > 0`. HALO probably sends unmatched note-offs (glide script / arps), which would drive the count to 0 while a key is still held, so the playhead vanishes with overlapping notes. This is suspected, not traced.
- The same change lowered the LFO table-update `inputMerger` limit from 10 to 1. That only costs CPU.
- **Gate point:** wrap the `publishPlayhead` call (and restore the merger limit) in the flag. `Table::playhead.active` is only ever set by `publishPlayhead`, so the editor then never enters playhead mode and falls back to the old look-and-feel path.

## Unguarded, but HALO does not use the feature today

These matter for other projects, and for HALO if it ever adopts the module:

- `dynamics.ott` lost its 0 dBFS ceiling on the downward branch (3b25e3352). That is a large level change. HALO's OTT network uses `dynamics.updown_comp`, which is unaffected.
- `container.branch` resets the incoming child on an index change, which cuts tails (b959981e5). HALO has no branch containers.
- Comb filter Hermite tap (65eb6e1c2), `filters.apex` per-sample glide and constant-peak band pass (400684d5d, 7ab0b89d5). These are fork-only nodes.
- `FlexAHDSR`: the attack level is no longer clamped to sustain, Loop mode cycles A-H-D, and release 0 ends the voice (cbd11790b, c15e8ad89). HALO has no flex_ahdsr.
- `KeyModulator` includes the transpose amount (bee566822). HALO has no KeyNumber modulator.
- ModulatorChain:
  - The no-voice `getOneModulationValue` starts from the chain's initial value instead of 1.0 (13b9a6fea). A Pan/Offset chain that holds only voice-start/envelope mods goes to 0.
  - Hardcoded FX modulation is clamped to 0..1 (`ModulatorChain.h` ~1101).
  - Flipping an envelope's bypass on an FX chain no longer calls allNotesOff (b70b0ffdc, ba7ef6618).
- `setFXOrder` no longer un-bypasses the kept effects (066be678e). HALO doesn't call it.
- MatrixModulator / GlobalModulatorContainer matrix behaviour: the right-click edit callback, `Bypassed` connections, re-folding of bypassed siblings. HALO has no MatrixModulator.
- `MainController` outputs silence for any block with a non-finite sample (d49e21601). This applies to every project but is inaudible unless the output was already broken.
- `setHardcodedAttribute` clamps to the parameter range and stores the clamped value (f68eb8ec9). Low impact.
- Table `setTablePoint` curve range (see #6).
- An out-of-range tempo index (≥ list size) now clamps to the last entry instead of falling back to 1/4.

## Bugs found (independent of HALO)

- **High: envelope voice slots (1404ad751).** `GlobalModulatorContainer::EnvelopeData::getModulationSignalStatic` publishes `resetFlag` but never `slotEventIds`. `mod_voice_checker::getSlotForEvent` therefore falls back to `eventId % N`, while GMC now places notes in probed slots. When two notes collide on a slot, ending one can kill the other, which is the bug the commit set out to fix. This affects APEX if it uses global envelopes with voice checkers.
- **Suspected crash risk (e02eb3eaf).** `SlotFX.setEffect` on a bypassed HardcodedMasterFX skips the ScopedTicket, the voice kill and `freeToGo`. It relies on locks inside the node swap while audio keeps running.
- **Low (4f81b0c40).** In `ScriptModulationMatrix.cpp` ~676, `ADD_CALLBACK_DIAGNOSTIC(editCallback, setEditCallback, 2)` checks argument index 2, but the function is at index 1, so the IDE diagnostic never fires. There is no runtime effect.
- **Suspected: ABI change.** `modulation::EventData` gained a member, which changes its layout across the DLL boundary. **Rebuild HALO's network DLL** before using it in the new HISE app. Exported plugins are unaffected.
- **Low.** `EqComponent` FFT freeze (see #9). `Delay.h` ~75 computes the right delay from `syncTimeLeft`; this bug predates the range.

## What was done (2026-11-09)

1. **LFO Delay fix** (`1d2d67d28`). See "Confirmed safe" above. A HALO project saved in a HISE build between `62880daa4` and `1d2d67d28` would have `Delay="0"` written into its synced LFOs; HALO's `HALO 2.0.6.xml` was last saved 2026-08-28 and has no `Delay` attribute, so it is clean. `Presets/Autosave_1.hip` (2026-11-09 10:23) was written by a pre-fix build and may carry the bad value: don't restore from it.
2. **Three gates** (`d79be6f58`), all defaulting to 0:
   - `HISE_TABLE_PLAYHEAD` (#3): wraps the `publishPlayhead` call in `LfoModulator` and restores the `inputMerger` limit to 10 when off.
   - `HISE_ARP_CARRY_TRANSPOSE` (#2): the arp adds `incomingTranspose` only when on.
   - `HISE_OPAQUE_FLOATING_TILES` (#8): both `setOpaque` sites in `ScriptComponentWrappers.cpp`.
3. **The design question is settled: a runtime read.** `ProjectBehaviour` (`hi_tools/hi_tools/MiscToolClasses.h`) holds the flags as atomics initialised from the compile-time defines, so an export uses its own value. The HISE app calls `ProjectBehaviour::setFromDefinitions` at the same two points as the tempo list (`HiseSettings.cpp` on an extra-definitions change, `BackendProcessor::refreshExpansionType` at startup and project switch). A flag the open project does not define is off. One HISE build therefore shows APEX with the features and HALO without. New APEX-only behaviour should use this pattern rather than a bare compile-time define.
4. **APEX opts in** (APEX `71bea020`): all three flags set to 1 in `project_info.xml`, Windows and OSX.
5. **Checked by the owner in a rebuilt HISE:** HALO and APEX behave as expected.

## Still open

1. HALO regression checks after the next export:
   - A tempo-synced LFO starts the moment a note is played.
   - A CYBERFUNK SEQUENCES preset plays at its 2.0.6 octave.
   - The Custom LFO table editors: ruler look, hidden tables.
   - The preset browser with no expansions installed.
   - `delay_sfx` with tempo changes.
   - `FloatingTile13`.
2. Rebuild HALO's network DLL before using it in the new HISE app (see the ABI note under "Bugs found").
3. Don't edit HISE sources while a HALO export is compiling (the export compiles these modules).
4. Optional follow-ups: the GMC `slotEventIds` bug, the `setEffect` bypassed-swap locking, the `isSettled()` step for the EQ overlay (#9), the diagnostic arg index, `HISE_EXTENDED_TABLE_CURVES` (#6).
