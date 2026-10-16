# ozo NIGHTSHIFT 0.3.0 — UI build

NIGHTSHIFT is the current name of the GLINT MIDI particle delay. The existing plugin
identity, 60 parameter IDs and version-6 state protocol are retained.

## Included

- Seven harmonic lanes: unison, upper/lower diatonic thirds, fifths and octaves.
- Photographic airport editor with a shared four-beat timing-light grid, layered
  light bloom, real generated-MIDI pulses, afterglow, welcome and standby sweeps.
- Compact lane controls, full eight-parameter lane details, discrete timing knobs,
  drag-only main-panel readouts and permanent detail readouts.
- Inset lane lamps, inactive-label dimming, independent branding and idle panel fade.
- PRISM-derived preset architecture: factory/user lists, switching, local save,
  confirmed overwrite and deletion. Factory presets are read-only.
- Six presets: Init - Clear Apron, Night Taxi, Glass Canopy, Red Eye, Slow Orbit,
  Last Departure. Harmonic presets specify their key and scale.
- Airport resources are embedded; no developer filesystem paths are needed at runtime.

## Package formats

- macOS arm64: AU MIDI FX and VST3 MIDI-output instrument. macOS 12.0 deployment
  target; ad-hoc signed, not Developer ID signed or notarized.
- Windows x64: VST3 MIDI-output instrument. The CI archive contains the complete
  VST3 bundle, PE-architecture validation and SHA-256 checksum.

The build does not produce audio by itself. A receiving instrument and a DAW that
supports routing MIDI output from VST3 instruments are required. The AU uses the
MIDI Processor type.

## Validation boundaries

Recent local basic checks cover editor parameter bindings, main/detail value
synchronization, discrete beat choices and preset import/save/delete. Mac AU/VST3
bundles were built and signature-verified. Windows CI checks compilation, package
integrity and x64 PE architecture; it does not claim interactive DAW validation.

An earlier complete engine suite reported **34/40 passing with six existing
failures**. The pre-UI binary reproduced the same six failures (32/38 passing);
new observation cases account for the two additional passing cases. The failures
involve finite-reply/NoteOff-balance assumptions and pitch-boundary/ownership cases.
These were not changed as part of the UI work and are not represented as fixed.
The normal registered `glint.core` target runs the bounded smoke subset, not the
complete suite. This is a testing build, not an all-tests-passing qualification.

## Build independently

This entry point avoids dependency on other products or local repository-layout
migrations:

```sh
git clone --branch 9.0.2 --depth 1 https://github.com/juce-framework/JUCE.git JUCE
cmake -S cmake/nightshift -B build/nightshift -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/nightshift --config Release --target ozoGLINT_VST3
```

On Windows run from an MSVC x64 development environment. On macOS also build
`ozoGLINT_AU`. The product artifacts are under
`build/nightshift/plugins/glint/ozoGLINT_artefacts/Release/`.
