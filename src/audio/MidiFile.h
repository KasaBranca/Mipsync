#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace MipsyncEngine {
struct MidiEvent { uint32_t timeUs = 0; uint8_t status = 0, data1 = 0, data2 = 0; };
struct MidiSequence { std::vector<MidiEvent> events; uint32_t durationUs = 0; };
bool IsMidiPath(const std::string& path);
bool ParseMidi(const std::vector<uint8_t>& bytes, MidiSequence& out, std::string& error);
bool ReadMidi(const std::string& path, MidiSequence& out, std::string& error);
// A compact built-in eight-tone bank shared by desktop previews and PS1 SFX.
float MidiTone(unsigned int family, double phase);
std::vector<int16_t> RenderMidi(const MidiSequence& midi, uint32_t sampleRate = 44100);
} // namespace MipsyncEngine
