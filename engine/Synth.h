// TRoss 303 — the sound engine (Lemus ro-mono-303 voice) with the built-in sequencer.
// Copyright (C) 2026 Ticco Ross. AGPL-3.0-or-later (see LICENSE).
//
// The voice is Lemus's TB-303 main-board model (third_party/lemus, machines/tb303/core),
// used unchanged. This file plays it from MIDI notes and from the 64-pattern sequencer.
#pragma once

#include "Patterns.h"

#include "voice.h"
#include "midi.h"

#include <atomic>
#include <random>
#include <vector>

namespace tross {

// Knobs as the panel shows them, 0-10, plus the switches. Written from the message thread,
// read by the audio thread at the start of each block.
struct Params {
	std::atomic<double> tuning{5}, cutoff{5}, resonance{5}, envMod{5}, decay{5}, accent{5}, volume{7};
	std::atomic<bool> square{false};
	std::atomic<double> cutMaxHz{2300}; // how high full CUT OFF reaches (1200-3000 Hz), as the plugin's CUT OFF MAX
	std::atomic<int> transpose{0};      // semitones added to the sequencer's notes
	std::atomic<bool> seqOn{true};      // the sequencer plays while Live's transport runs
	std::atomic<bool> transport{true};  // Live's play state (live.observer is_playing); the
	                                    // beat phase stopping also counts as stopped
};

class Synth {
public:
	Synth();

	void prepare(double sampleRate);

	// MIDI from Live (velocity 100+ = accent, overlapping notes slide, as on the TB-03/TD-3).
	// Message thread; queued and applied at the next sample block.
	void midiNote(int note, int velocity);
	void midiAllOff();

	// Renders n samples. beatPhase is Live's beat phase (plugphasor~: 0-1 per quarter note);
	// nullptr means no host sync, so the sequencer stays stopped.
	void process(const double *beatPhase, float *out, int n);

	Params params;
	PatternBank bank;

	// For the display: the step playing (-1 = stopped) and the pattern playing.
	int playingStep() const { return m_dispStep.load(std::memory_order_relaxed); }
	int playingPattern() const { return m_dispPattern.load(std::memory_order_relaxed); }

	// Pattern editing helpers for the message thread (each keeps one undo level per pattern).
	void randomizeSelected(const RandomSettings &rs);
	void varySelected(const RandomSettings &rs); // SHIFT + RANDOM: a small variation
	// Per-pattern transpose of the selected pattern, dragged on the LCD. endTransposeDrag()
	// closes the undo step.
	void setSelectedTranspose(int semitones);
	void endTransposeDrag() { m_transposeDrag = false; }
	// What MIDI export writes: the chain when it is on and not empty, else the selected pattern.
	std::vector<int> exportList() const;
	void clearSelected();
	void copySelected();
	void pasteSelected();
	bool undoSelected();
	void rememberForUndo(int p);

private:
	struct MidiEvent {
		int note, velocity; // velocity < 0 = all notes off
	};

	void applyParams();
	void startSequencer(double phase);
	void stopSequencer();
	void seqStep();          // a new 16th starts
	void seqHalf();          // halfway through the step: the gate falls unless held
	int nextPattern(int current, bool advanceChain);

	tb303::Voice m_voice;
	tb303::MidiDriver m_midi;
	double m_rate = 44100;
	bool m_prepared = false;

	// MIDI handoff, a small lock-free single-producer/single-consumer ring.
	static constexpr int kQueue = 256;
	std::array<MidiEvent, kQueue> m_queue{};
	std::atomic<int> m_qWrite{0}, m_qRead{0};

	// Parameter cache (audio thread).
	tb303::Controls m_lastControls;
	double m_lastCutMax = -1;
	std::vector<std::pair<double, double>> m_topLaw;

	// Sequencer (audio thread).
	tb303::CpuLines m_seqLines;
	unsigned m_seqTriggers = 0;
	bool m_running = false;
	double m_lastPhase = -1;
	long m_still = 0;
	long m_beat = 0;
	long m_lastPos = -1, m_startPos = 0;
	bool m_halfDone = true;
	int m_play = 0, m_stepIdx = -1, m_chainPos = 0;
	bool m_holdThrough = false, m_prevHeld = false;
	bool m_noteSlide = false;
	bool m_wasSeqOn = true;

	bool m_midiActive = false;
	bool m_gate = false;

	// Sleep once quiet so an idle device costs nothing.
	long m_quiet = 0;
	bool m_asleep = false;

	std::atomic<int> m_dispStep{-1}, m_dispPattern{0};

	// Message thread.
	std::mt19937 m_rng{std::random_device{}()};
	std::array<Pattern, kPatterns> m_undo{};
	std::array<bool, kPatterns> m_hasUndo{};
	Pattern m_clipboard;
	bool m_transposeDrag = false;
	bool m_hasClipboard = false;
};

} // namespace tross
