// TRoss 303 engine tests: runs the sequencer against a simulated Live transport and
// renders WAVs to listen to. Build: cmake -B build -DTROSS_TESTS=ON && cmake --build build
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

	std::printf(failures ? "%d FAILED\n" : "all tests passed\n", failures);
	return failures ? 1 : 0;
}
