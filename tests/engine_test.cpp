// TRoss 303 engine tests: runs the sequencer against a simulated Live transport and
// renders WAVs to listen to. Build: cmake -B build -DTROSS_TESTS=ON && cmake --build build
#include "MidiExport.h"
#include "Synth.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace tross;

static int failures = 0;
#define CHECK(c)                                                                                \
	do {                                                                                        \
		if (!(c)) {                                                                             \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                            \
			++failures;                                                                         \
		}                                                                                       \
	} while (0)

static void writeWav(const std::string &path, const std::vector<float> &x, int rate)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	const uint32_t bytes = uint32_t(x.size() * 2);
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + bytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(1);
	u16(1);
	u32(uint32_t(rate));
	u32(uint32_t(rate * 2));
	u16(2);
	u16(16);
	std::fwrite("data", 1, 4, f);
	u32(bytes);
	for (float v : x) {
		const float c = v > 1 ? 1 : v < -1 ? -1 : v;
		const int16_t s = int16_t(std::lround(c * 32767));
		std::fwrite(&s, 2, 1, f);
	}
	std::fclose(f);
}

// Runs `seconds` of audio at `bpm`, transport playing from beat 0.
static std::vector<float> run(Synth &s, double seconds, double bpm, int rate, std::vector<int> *steps = nullptr,
                              double stopAt = -1)
{
	const int block = 64;
	const long total = long(seconds * rate);
	std::vector<float> out(size_t(total), 0.0f);
	std::vector<double> phase(block);
	double beat = 0;
	int lastStep = -2;
	for (long at = 0; at < total; at += block) {
		const int n = int(std::min<long>(block, total - at));
		for (int i = 0; i < n; ++i) {
			const double t = double(at + i) / rate;
			if (stopAt < 0 || t < stopAt)
				beat = t * bpm / 60.0;
			phase[size_t(i)] = beat - std::floor(beat);
		}
		s.process(phase.data(), out.data() + at, n);
		if (steps && s.playingStep() != lastStep) {
			lastStep = s.playingStep();
			steps->push_back(lastStep);
		}
	}
	return out;
}

static double rms(const std::vector<float> &x, size_t from, size_t to)
{
	double a = 0;
	for (size_t i = from; i < to && i < x.size(); ++i)
		a += double(x[i]) * x[i];
	return std::sqrt(a / double(std::max<size_t>(1, to - from)));
}

static bool finite(const std::vector<float> &x)
{
	for (float v : x)
		if (!std::isfinite(v))
			return false;
	return true;
}

int main(int argc, char **argv)
{
	const std::string dir = argc > 1 ? argv[1] : ".";
	const int rate = 48000;

	// 1. Step packing round trip.
	for (int note = 0; note <= 12; ++note)
		for (int oct = 0; oct < 3; ++oct)
			for (int t = 0; t < 3; ++t) {
				Step a;
				a.note = uint8_t(note);
				a.oct = uint8_t(oct);
				a.accent = (note & 1) != 0;
				a.slide = (oct & 1) != 0;
				a.time = Time(t);
				const Step b = Step::unpack(a.pack());
				CHECK(b.note == a.note && b.oct == a.oct && b.accent == a.accent && b.slide == a.slide && b.time == a.time);
			}

	// 2. Bank save/load round trip.
	{
		PatternBank a;
		std::mt19937 rng(7);
		for (int p = 0; p < kPatterns; ++p) {
			Pattern pat;
			randomize(pat, RandomSettings{}, rng);
			pat.length = 1 + p % 16;
			a.put(p, pat);
		}
		a.chainAdd(3);
		a.chainAdd(17);
		a.setChainOn(true);
		a.select(42);
		PatternBank b;
		CHECK(b.load(a.save()));
		CHECK(b.save() == a.save());
		CHECK(b.selected() == 42 && b.chainLength() == 2 && b.chainAt(1) == 17 && b.chainOn());
		std::vector<int> bad = a.save();
		bad[0] = 99;
		CHECK(!b.load(bad));
	}

	// 3. Random creator respects locks.
	{
		std::mt19937 rng(1);
		Pattern p;
		randomize(p, RandomSettings{}, rng);
		const Pattern before = p;
		RandomSettings rs;
		rs.locks = LockNotes | LockOctaves | LockTime;
		for (int k = 0; k < 20; ++k)
			randomize(p, rs, rng);
		for (int i = 0; i < kSteps; ++i) {
			CHECK(p.steps[size_t(i)].note == before.steps[size_t(i)].note);
			CHECK(p.steps[size_t(i)].oct == before.steps[size_t(i)].oct);
			CHECK(p.steps[size_t(i)].time == before.steps[size_t(i)].time);
		}
		// Scale: every note of a C minor line is in C minor.
		RandomSettings minor;
		minor.scale = 2;
		minor.root = 0;
		for (int k = 0; k < 50; ++k) {
			randomize(p, minor, rng);
			for (int i = 0; i < kSteps; ++i) {
				const int pc = p.steps[size_t(i)].note % 12;
				const auto &d = scales()[2].degrees;
				CHECK(std::find(d.begin(), d.end(), pc) != d.end());
			}
		}
	}

	// 4. Undo / copy / paste.
	{
		Synth s;
		s.bank.select(5);
		RandomSettings rs;
		s.randomizeSelected(rs);
		const auto a = s.bank.save();
		s.randomizeSelected(rs);
		CHECK(s.undoSelected());
		CHECK(s.bank.save() == a);
		s.copySelected();
		s.bank.select(6);
		s.pasteSelected();
		const Pattern p5 = s.bank.get(5), p6 = s.bank.get(6);
		for (int i = 0; i < kSteps; ++i)
			CHECK(p5.steps[size_t(i)].pack() == p6.steps[size_t(i)].pack());
		s.clearSelected();
		CHECK(s.bank.get(6).steps[0].time == Time::Rest);
		CHECK(s.undoSelected());
		CHECK(s.bank.get(6).steps[0].pack() == p5.steps[0].pack());
	}

	// 5. Sequencer timing: steps advance once per 16th, wrap at the pattern length.
	{
		Synth s;
		s.prepare(rate);
		Pattern p;
		for (int i = 0; i < kSteps; ++i)
			p.steps[size_t(i)].time = Time::Note;
		p.length = 12;
		s.bank.put(0, p);
		std::vector<int> steps;
		run(s, 4.0, 120, rate, &steps); // 2 s = 16 16ths at 120 bpm, so 4 s = 32
		// Drop the initial -1, then expect 0..11,0..11,0..7
		std::vector<int> seq;
		for (int v : steps)
			if (v >= 0)
				seq.push_back(v);
		CHECK(seq.size() >= 31 && seq.size() <= 33);
		for (size_t i = 0; i < seq.size(); ++i)
			CHECK(seq[i] == int(i % 12));
	}

	// 6. Transport stop: the sequencer stops and the voice goes quiet.
	{
		Synth s;
		s.prepare(rate);
		s.params.decay = 0;
		Pattern p;
		for (int i = 0; i < kSteps; ++i)
			p.steps[size_t(i)].time = Time::Note;
		s.bank.put(0, p);
		std::vector<int> steps;
		const auto x = run(s, 3.0, 120, rate, &steps, 1.0);
		CHECK(s.playingStep() == -1);
		CHECK(rms(x, size_t(0.2 * rate), size_t(0.9 * rate)) > 1e-3);
		CHECK(rms(x, size_t(2.5 * rate), size_t(3.0 * rate)) < 1e-3);
	}

	// 7. Renders: an acid line from the random creator, chain A -> B, and MIDI.
	{
		Synth s;
		s.prepare(rate);
		RandomSettings rs;
		rs.scale = 9; // Acid
		s.bank.select(0);
		s.randomizeSelected(rs);
		s.bank.select(1);
		s.randomizeSelected(rs);
		s.bank.chainAdd(0);
		s.bank.chainAdd(0);
		s.bank.chainAdd(1);
		s.bank.setChainOn(true);
		s.params.cutoff = 3;
		s.params.resonance = 8.5;
		s.params.envMod = 7;
		s.params.decay = 4;
		s.params.accent = 8;
		const auto x = run(s, 12.0, 128, rate);
		CHECK(finite(x));
		CHECK(rms(x, 0, x.size()) > 1e-3);
		writeWav(dir + "/acid_chain.wav", x, rate);

		s.params.square = true;
		const auto y = run(s, 6.0, 128, rate);
		CHECK(finite(y));
		writeWav(dir + "/acid_square.wav", y, rate);
	}
	{
		Synth s;
		s.prepare(rate);
		s.params.seqOn = false;
		const int block = 64;
		std::vector<float> out(size_t(rate * 3), 0.0f);
		const int notes[] = {36, 48, 39, 41};
		for (long at = 0; at < long(out.size()); at += block) {
			const int k = int(at / (rate / 4));
			if (at % (rate / 4) == 0 && k < 8) {
				const int nt = notes[k % 4];
				s.midiNote(nt, k % 3 == 0 ? 120 : 80);
				if (k > 0)
					s.midiNote(notes[(k - 1) % 4], 0); // overlapping: slides
			}
			if (k == 8 && at % (rate / 4) == 0)
				s.midiAllOff();
			s.process(nullptr, out.data() + at, block);
		}
		CHECK(finite(out));
		CHECK(rms(out, 0, size_t(rate * 2)) > 1e-3);
		writeWav(dir + "/midi.wav", out, rate);
	}


	// 8. Bank v2 keeps the per-pattern transpose; v1 data (TRoss 303 1.0-1.1) still loads.
	{
		PatternBank a;
		a.setTranspose(3, -7);
		a.setTranspose(40, 12);
		a.setTranspose(41, 99); // clamped
		PatternBank b;
		CHECK(b.load(a.save()));
		CHECK(b.transpose(3) == -7 && b.transpose(40) == 12 && b.transpose(41) == 12);
		// Build v1 data by hand: no transpose field.
		std::vector<int> v1{1};
		for (int p = 0; p < kPatterns; ++p) {
			v1.push_back(p == 2 ? 7 : 16);
			for (int i = 0; i < kSteps; ++i)
				v1.push_back(Step{uint8_t(i % 13), 1, false, false, Time::Note}.pack());
		}
		v1.insert(v1.end(), {0, 0});
		for (int i = 0; i < kChainMax; ++i)
			v1.push_back(0);
		v1.push_back(2);
		PatternBank c;
		c.setTranspose(2, 5);
		CHECK(c.load(v1));
		CHECK(c.length(2) == 7 && c.transpose(2) == 0 && c.selected() == 2 && c.step(2, 4).note == 4);
	}

	// 9. Variation: a few steps change, locked lanes never do.
	{
		std::mt19937 rng(3);
		RandomSettings rs;
		Pattern p;
		randomize(p, rs, rng);
		for (int k = 0; k < 200; ++k) {
			Pattern q = p;
			vary(q, rs, rng);
			int changed = 0;
			for (int i = 0; i < kSteps; ++i)
				changed += q.steps[size_t(i)].pack() != p.steps[size_t(i)].pack();
			CHECK(changed <= 4);
			CHECK(q.transpose == p.transpose && q.length == p.length);
		}
		int anyChange = 0;
		for (int k = 0; k < 50; ++k) {
			Pattern q = p;
			vary(q, rs, rng);
			for (int i = 0; i < kSteps; ++i)
				anyChange += q.steps[size_t(i)].pack() != p.steps[size_t(i)].pack();
		}
		CHECK(anyChange > 50);
		RandomSettings locked = rs;
		locked.locks = LockNotes | LockOctaves | LockTime;
		for (int k = 0; k < 200; ++k) {
			Pattern q = p;
			vary(q, locked, rng);
			for (int i = 0; i < kSteps; ++i) {
				CHECK(q.steps[size_t(i)].note == p.steps[size_t(i)].note);
				CHECK(q.steps[size_t(i)].oct == p.steps[size_t(i)].oct);
				CHECK(q.steps[size_t(i)].time == p.steps[size_t(i)].time);
			}
		}
		locked.locks = LockNotes | LockOctaves | LockTime | LockAccents | LockSlides;
		Pattern q = p;
		vary(q, locked, rng);
		for (int i = 0; i < kSteps; ++i)
			CHECK(q.steps[size_t(i)].pack() == p.steps[size_t(i)].pack());
		// Undo covers it.
		Synth s;
		s.bank.put(0, p);
		const auto before = s.bank.save();
		s.varySelected(rs);
		CHECK(s.undoSelected());
		CHECK(s.bank.save() == before);
	}

	// 10. MIDI export: ties, slides (overlap), accents (velocity), transposes, chain order.
	{
		PatternBank b;
		Pattern p;
		p.length = 8;
		p.transpose = 2;
		auto st = [](int note, int oct, bool acc, bool sl, Time t) { return Step{uint8_t(note), uint8_t(oct), acc, sl, t}; };
		p.steps[0] = st(0, 1, true, false, Time::Note);   // C, accent, gate half a step
		p.steps[1] = st(3, 1, false, false, Time::Note);  // D# held through two ties
		p.steps[2] = st(0, 1, false, false, Time::Tie);
		p.steps[3] = st(0, 1, false, false, Time::Tie);
		p.steps[4] = st(7, 2, false, true, Time::Note);   // G up, slides into the next note
		p.steps[5] = st(10, 0, false, false, Time::Note); // A# down
		p.steps[6] = st(0, 1, false, false, Time::Rest);
		p.steps[7] = st(0, 1, false, false, Time::Tie);   // a tie after a rest: nothing
		b.put(9, p);
		int total = 0;
		const auto n = patternNotes(b, {9}, -1, &total);
		CHECK(total == 8 * kStepTicks);
		CHECK(n.size() == 4);
		if (n.size() == 4) {
			CHECK(n[0].tick == 0 && n[0].length == kGateTicks && n[0].pitch == 36 + 1 && n[0].velocity == 127);
			CHECK(n[1].tick == kStepTicks && n[1].length == 2 * kStepTicks + kGateTicks && n[1].pitch == 36 + 1 + 3 && n[1].velocity == 90);
			CHECK(n[2].tick == 4 * kStepTicks && n[2].length == kStepTicks + kSlideOverlap && n[2].pitch == 36 + 1 + 19);
			CHECK(n[3].tick == 5 * kStepTicks && n[3].pitch == 36 + 1 - 2);
		}
		// Chain of the same pattern twice: notes repeat one pattern length later.
		const auto n2 = patternNotes(b, {9, 9}, 0, &total);
		CHECK(n2.size() == 8 && total == 16 * kStepTicks);
		if (n2.size() == 8)
			CHECK(n2[4].tick == 8 * kStepTicks && n2[4].pitch == n[0].pitch + 1);
		const auto f = midiFile(n, 8 * kStepTicks, "TRoss 303 I-5B", 128);
		CHECK(f.size() > 22 && std::memcmp(f.data(), "MThd", 4) == 0 && std::memcmp(f.data() + 14, "MTrk", 4) == 0);
		const uint32_t trkLen = (uint32_t(f[18]) << 24) | (uint32_t(f[19]) << 16) | (uint32_t(f[20]) << 8) | f[21];
		CHECK(trkLen + 22 == f.size());
		CHECK(f[f.size() - 3] == 0xFF && f[f.size() - 2] == 0x2F && f[f.size() - 1] == 0x00);
		FILE *mf = std::fopen((dir + "/export.mid").c_str(), "wb");
		if (mf) {
			std::fwrite(f.data(), 1, f.size(), mf);
			std::fclose(mf);
		}
	}

	// 11. The per-pattern transpose reaches the voice: same line, +12 sounds an octave up.
	{
		auto pitchOf = [&](int tr) {
			Synth s;
			s.prepare(rate);
			Pattern p;
			for (int i = 0; i < kSteps; ++i)
				p.steps[size_t(i)] = Step{0, 1, false, false, Time::Note};
			p.transpose = tr;
			s.bank.put(0, p);
			s.params.decay = 10;
			s.params.cutoff = 10;
			const auto x = run(s, 0.5, 120, rate);
			// zero crossings in the middle of the first step's note
			int zc = 0;
			for (size_t i = size_t(0.01 * rate); i < size_t(0.06 * rate); ++i)
				zc += (x[i - 1] < 0) != (x[i] < 0);
			return zc;
		};
		const int a = pitchOf(0), b2 = pitchOf(12);
		CHECK(b2 > a * 1.6 && b2 < a * 2.4);
	}

	std::printf(failures ? "%d FAILED\n" : "all tests passed\n", failures);
	return failures ? 1 : 0;
}
