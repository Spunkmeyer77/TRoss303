// TRoss 303 — pattern memory and random pattern creator.
// Copyright (C) 2026 Ticco Ross. AGPL-3.0-or-later (see LICENSE).
//
// 64 patterns like the TB-303: 4 groups (I-IV) x 8 patterns x 2 sections (A/B).
// Index = group * 16 + pattern * 2 + section.
//
// Every value is an atomic so the audio thread (which plays the patterns) and the
// message thread (which edits them) can share one bank without locks.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

namespace tross {

constexpr int kSteps = 16;
constexpr int kGroups = 4, kPatternsPerGroup = 8, kSections = 2;
constexpr int kPatterns = kGroups * kPatternsPerGroup * kSections; // 64
constexpr int kChainMax = 16;

inline int patternIndex(int group, int pattern, int section) { return group * 16 + pattern * 2 + section; }

enum class Time : uint8_t { Rest = 0, Note = 1, Tie = 2 };

// One step. note 0-12 = C..C' (the 303's 13 keys), oct 0 = DOWN, 1 = none, 2 = UP.
struct Step {
	uint8_t note = 0;
	uint8_t oct = 1;
	bool accent = false;
	bool slide = false;
	Time time = Time::Rest;

	uint16_t pack() const
	{
		return uint16_t((note & 15) | ((oct & 3) << 4) | (accent ? 64 : 0) | (slide ? 128 : 0) |
		                ((uint16_t(time) & 3) << 8));
	}
	static Step unpack(uint16_t v)
	{
		Step s;
		s.note = uint8_t(v & 15);
		if (s.note > 12)
			s.note = 12;
		s.oct = uint8_t((v >> 4) & 3);
		if (s.oct > 2)
			s.oct = 2;
		s.accent = (v & 64) != 0;
		s.slide = (v & 128) != 0;
		const int t = (v >> 8) & 3;
		s.time = t == 1 ? Time::Note : t == 2 ? Time::Tie : Time::Rest;
		return s;
	}
	// Semitones from the pattern's C.
	int semis() const { return int(note) + (int(oct) - 1) * 12; }
};

constexpr int kTransposeRange = 12; // per-pattern transpose, semitones either way

struct Pattern {
	std::array<Step, kSteps> steps{};
	int length = kSteps;
	int transpose = 0;
};

enum Lock : unsigned { LockNotes = 1, LockOctaves = 2, LockAccents = 4, LockSlides = 8, LockTime = 16 };

struct RandomSettings {
	int scale = 2;        // index into scales()
	int root = 0;         // 0 = C
	double density = 0.7; // share of steps that play
	double accent = 0.3;
	double slide = 0.25;
	double spread = 0.3;  // octave UP/DOWN amount
	unsigned locks = 0;   // Lock bits: lanes kept when re-rolling
};

struct Scale {
	const char *name;
	std::vector<int> degrees;
};

inline const std::vector<Scale> &scales()
{
	static const std::vector<Scale> s = {
		{"Chromatic", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
		{"Major", {0, 2, 4, 5, 7, 9, 11}},
		{"Minor", {0, 2, 3, 5, 7, 8, 10}},
		{"Dorian", {0, 2, 3, 5, 7, 9, 10}},
		{"Phrygian", {0, 1, 3, 5, 7, 8, 10}},
		{"Mixolydian", {0, 2, 4, 5, 7, 9, 10}},
		{"Harm Minor", {0, 2, 3, 5, 7, 8, 11}},
		{"Min Penta", {0, 3, 5, 7, 10}},
		{"Blues", {0, 3, 5, 6, 7, 10}},
		{"Acid", {0, 0, 0, 3, 7, 10, 12}}, // root-heavy minor seventh: the classic acid line
	};
	return s;
}

class PatternBank {
public:
	PatternBank() { clearAll(); }

	void clearAll()
	{
		for (int p = 0; p < kPatterns; ++p)
			clear(p);
		m_chainLen.store(0);
		m_chainOn.store(false);
		m_selected.store(0);
	}

	Step step(int p, int i) const { return Step::unpack(m_steps[idx(p, i)].load(std::memory_order_relaxed)); }
	void setStep(int p, int i, const Step &s) { m_steps[idx(p, i)].store(s.pack(), std::memory_order_relaxed); }
	int length(int p) const { return m_length[clampP(p)].load(std::memory_order_relaxed); }
	int transpose(int p) const { return m_transpose[clampP(p)].load(std::memory_order_relaxed); }
	void setTranspose(int p, int t)
	{
		m_transpose[clampP(p)].store(t < -kTransposeRange ? -kTransposeRange : t > kTransposeRange ? kTransposeRange : t);
	}
	void setLength(int p, int n) { m_length[clampP(p)].store(n < 1 ? 1 : n > kSteps ? kSteps : n); }

	Pattern get(int p) const
	{
		Pattern out;
		for (int i = 0; i < kSteps; ++i)
			out.steps[size_t(i)] = step(p, i);
		out.length = length(p);
		out.transpose = transpose(p);
		return out;
	}
	void put(int p, const Pattern &pat)
	{
		for (int i = 0; i < kSteps; ++i)
			setStep(p, i, pat.steps[size_t(i)]);
		setLength(p, pat.length);
		setTranspose(p, pat.transpose);
	}
	void clear(int p) { put(p, Pattern{}); }

	int selected() const { return m_selected.load(); }
	void select(int p) { m_selected.store(clampP(p)); }

	bool chainOn() const { return m_chainOn.load(); }
	void setChainOn(bool on) { m_chainOn.store(on); }
	int chainLength() const { return m_chainLen.load(); }
	int chainAt(int i) const { return m_chain[size_t(i < 0 ? 0 : i >= kChainMax ? kChainMax - 1 : i)].load(); }
	void chainClear() { m_chainLen.store(0); }
	bool chainAdd(int p)
	{
		const int n = m_chainLen.load();
		if (n >= kChainMax)
			return false;
		m_chain[size_t(n)].store(clampP(p));
		m_chainLen.store(n + 1);
		return true;
	}

	// Serialises the whole bank for the Live set: version, 64 x (length, transpose, 16 steps),
	// chain on, chain length, 16 chain slots, selected pattern. Version 1 (TRoss 303 1.0-1.1)
	// had no transpose and still loads.
	std::vector<int> save() const
	{
		std::vector<int> v;
		v.reserve(1 + kPatterns * (2 + kSteps) + 2 + kChainMax + 1);
		v.push_back(kVersion);
		for (int p = 0; p < kPatterns; ++p) {
			v.push_back(length(p));
			v.push_back(transpose(p));
			for (int i = 0; i < kSteps; ++i)
				v.push_back(m_steps[idx(p, i)].load());
		}
		v.push_back(chainOn() ? 1 : 0);
		v.push_back(chainLength());
		for (int i = 0; i < kChainMax; ++i)
			v.push_back(m_chain[size_t(i)].load());
		v.push_back(selected());
		return v;
	}
	bool load(const std::vector<int> &v)
	{
		if (v.empty() || (v[0] != 1 && v[0] != kVersion))
			return false;
		const int perPattern = v[0] == 1 ? 1 + kSteps : 2 + kSteps;
		const size_t need = 1 + size_t(kPatterns * perPattern) + 2 + kChainMax + 1;
		if (v.size() < need)
			return false;
		size_t k = 1;
		for (int p = 0; p < kPatterns; ++p) {
			setLength(p, v[k++]);
			setTranspose(p, v[0] == 1 ? 0 : v[k++]);
			for (int i = 0; i < kSteps; ++i)
				setStep(p, i, Step::unpack(uint16_t(v[k++])));
		}
		setChainOn(v[k++] != 0);
		const int n = v[k++];
		for (int i = 0; i < kChainMax; ++i)
			m_chain[size_t(i)].store(clampP(v[k++]));
		m_chainLen.store(n < 0 ? 0 : n > kChainMax ? kChainMax : n);
		select(v[k++]);
		return true;
	}

private:
	static constexpr int kVersion = 2;
	static int clampP(int p) { return p < 0 ? 0 : p >= kPatterns ? kPatterns - 1 : p; }
	static size_t idx(int p, int i)
	{
		return size_t(clampP(p) * kSteps + (i < 0 ? 0 : i >= kSteps ? kSteps - 1 : i));
	}

	std::array<std::atomic<uint16_t>, kPatterns * kSteps> m_steps{};
	std::array<std::atomic<int>, kPatterns> m_length{};
	std::array<std::atomic<int>, kPatterns> m_transpose{};
	std::array<std::atomic<int>, kChainMax> m_chain{};
	std::atomic<int> m_chainLen{0};
	std::atomic<bool> m_chainOn{false};
	std::atomic<int> m_selected{0};
};

// Writes a random acid line over `pat`, keeping the locked lanes.
inline void randomize(Pattern &pat, const RandomSettings &rs, std::mt19937 &rng)
{
	std::uniform_real_distribution<double> u(0.0, 1.0);
	const auto &sc = scales()[size_t(rs.scale < 0 ? 0 : rs.scale >= int(scales().size()) ? 0 : rs.scale)];
	const int root = ((rs.root % 12) + 12) % 12;

	bool prevPlays = false;
	for (int i = 0; i < kSteps; ++i) {
		Step &s = pat.steps[size_t(i)];
		if (!(rs.locks & LockTime)) {
			// The downbeat plays more often than the rest, so lines keep a pulse.
			const double d = (i % 4 == 0) ? std::min(1.0, rs.density * 1.3 + 0.1) : rs.density;
			if (u(rng) < d)
				s.time = (prevPlays && u(rng) < 0.12) ? Time::Tie : Time::Note;
			else
				s.time = Time::Rest;
		}
		if (!(rs.locks & LockNotes)) {
			// Root weighted: half the notes land on it, the rest anywhere in the scale.
			int deg = u(rng) < 0.4 ? 0 : sc.degrees[size_t(std::uniform_int_distribution<int>(0, int(sc.degrees.size()) - 1)(rng))];
			int n = root + deg; // 0..23
			int oct = 1;
			while (n > 12) {
				n -= 12;
				++oct;
			}
			s.note = uint8_t(n);
			if (!(rs.locks & LockOctaves))
				s.oct = uint8_t(oct > 2 ? 2 : oct);
		}
		if (!(rs.locks & LockOctaves)) {
			const double r = u(rng);
			if (r < rs.spread * 0.5)
				s.oct = 2;
			else if (r < rs.spread)
				s.oct = 0;
			else if (rs.locks & LockNotes)
				s.oct = 1;
			else if (s.oct > 1 && u(rng) < 0.5)
				s.oct = 1;
		}
		if (!(rs.locks & LockAccents))
			s.accent = u(rng) < rs.accent;
		if (!(rs.locks & LockSlides))
			s.slide = u(rng) < rs.slide;
		prevPlays = s.time != Time::Rest;
	}
}


// A small variation of `pat` (SHIFT + RANDOM): 2 to 4 steps change one thing each (a note
// moves within the scale, an octave flips, an accent or slide toggles, a step plays or
// rests), keeping the locked lanes. The line stays recognisably the same.
inline void vary(Pattern &pat, const RandomSettings &rs, std::mt19937 &rng)
{
	std::uniform_real_distribution<double> u(0.0, 1.0);
	const auto &sc = scales()[size_t(rs.scale < 0 ? 0 : rs.scale >= int(scales().size()) ? 0 : rs.scale)];
	const int root = ((rs.root % 12) + 12) % 12;
	const int len = pat.length < 1 ? 1 : pat.length > kSteps ? kSteps : pat.length;

	std::vector<int> kinds;
	if (!(rs.locks & LockNotes))
		kinds.insert(kinds.end(), {0, 0, 0}); // note changes are the most musical: weight them
	if (!(rs.locks & LockOctaves))
		kinds.push_back(1);
	if (!(rs.locks & LockAccents))
		kinds.insert(kinds.end(), {2, 2});
	if (!(rs.locks & LockSlides))
		kinds.push_back(3);
	if (!(rs.locks & LockTime))
		kinds.push_back(4);
	if (kinds.empty())
		return;

	const int changes = std::uniform_int_distribution<int>(2, 4)(rng);
	for (int c = 0; c < changes; ++c) {
		const int kind = kinds[size_t(std::uniform_int_distribution<int>(0, int(kinds.size()) - 1)(rng))];
		// Prefer steps that play (so the change is heard), except for rhythm changes.
		int i = std::uniform_int_distribution<int>(0, len - 1)(rng);
		for (int tries = 0; tries < 8 && kind != 4 && pat.steps[size_t(i)].time == Time::Rest; ++tries)
			i = std::uniform_int_distribution<int>(0, len - 1)(rng);
		Step &s = pat.steps[size_t(i)];
		switch (kind) {
		case 0: { // another note of the scale, nearby
			int best = s.note, bestD = 99;
			for (int tries = 0; tries < 12; ++tries) {
				const int deg = sc.degrees[size_t(std::uniform_int_distribution<int>(0, int(sc.degrees.size()) - 1)(rng))];
				int n = (root + deg) % 12;
				if (s.note == 12 && n == 0)
					n = 12;
				const int d = std::abs(n - int(s.note));
				if (n != int(s.note) && d < bestD) {
					best = n;
					bestD = d;
				}
				if (bestD <= 5 && u(rng) < 0.5)
					break;
			}
			s.note = uint8_t(best);
			break;
		}
		case 1:
			s.oct = s.oct == 1 ? (u(rng) < 0.5 ? 2 : 0) : 1;
			break;
		case 2:
			s.accent = !s.accent;
			break;
		case 3:
			s.slide = !s.slide;
			break;
		case 4:
			s.time = s.time == Time::Rest ? Time::Note : (u(rng) < 0.7 ? Time::Rest : Time::Tie);
			break;
		}
	}
}

} // namespace tross
