// TRoss 303 — the sound engine with the built-in sequencer.
// Copyright (C) 2026 Ticco Ross. AGPL-3.0-or-later (see LICENSE).
#include "Synth.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace tross {

namespace {

// CUT OFF MAX, as in Lemus's plugin (machines/tb303/plugin/PluginProcessor.cpp): the pot law
// (the CUT OFF pot's share of its track at mid rotation) that makes full CUT OFF reach a given
// frequency, interpolated from a grid worked out once with the voice's own fullCutOffHz().
std::vector<std::pair<double, double>> makeTopLaw()
{
	auto c = tb303::unitCalibration();
	std::vector<double> mids{c.taperAMid};
	for (double m = 0.6; m > 0.003; m *= 0.94)
		if (std::abs(m / c.taperAMid - 1) > 0.01)
			mids.push_back(m);
	std::sort(mids.begin(), mids.end(), std::greater<>());
	std::vector<std::pair<double, double>> pts;
	for (double m : mids) {
		c.cutoffTaperMid = m;
		pts.push_back({std::log(tb303::fullCutOffHz(c)), std::log(m)});
	}
	std::sort(pts.begin(), pts.end());
	return pts;
}

double potMidForTop(const std::vector<std::pair<double, double>> &law, double hz)
{
	const double x = std::log(hz);
	if (x <= law.front().first)
		return std::exp(law.front().second);
	for (size_t i = 1; i < law.size(); ++i)
		if (x <= law[i].first) {
			const double t = (x - law[i - 1].first) / (law[i].first - law[i - 1].first);
			return std::exp(law[i - 1].second + t * (law[i].second - law[i - 1].second));
		}
	return std::exp(law.back().second);
}

double knob(const std::atomic<double> &v)
{
	const double k = v.load(std::memory_order_relaxed) / 10.0;
	return k < 0 ? 0 : k > 1 ? 1 : k;
}

} // namespace

Synth::Synth()
{
	m_topLaw = makeTopLaw();
	m_hasUndo.fill(false);
}

void Synth::prepare(double sampleRate)
{
	m_rate = sampleRate > 0 ? sampleRate : 44100;
	m_lastCutMax = -1;
	applyParams();
	m_voice.prepare(m_rate);
	m_prepared = true;
	m_asleep = false;
	m_quiet = 0;
}

void Synth::midiNote(int note, int velocity)
{
	const int w = m_qWrite.load(std::memory_order_relaxed);
	const int next = (w + 1) % kQueue;
	if (next == m_qRead.load(std::memory_order_acquire))
		return; // full: drop
	m_queue[size_t(w)] = {note, velocity};
	m_qWrite.store(next, std::memory_order_release);
}

void Synth::midiAllOff() { midiNote(0, -1); }

void Synth::applyParams()
{
	tb303::Controls c;
	c.tuning = knob(params.tuning);
	c.cutoff = knob(params.cutoff);
	c.resonance = knob(params.resonance);
	c.envMod = knob(params.envMod);
	c.decay = knob(params.decay);
	c.accent = knob(params.accent);
	c.volume = knob(params.volume);
	c.square = params.square.load(std::memory_order_relaxed);
	m_voice.setControls(c);

	double hz = params.cutMaxHz.load(std::memory_order_relaxed);
	hz = hz < 1200 ? 1200 : hz > 3000 ? 3000 : hz;
	if (hz != m_lastCutMax) {
		m_lastCutMax = hz;
		tb303::Calibration cal = tb303::unitCalibration();
		cal.cutoffTaperMid = potMidForTop(m_topLaw, hz);
		m_voice.setCalibration(cal);
	}
}

// ---- Sequencer ------------------------------------------------------------------------

int Synth::nextPattern(int current, bool advanceChain)
{
	if (bank.chainOn() && bank.chainLength() > 0) {
		if (advanceChain)
			m_chainPos = (m_chainPos + 1) % bank.chainLength();
		return bank.chainAt(m_chainPos);
	}
	(void)current;
	return bank.selected(); // a new selection takes over when the pattern ends, as on the 303
}

void Synth::startSequencer(double phase)
{
	m_running = true;
	m_beat = 0;
	m_startPos = long(std::floor(phase * 4.0));
	m_lastPos = -1;
	m_chainPos = 0;
	m_play = (bank.chainOn() && bank.chainLength() > 0) ? bank.chainAt(0) : bank.selected();
	m_stepIdx = -1;
	m_prevHeld = false;
	m_noteSlide = false;
	m_halfDone = true;
}

void Synth::stopSequencer()
{
	m_running = false;
	m_seqLines.gate = false;
	m_seqLines.slide = false;
	m_dispStep.store(-1, std::memory_order_relaxed);
}

void Synth::seqStep()
{
	++m_stepIdx;
	if (m_stepIdx >= bank.length(m_play)) {
		m_stepIdx = 0;
		m_play = nextPattern(m_play, true);
	}
	m_dispStep.store(m_stepIdx, std::memory_order_relaxed);
	m_dispPattern.store(m_play, std::memory_order_relaxed);

	const Step s = bank.step(m_play, m_stepIdx);

	// What follows this step, for ties: the next step, or the first of the next pattern.
	Step next;
	if (m_stepIdx + 1 < bank.length(m_play)) {
		next = bank.step(m_play, m_stepIdx + 1);
	} else {
		const int savedChain = m_chainPos;
		const int np = nextPattern(m_play, true);
		m_chainPos = savedChain;
		next = bank.step(np, 0);
	}

	const int pitch = std::clamp(36 + params.transpose.load(std::memory_order_relaxed) + s.semis(), 12, 96);

	switch (s.time) {
	case Time::Rest:
		m_seqLines.gate = false;
		m_seqLines.slide = false;
		m_noteSlide = false;
		m_holdThrough = false;
		break;
	case Time::Note: {
		const bool slideIn = m_prevHeld && m_noteSlide; // the step before slides into this one
		m_seqLines.note = pitch;
		m_seqLines.accent = s.accent;
		m_seqLines.slide = slideIn;
		if (!slideIn || !m_seqLines.gate) {
			m_seqLines.gate = true;
			++m_seqTriggers; // a new note: the envelopes restart
		}
		m_noteSlide = s.slide;
		m_holdThrough = s.slide || next.time == Time::Tie;
		break;
	}
	case Time::Tie:
		if (!m_prevHeld) { // a tie after a rest plays nothing
			m_seqLines.gate = false;
			m_holdThrough = false;
			break;
		}
		// The note carries on; its own SLIDE applies after its last tied step.
		m_holdThrough = m_noteSlide || next.time == Time::Tie;
		break;
	}
	m_halfDone = false;
}

void Synth::seqHalf()
{
	m_halfDone = true;
	if (!m_holdThrough)
		m_seqLines.gate = false;
	m_prevHeld = m_holdThrough && m_seqLines.gate;
}

// ---- Rendering ------------------------------------------------------------------------

void Synth::process(const double *beatPhase, float *out, int n)
{
	if (!m_prepared)
		prepare(m_rate);
	applyParams();

	const bool seqOn = params.seqOn.load(std::memory_order_relaxed);
	if (!seqOn && m_wasSeqOn && m_running)
		stopSequencer();
	m_wasSeqOn = seqOn;

	int done = 0;
	auto pushLines = [&](int at) {
		// Render up to `at` with the old lines, then switch.
		if (at > done) {
			if (m_asleep)
				std::fill(out + done, out + at, 0.0f);
			else
				m_voice.render(out + done, at - done);
			done = at;
		}
		// MIDI wins while a key is held (and through its release, so the tail is MIDI's),
		// and the sequencer takes back over at its next note.
		const bool midiHeld = m_midi.lines().gate;
		if (midiHeld)
			m_midiActive = true;
		else if (m_seqLines.gate)
			m_midiActive = false;
		const auto &lines = m_midiActive ? m_midi.lines() : m_seqLines;
		m_voice.setLines(lines, m_midiActive ? m_midi.triggers() : m_seqTriggers);
		if (lines.gate && m_asleep) {
			m_asleep = false;
			m_quiet = 0;
		}
		m_gate = lines.gate;
	};

	const long stillLimit = long(0.06 * m_rate);
	for (int i = 0; i < n; ++i) {
		bool changed = false;

		// MIDI first, so notes land on the sample block they arrived for.
		if (i == 0) {
			while (m_qRead.load(std::memory_order_relaxed) != m_qWrite.load(std::memory_order_acquire)) {
				const int r = m_qRead.load(std::memory_order_relaxed);
				const MidiEvent e = m_queue[size_t(r)];
				m_qRead.store((r + 1) % kQueue, std::memory_order_release);
				if (e.velocity < 0)
					m_midi.allNotesOff();
				else if (e.velocity == 0)
					m_midi.noteOff(e.note);
				else
					m_midi.noteOn(e.note, e.velocity);
				changed = true;
			}
		}

		if (beatPhase && seqOn) {
			const double ph = beatPhase[i];
			if (ph != m_lastPhase && m_lastPhase >= 0) {
				m_still = 0;
				if (!m_running) {
					startSequencer(ph);
				} else if (ph < m_lastPhase - 0.5) {
					++m_beat; // the phase wrapped: a new beat
				}
			} else if (m_running && ++m_still > stillLimit) {
				stopSequencer(); // Live's transport stopped
				changed = true;
			}
			m_lastPhase = ph;

			if (m_running) {
				const double q = ph * 4.0;
				const long pos = m_beat * 4 + long(std::floor(q)) - m_startPos;
				if (pos != m_lastPos && pos >= 0) {
					if (!m_halfDone)
						seqHalf();
					m_lastPos = pos;
					seqStep();
					changed = true;
				} else if (!m_halfDone && (q - std::floor(q)) >= 0.5) {
					seqHalf();
					changed = true;
				}
			}
		} else if (m_running) {
			stopSequencer();
			changed = true;
		}

		if (changed)
			pushLines(i);
	}
	if (n > done) {
		if (m_asleep)
			std::fill(out + done, out + n, 0.0f);
		else
			m_voice.render(out + done, n - done);
	}

	// Sleep after 5 s of silence with the gate down (the decay can take 4 s at full DECAY).
	if (!m_gate && !m_asleep) {
		float peak = 0;
		for (int i = 0; i < n; ++i)
			peak = std::max(peak, std::abs(out[i]));
		if (peak < 1e-6f) {
			m_quiet += n;
			if (m_quiet > long(5.0 * m_rate))
				m_asleep = true;
		} else {
			m_quiet = 0;
		}
	} else if (m_gate) {
		m_quiet = 0;
	}
}

// ---- Editing (message thread) --------------------------------------------------------

void Synth::rememberForUndo(int p)
{
	if (p < 0 || p >= kPatterns)
		return;
	m_undo[size_t(p)] = bank.get(p);
	m_hasUndo[size_t(p)] = true;
}

void Synth::randomizeSelected(const RandomSettings &rs)
{
	const int p = bank.selected();
	rememberForUndo(p);
	Pattern pat = bank.get(p);
	randomize(pat, rs, m_rng);
	bank.put(p, pat);
}

void Synth::clearSelected()
{
	const int p = bank.selected();
	rememberForUndo(p);
	bank.clear(p);
}

void Synth::copySelected()
{
	m_clipboard = bank.get(bank.selected());
	m_hasClipboard = true;
}

void Synth::pasteSelected()
{
	if (!m_hasClipboard)
		return;
	const int p = bank.selected();
	rememberForUndo(p);
	bank.put(p, m_clipboard);
}

bool Synth::undoSelected()
{
	const int p = bank.selected();
	if (!m_hasUndo[size_t(p)])
		return false;
	const Pattern now = bank.get(p);
	bank.put(p, m_undo[size_t(p)]);
	m_undo[size_t(p)] = now; // undo again = redo
	return true;
}

} // namespace tross
