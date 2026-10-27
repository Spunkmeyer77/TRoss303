// tross303~ — Max external for TRoss 303: the Lemus TB-303 voice with a 303-style sequencer.
// Copyright (C) 2026 Ticco Ross. AGPL-3.0-or-later (see LICENSE).
//
// Inlet:  signal = Live's beat phase (plugphasor~), plus messages (see README "Messages").
// Outlets: 0 signal  audio
//          1 display messages for the panel (pattern, playstep, playing, chain, select)
//          2 "set bank ..." for the pattr that keeps the patterns in the Live set
#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"

#include "Synth.h"

#include <cstring>
#include <vector>

struct t_tross303 {
	t_pxobject ob;
	tross::Synth *synth;
	tross::RandomSettings *rnd;
	std::vector<float> *buf;
	t_outlet *outDisplay;
	t_outlet *outStore;
	t_clock *pollClock;
	t_clock *storeClock;
	int lastStep, lastPlaying;
};

static t_class *s_class = nullptr;

// ---- Output helpers ---------------------------------------------------------------------

static void outPattern(t_tross303 *x)
{
	const int p = x->synth->bank.selected();
	t_atom av[2 + tross::kSteps];
	atom_setlong(av, p);
	atom_setlong(av + 1, x->synth->bank.length(p));
	for (int i = 0; i < tross::kSteps; ++i)
		atom_setlong(av + 2 + i, x->synth->bank.step(p, i).pack());
	outlet_anything(x->outDisplay, gensym("pattern"), 2 + tross::kSteps, av);
}

static void outChain(t_tross303 *x)
{
	const auto &b = x->synth->bank;
	t_atom av[2 + tross::kChainMax];
	atom_setlong(av, b.chainOn() ? 1 : 0);
	const int n = b.chainLength();
	atom_setlong(av + 1, n);
	for (int i = 0; i < n; ++i)
		atom_setlong(av + 2 + i, b.chainAt(i));
	outlet_anything(x->outDisplay, gensym("chain"), short(2 + n), av);
}

static void outAll(t_tross303 *x)
{
	outChain(x);
	outPattern(x);
	t_atom a;
	atom_setlong(&a, x->synth->playingStep());
	outlet_anything(x->outDisplay, gensym("playstep"), 1, &a);
	atom_setlong(&a, x->synth->playingPattern());
	outlet_anything(x->outDisplay, gensym("playing"), 1, &a);
}

// The patterns changed: show them, and save the bank to the Live set a moment later
// (one save for a burst of edits).
static void edited(t_tross303 *x, bool chainToo = false)
{
	if (chainToo)
		outChain(x);
	outPattern(x);
	clock_fdelay(x->storeClock, 250);
}

static void storeTick(t_tross303 *x)
{
	const std::vector<int> v = x->synth->bank.save();
	std::vector<t_atom> av(v.size() + 1);
	atom_setsym(av.data(), gensym("bank"));
	for (size_t i = 0; i < v.size(); ++i)
		atom_setlong(&av[i + 1], v[i]);
	outlet_anything(x->outStore, gensym("set"), short(av.size()), av.data());
}

static void pollTick(t_tross303 *x)
{
	const int step = x->synth->playingStep(), playing = x->synth->playingPattern();
	t_atom a;
	if (playing != x->lastPlaying) {
		x->lastPlaying = playing;
		atom_setlong(&a, playing);
		outlet_anything(x->outDisplay, gensym("playing"), 1, &a);
	}
	if (step != x->lastStep) {
		x->lastStep = step;
		atom_setlong(&a, step);
		outlet_anything(x->outDisplay, gensym("playstep"), 1, &a);
	}
	clock_fdelay(x->pollClock, 30);
}

// ---- Knobs and switches -----------------------------------------------------------------

#define KNOB(name, field)                                                                      \
	static void m_##name(t_tross303 *x, double v) { x->synth->params.field.store(v); }

KNOB(tuning, tuning)
KNOB(cutoff, cutoff)
KNOB(resonance, resonance)
KNOB(envmod, envMod)
KNOB(decay, decay)
KNOB(accent, accent)
KNOB(volume, volume)
KNOB(cutmax, cutMaxHz)

static void m_wave(t_tross303 *x, long v) { x->synth->params.square.store(v != 0); }
static void m_transpose(t_tross303 *x, long v) { x->synth->params.transpose.store(int(v < -24 ? -24 : v > 24 ? 24 : v)); }
static void m_seq(t_tross303 *x, long v) { x->synth->params.seqOn.store(v != 0); }
static void m_transport(t_tross303 *x, long v) { x->synth->params.transport.store(v != 0); }

// ---- MIDI -------------------------------------------------------------------------------

static void m_note(t_tross303 *x, long note, long vel) { x->synth->midiNote(int(note), int(vel)); }
static void m_flush(t_tross303 *x) { x->synth->midiAllOff(); }

// ---- Pattern memory ---------------------------------------------------------------------

static void m_select(t_tross303 *x, long p)
{
	x->synth->bank.select(int(p));
	edited(x);
}

// setstep <step 0-15> <note|oct|acc|slide|time> <value>
static void m_setstep(t_tross303 *x, t_symbol *, long argc, t_atom *argv)
{
	if (argc < 3)
		return;
	const int i = int(atom_getlong(argv));
	t_symbol *f = atom_getsym(argv + 1);
	const int v = int(atom_getlong(argv + 2));
	if (i < 0 || i >= tross::kSteps)
		return;
	auto &b = x->synth->bank;
	const int p = b.selected();
	x->synth->rememberForUndo(p);
	tross::Step s = b.step(p, i);
	if (f == gensym("note"))
		s.note = uint8_t(v < 0 ? 0 : v > 12 ? 12 : v);
	else if (f == gensym("oct"))
		s.oct = uint8_t(v < 0 ? 0 : v > 2 ? 2 : v);
	else if (f == gensym("acc"))
		s.accent = v != 0;
	else if (f == gensym("slide"))
		s.slide = v != 0;
	else if (f == gensym("time"))
		s.time = v == 1 ? tross::Time::Note : v == 2 ? tross::Time::Tie : tross::Time::Rest;
	else
		return;
	b.setStep(p, i, s);
	edited(x);
}

static void m_length(t_tross303 *x, long n)
{
	const int p = x->synth->bank.selected();
	x->synth->rememberForUndo(p);
	x->synth->bank.setLength(p, int(n));
	edited(x);
}

static void m_random(t_tross303 *x)
{
	x->synth->randomizeSelected(*x->rnd);
	edited(x);
}
static void m_undo(t_tross303 *x)
{
	if (x->synth->undoSelected())
		edited(x);
}
static void m_copy(t_tross303 *x) { x->synth->copySelected(); }
static void m_paste(t_tross303 *x)
{
	x->synth->pasteSelected();
	edited(x);
}
static void m_clear(t_tross303 *x)
{
	x->synth->clearSelected();
	edited(x);
}

static void m_chain(t_tross303 *x, long on)
{
	x->synth->bank.setChainOn(on != 0);
	edited(x, true);
}
static void m_chainadd(t_tross303 *x, long p)
{
	x->synth->bank.chainAdd(int(p));
	edited(x, true);
}
static void m_chainclear(t_tross303 *x)
{
	x->synth->bank.chainClear();
	edited(x, true);
}

// ---- Random creator settings ------------------------------------------------------------

static void m_rscale(t_tross303 *x, long v) { x->rnd->scale = int(v); }
static void m_rroot(t_tross303 *x, long v) { x->rnd->root = int(v); }
static double pct(double v) { return v < 0 ? 0 : v > 100 ? 1 : v / 100.0; }
static void m_rdensity(t_tross303 *x, double v) { x->rnd->density = pct(v); }
static void m_raccent(t_tross303 *x, double v) { x->rnd->accent = pct(v); }
static void m_rslide(t_tross303 *x, double v) { x->rnd->slide = pct(v); }
static void m_rspread(t_tross303 *x, double v) { x->rnd->spread = pct(v); }

// rlock <notes|octaves|accents|slides|time> <0/1>
static void m_rlock(t_tross303 *x, t_symbol *lane, long on)
{
	unsigned bit = 0;
	if (lane == gensym("notes"))
		bit = tross::LockNotes;
	else if (lane == gensym("octaves"))
		bit = tross::LockOctaves;
	else if (lane == gensym("accents"))
		bit = tross::LockAccents;
	else if (lane == gensym("slides"))
		bit = tross::LockSlides;
	else if (lane == gensym("time"))
		bit = tross::LockTime;
	if (on)
		x->rnd->locks |= bit;
	else
		x->rnd->locks &= ~bit;
}

// ---- Save / restore ---------------------------------------------------------------------

// bank <ints...> from the pattr (the Live set).
static void m_bank(t_tross303 *x, t_symbol *, long argc, t_atom *argv)
{
	std::vector<int> v(size_t(argc > 0 ? argc : 0));
	for (long i = 0; i < argc; ++i)
		v[size_t(i)] = int(atom_getlong(argv + i));
	if (x->synth->bank.load(v))
		outAll(x);
	else
		object_warn((t_object *)x, "pattern data not recognised, keeping the current patterns");
}

static void m_refresh(t_tross303 *x) { outAll(x); }

// ---- DSP --------------------------------------------------------------------------------

static void perform64(t_tross303 *x, t_object *, double **ins, long numins, double **outs, long, long n, long, void *)
{
	float *f = x->buf->data();
	if (long(x->buf->size()) < n)
		n = long(x->buf->size()); // never: dsp64 sizes it
	x->synth->process(numins > 0 ? ins[0] : nullptr, f, int(n));
	double *o = outs[0];
	for (long i = 0; i < n; ++i)
		o[i] = double(f[i]);
}

static void dsp64(t_tross303 *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long)
{
	x->buf->assign(size_t(maxvectorsize > 0 ? maxvectorsize : 64), 0.0f);
	x->synth->prepare(samplerate);
	(void)count;
	object_method(dsp64, gensym("dsp_add64"), x, perform64, 0, NULL);
}

// ---- Lifetime ---------------------------------------------------------------------------

static void *tross_new(t_symbol *, long, t_atom *)
{
	t_tross303 *x = (t_tross303 *)object_alloc(s_class);
	if (!x)
		return nullptr;
	dsp_setup((t_pxobject *)x, 1);
	x->outStore = (t_outlet *)outlet_new(x, NULL);
	x->outDisplay = (t_outlet *)outlet_new(x, NULL);
	outlet_new(x, "signal");
	x->synth = new tross::Synth();
	x->rnd = new tross::RandomSettings();
	x->buf = new std::vector<float>(4096, 0.0f);
	x->lastStep = -2;
	x->lastPlaying = -1;
	x->pollClock = clock_new(x, (method)pollTick);
	x->storeClock = clock_new(x, (method)storeTick);
	clock_fdelay(x->pollClock, 100);
	return x;
}

static void tross_free(t_tross303 *x)
{
	dsp_free((t_pxobject *)x);
	clock_unset(x->pollClock);
	clock_unset(x->storeClock);
	object_free(x->pollClock);
	object_free(x->storeClock);
	delete x->synth;
	delete x->rnd;
	delete x->buf;
}

static void tross_assist(t_tross303 *, void *, long m, long a, char *s)
{
	if (m == ASSIST_INLET)
		std::strcpy(s, "(signal) beat phase from plugphasor~, messages: knobs, notes, patterns");
	else if (a == 0)
		std::strcpy(s, "(signal) audio out");
	else if (a == 1)
		std::strcpy(s, "display: pattern, playstep, playing, chain");
	else
		std::strcpy(s, "bank data for pattr (Live set storage)");
}

void ext_main(void *)
{
	t_class *c = class_new("tross303~", (method)tross_new, (method)tross_free, (long)sizeof(t_tross303), 0L, A_GIMME, 0);

	class_addmethod(c, (method)dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)tross_assist, "assist", A_CANT, 0);

	class_addmethod(c, (method)m_tuning, "tuning", A_FLOAT, 0);
	class_addmethod(c, (method)m_cutoff, "cutoff", A_FLOAT, 0);
	class_addmethod(c, (method)m_resonance, "resonance", A_FLOAT, 0);
	class_addmethod(c, (method)m_envmod, "envmod", A_FLOAT, 0);
	class_addmethod(c, (method)m_decay, "decay", A_FLOAT, 0);
	class_addmethod(c, (method)m_accent, "accent", A_FLOAT, 0);
	class_addmethod(c, (method)m_volume, "volume", A_FLOAT, 0);
	class_addmethod(c, (method)m_cutmax, "cutmax", A_FLOAT, 0);
	class_addmethod(c, (method)m_wave, "wave", A_LONG, 0);
	class_addmethod(c, (method)m_transpose, "transpose", A_LONG, 0);
	class_addmethod(c, (method)m_seq, "seq", A_LONG, 0);
	class_addmethod(c, (method)m_transport, "transport", A_LONG, 0);

	class_addmethod(c, (method)m_note, "note", A_LONG, A_LONG, 0);
	class_addmethod(c, (method)m_flush, "flush", 0);

	class_addmethod(c, (method)m_select, "select", A_LONG, 0);
	class_addmethod(c, (method)m_setstep, "setstep", A_GIMME, 0);
	class_addmethod(c, (method)m_length, "length", A_LONG, 0);
	class_addmethod(c, (method)m_random, "random", 0);
	class_addmethod(c, (method)m_undo, "undo", 0);
	class_addmethod(c, (method)m_copy, "copy", 0);
	class_addmethod(c, (method)m_paste, "paste", 0);
	class_addmethod(c, (method)m_clear, "clear", 0);
	class_addmethod(c, (method)m_chain, "chain", A_LONG, 0);
	class_addmethod(c, (method)m_chainadd, "chainadd", A_LONG, 0);
	class_addmethod(c, (method)m_chainclear, "chainclear", 0);

	class_addmethod(c, (method)m_rscale, "rscale", A_LONG, 0);
	class_addmethod(c, (method)m_rroot, "rroot", A_LONG, 0);
	class_addmethod(c, (method)m_rdensity, "rdensity", A_FLOAT, 0);
	class_addmethod(c, (method)m_raccent, "raccent", A_FLOAT, 0);
	class_addmethod(c, (method)m_rslide, "rslide", A_FLOAT, 0);
	class_addmethod(c, (method)m_rspread, "rspread", A_FLOAT, 0);
	class_addmethod(c, (method)m_rlock, "rlock", A_SYM, A_LONG, 0);

	class_addmethod(c, (method)m_bank, "bank", A_GIMME, 0);
	class_addmethod(c, (method)m_refresh, "refresh", 0);

	class_dspinit(c);
	class_register(CLASS_BOX, c);
	s_class = c;
}
