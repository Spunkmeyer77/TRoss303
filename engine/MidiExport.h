// TRoss 303 — Standard MIDI File export of patterns.
// Copyright (C) 2026 Ticco Ross. AGPL-3.0-or-later (see LICENSE).
//
// The notes are written the way TRoss 303 (and the TB-03/TD-3) read MIDI back: velocity 127 for
// accented steps and 90 for the rest (the voice takes 100+ as an accent), and a slide as two
// overlapping notes. 16th-note steps, 480 ticks per quarter note.
#pragma once

#include "Patterns.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tross {

struct MidiNote {
	int tick, length, pitch, velocity;
};

constexpr int kPpq = 480, kStepTicks = kPpq / 4, kGateTicks = kStepTicks / 2, kSlideOverlap = 10;
constexpr int kBaseNote = 36; // the sequencer's C, as in Synth (C2 in Live's naming)

// The notes the sequencer plays for `patterns` in order (each for its own length), with
// `globalTranspose` added to every pattern's own transpose. Same rules as Synth::seqStep:
// ties hold the note, a note's SLIDE applies after its last tied step, a tie after a rest
// plays nothing.
inline std::vector<MidiNote> patternNotes(const PatternBank &bank, const std::vector<int> &patterns, int globalTranspose,
                                          int *totalTicks = nullptr)
{
	struct Flat {
		Step s;
		int pitch;
	};
	std::vector<Flat> flat;
	for (int p : patterns) {
		const Pattern pat = bank.get(p);
		for (int i = 0; i < pat.length; ++i) {
			const Step &s = pat.steps[size_t(i)];
			int pitch = kBaseNote + globalTranspose + pat.transpose + s.semis();
			pitch = pitch < 0 ? 0 : pitch > 127 ? 127 : pitch;
			flat.push_back({s, pitch});
		}
	}
	if (totalTicks)
		*totalTicks = int(flat.size()) * kStepTicks;

	std::vector<MidiNote> notes;
	const int n = int(flat.size());
	int i = 0;
	while (i < n) {
		if (flat[size_t(i)].s.time != Time::Note) {
			++i; // rests, and ties that follow a rest
			continue;
		}
		const Step &start = flat[size_t(i)].s;
		int last = i; // the last step this note holds through its ties
		while (last + 1 < n && flat[size_t(last + 1)].s.time == Time::Tie)
			++last;
		int end;
		if (start.slide && last + 1 < n && flat[size_t(last + 1)].s.time == Time::Note)
			end = (last + 1) * kStepTicks + kSlideOverlap; // legato into the next note: a slide
		else if (start.slide)
			end = (last + 1) * kStepTicks; // slide into a rest: held for the whole step
		else
			end = last * kStepTicks + kGateTicks;
		notes.push_back({i * kStepTicks, end - i * kStepTicks, flat[size_t(i)].pitch, start.accent ? 127 : 90});
		i = last + 1;
	}
	return notes;
}

namespace midifile {

inline void be16(std::vector<uint8_t> &v, int x)
{
	v.push_back(uint8_t((x >> 8) & 255));
	v.push_back(uint8_t(x & 255));
}
inline void be32(std::vector<uint8_t> &v, uint32_t x)
{
	for (int s = 24; s >= 0; s -= 8)
		v.push_back(uint8_t((x >> s) & 255));
}
inline void vlq(std::vector<uint8_t> &v, uint32_t x)
{
	uint8_t buf[5];
	int k = 0;
	buf[k++] = uint8_t(x & 127);
	while (x >>= 7)
		buf[k++] = uint8_t((x & 127) | 128);
	while (k)
		v.push_back(buf[--k]);
}

} // namespace midifile

// A type-0 Standard MIDI File: track name, the tempo (if > 0), the notes on channel 1, and an
// end-of-track at the loop length so the clip loops cleanly when dragged into a DAW.
inline std::vector<uint8_t> midiFile(const std::vector<MidiNote> &notes, int totalTicks, const std::string &name,
                                     double bpm)
{
	using namespace midifile;
	struct Ev {
		int tick, order;
		std::vector<uint8_t> bytes;
	};
	std::vector<Ev> evs;
	{
		std::vector<uint8_t> b{0xFF, 0x03};
		vlq(b, uint32_t(name.size()));
		b.insert(b.end(), name.begin(), name.end());
		evs.push_back({0, 0, b});
	}
	if (bpm > 0) {
		const uint32_t us = uint32_t(60000000.0 / bpm + 0.5);
		evs.push_back({0, 0, {0xFF, 0x51, 0x03, uint8_t(us >> 16), uint8_t(us >> 8), uint8_t(us)}});
	}
	evs.push_back({0, 0, {0xFF, 0x58, 0x04, 4, 2, 24, 8}}); // 4/4
	for (const auto &nt : notes) {
		// Note-offs sort before note-ons at the same tick (except a slide's overlap, which ends later).
		evs.push_back({nt.tick + nt.length, 1, {0x80, uint8_t(nt.pitch), 0}});
		evs.push_back({nt.tick, 2, {0x90, uint8_t(nt.pitch), uint8_t(nt.velocity)}});
	}
	std::stable_sort(evs.begin(), evs.end(), [](const Ev &a, const Ev &b) {
		return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
	});

	std::vector<uint8_t> trk;
	int now = 0;
	for (const auto &e : evs) {
		vlq(trk, uint32_t(e.tick - now));
		now = e.tick;
		trk.insert(trk.end(), e.bytes.begin(), e.bytes.end());
	}
	const int endTick = totalTicks > now ? totalTicks : now;
	vlq(trk, uint32_t(endTick - now));
	trk.insert(trk.end(), {0xFF, 0x2F, 0x00});

	std::vector<uint8_t> out{'M', 'T', 'h', 'd'};
	be32(out, 6);
	be16(out, 0);    // format 0
	be16(out, 1);    // one track
	be16(out, kPpq); // ticks per quarter note
	out.insert(out.end(), {'M', 'T', 'r', 'k'});
	be32(out, uint32_t(trk.size()));
	out.insert(out.end(), trk.begin(), trk.end());
	return out;
}

} // namespace tross
