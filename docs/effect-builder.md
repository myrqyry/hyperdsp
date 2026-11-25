# Effect Builder direction (future concept)

User proposed an audio Effect Builder dock analogous to HyperShader's Shader Builder. This is a design direction, not a shipped feature or a change in immediate native-effect priorities.

## Intended experience

A dock for composing, previewing, saving and applying reusable deterministic audio effects, not just browsing plugins or changing one filter's parameters. A compact dark, dockable interface with an editable signal graph, searchable blocks, a parameter inspector and a visible modulation layer. Expose selected graph parameters as controls on the resulting effect; allow effect presets and portable versioned graph definitions. Consider a script editor when an appropriate DSP language and license are qualified.

Reuse HyperShader's proven frontend ideas (Qt WebEngine dock, narrowly scoped native bridge, drafts/recovery, explicit export/apply and validation feedback) after inspecting their actual implementation. Do not assume shader preview execution is suitable for audio. Avoid mandatory WebEngine or frontend API dependencies in the headless DSP module.

## Effect discovery

User wants external effects easy to find. The eventual library should bring native blocks, installed third-party effects, community HyperDSP definitions and discoverable upstream collections into one searchable surface, showing format, compatibility and installed/available state. Reuse the current LADSPA catalog scanner and versioned metadata; do not load unknown native libraries merely to browse an online listing. Acquisition should be explicit and provenance visible. Prefer package-manager or official-source links before implementing any downloader. Favorites, tags and author-defined controls should work across supported backends. Unsupported formats can be discoverable as clearly labeled unavailable, never silently loaded.

The shared native/installed metadata inventory and JSON export are implemented (see [effect library contract](effect-library.md)); the graphical library, favorites and acquisition workflow are still future work. An initial executable [effect graph](effect-graph.md) supports all 38 native modes and gain, fixed branch alignment, and shared parameter definitions. External node adapters, sidechains, graph modulation, exposed controls and editor remain future work.

## Proposed implementation order

1. Expand native DSP building blocks (dynamics, saturation, delay, modulation/time effects, reverb).
2. Establish backend-neutral parameter metadata, effect graph schema and bounded serial/parallel routing, explicit sidechain inputs and latency accounting. Validate graphs off the audio thread; prepare resources off callback, publish a safe prepared graph, keep last working graph on errors. Feedback loops need explicitly bounded delay semantics; arbitrary cycles are rejected.
3. Add reusable presets, graph import/export and deterministic offline/real OBS audio verification. Establish safe preview routing, levels and bypass; do not silently alter program audio.
4. Add optional frontend dock: nodes, connections, parameter inspector, live waveform/spectrum and response visualization, modulation mapping, draft recovery, explicit preview/apply. Reuse existing native and LADSPA adapters rather than implementing DSP in the UI.
5. Extend to additional qualified effect hosts and optional script authoring.

Sight and Sound provide optional data inputs to modulation, not DSP nodes that load models. Aftermarket remains responsible for OBS interaction/routing, not effect execution. No new mandatory neural or GUI dependency.

The initial graph has a versioned JSON contract; UI framework, portable external identities and script language remain open pending real runtime and licensing evidence.
