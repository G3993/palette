#pragma once
// Palette stub. Easel's ShaderSource::applyAudioBindings() takes a MIDIManager*
// for MidiCC bindings. Palette's desktop MIDI source lands in Cut 2; until then
// every CC reads as 0 and the binding falls back to the parameter's own value.
class MIDIManager {
public:
    float getCCValue(int /*channel*/, int /*cc*/) const { return 0.0f; }
};
