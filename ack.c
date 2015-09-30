/* ack - a synthesizer made in austria (jn, 2013) */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>

void *xmalloc(size_t sz)
{
	void *mem = malloc(sz);

	if(!mem) {
		const char str[] = "out of memory\n";
		fwrite(str, sizeof(str), 1, stderr);
		exit(EXIT_FAILURE);
	}

	return mem;
}

struct {
	unsigned int sample_rate;
} global;

/* an oscillator */
struct osc {
	enum {
		WF_PWM,
		WF_SAW,
		WF_NOISE,
	} waveform;
	float phase; /* 0 <= p < 1 */
	float phase_per_sample;
	float parameter; /* 0 <= p <= 1 */
	float parameter_per_sample; /* kind of an LFO */
	/* detuning... */
};

void osc_set_note(struct osc *osc, int note);

void osc_init(struct osc *osc)
{
	/* some slightly less insane defaults than random data */
	osc->waveform = WF_PWM;
	osc->phase = 0.;
	osc_set_note(osc, 72);
	osc->parameter = 0.5;
	osc->parameter_per_sample = 0;
}

float osc_calc_wave(struct osc *osc)
{
	switch (osc->waveform) {
	case WF_PWM:
		return (osc->phase > osc->parameter)? 1:-1;
	case WF_SAW: {
		float phase, value;
		bool inverse;

		inverse = osc->phase > 0.5;
		phase = 2 * (inverse? (1 - osc->phase) : osc->phase);

		if (phase < osc->parameter)
			value = phase / osc->parameter;
		else
			value = (1 - phase) / (1 - osc->parameter);
		
		return inverse? -value : value;
		}
	case WF_NOISE:
		return ((float) rand()) / (RAND_MAX / 2) - 1;
	default:
		assert(0);
	}
}

float osc_next(struct osc *osc)
{
	float wave = osc_calc_wave(osc);

	osc->phase += osc->phase_per_sample;
	while (osc->phase >= 1)
		osc->phase -= 1;

	osc->parameter += osc->parameter_per_sample;
	while (osc->parameter >= 1)
		osc->parameter -= 1;

	return wave;
}

enum note_name_t {
	NOTE_C = 0,
	/* add on demand */
	NOTE_OCTAVE = 12
};

/* frequencies of the first 12 midi notes */
float base_freq[12] = {
	8.17579891564,
	8.66195721803,
	9.17702399742,
	9.72271824132,
	10.3008611535,
	10.9133822323,
	11.5623257097,
	12.2498573744,
	12.9782717994,
	13.75,
	14.5676175474,
	15.4338531643
};

float calc_note_freq(int note)
{
	assert(note >= 0 && note < 128);

	int octave = note / 12;
	int base = note % 12;

	return base_freq[base] * (1 << octave);
}

void osc_set_note(struct osc *osc, int note)
{
	osc->phase_per_sample = calc_note_freq(note) / global.sample_rate;
}

/* an ADSR envelope */
struct adsr {
	/* milliseconds */
	float attack, decay, release;
	float sustain; /* 0 .. 1 */

	enum {
		ADSR_OFF,
		ADSR_ATTACK,
		ADSR_ATTACK_R,
		ADSR_DECAY,
		ADSR_SUSTAIN,
		ADSR_RELEASE
	} state;

	float release_level;

	int samples_in_state;
	int samples_done;
};

void adsr_init(struct adsr *adsr)
{
	adsr->attack = 15;
	adsr->decay = 40;
	adsr->sustain = .7;
	adsr->release = 10;
	adsr->state = ADSR_OFF;
}

float adsr_calc(struct adsr *adsr)
{
	double progress = 1.0 * adsr->samples_done / (adsr->samples_in_state-1);
	switch(adsr->state) {
	case ADSR_OFF:
		return 0;
	case ADSR_ATTACK:
	case ADSR_ATTACK_R:
		return progress;
	case ADSR_DECAY:
		return 1.0 + (adsr->sustain - 1.0) * progress;
	case ADSR_SUSTAIN:
		return adsr->sustain;
	case ADSR_RELEASE:
		return adsr->release_level * (1-progress);
	default:
		assert(0);
	}
}

int ms_to_samples(float ms)
{
	return 0.001 * ms * global.sample_rate;
}

void adsr_trigger(struct adsr *adsr)
{
	/* always to a full retriggering, and ignore all previous state. */
	adsr->state = ADSR_ATTACK;
	adsr->samples_in_state = ms_to_samples(adsr->attack);
	adsr->samples_done = 0;
}

void adsr_do_release(struct adsr *adsr)
{
	adsr->release_level = adsr_calc(adsr);
	adsr->state = ADSR_RELEASE;
	adsr->samples_in_state = ms_to_samples(adsr->release);
	adsr->samples_done = 0;
}

/* release can happen at different stages:
   at sustain time: that's how it should be most of the time.
   at attack time: play the full attack and then a full release.
   at decay time: immediately start the release.
   when release starts, it always continues at the current level. */
void adsr_release(struct adsr *adsr)
{
	switch(adsr->state) {
	case ADSR_ATTACK:
		adsr->state = ADSR_ATTACK_R;
		break;
	case ADSR_DECAY:
	case ADSR_SUSTAIN:
		adsr_do_release(adsr);
	case ADSR_ATTACK_R:
	case ADSR_RELEASE:
	case ADSR_OFF:
		/* do nothing */
		break;
	default:
		assert(0);
	}
}

/* immediately stop the ADSR */
void adsr_stop(struct adsr *adsr)
{
	adsr->state = ADSR_OFF;
}

void adsr_try_change_state(struct adsr *adsr)
{
again:
	if (adsr->state == ADSR_OFF || adsr->state == ADSR_SUSTAIN)
		return;

	if (adsr->samples_done != adsr->samples_in_state)
		return;

	switch (adsr->state) {
	case ADSR_ATTACK:
		adsr->state = ADSR_DECAY;
		adsr->samples_in_state = ms_to_samples(adsr->decay);
		adsr->samples_done = 0;
		break;
	case ADSR_ATTACK_R:
		adsr_do_release(adsr);
		break;
	case ADSR_DECAY:
		adsr->state = ADSR_SUSTAIN;
		break;
	case ADSR_RELEASE:
		adsr->state = ADSR_OFF;
		break;
	default:
		assert(0);
	}
	
	goto again;
}

float adsr_next(struct adsr *adsr)
{
	adsr_try_change_state(adsr);

	float value = adsr_calc(adsr);

	adsr->samples_done++;
	adsr_try_change_state(adsr);

	return value;
}

struct voice {
	struct osc osc;
	struct adsr adsr;
	float velocity;
};

void voice_init(struct voice *v)
{
	osc_init(&v->osc);
	adsr_init(&v->adsr);
	v->velocity = 0.8;
}

void voice_trigger(struct voice *v, int note)
{
	osc_set_note(&v->osc, note);
	adsr_trigger(&v->adsr);
}

void voice_retrigger(struct voice *v)
{
	adsr_trigger(&v->adsr);
}

void voice_release(struct voice *v)
{
	adsr_release(&v->adsr);
}

/* get the next sample. (-1 .. +1) */
float voice_next(struct voice *v)
{
	float wave = osc_next(&v->osc);
	float env = adsr_next(&v->adsr);
	
	return wave * env * v->velocity;
}

/* a file-driven sequencer */
struct seq {
	struct voice *voices;
	int nvoices;
	enum { ALL_VOICES = -1 } current_voice;

	int samples_per_tick;
	int samples_done;

	FILE *file;
	const char *file_name;
	int line, column;
};

void seq_open_file(struct seq *seq, const char *name)
{
	if (!name) {
		/* read from stdin by default */
		seq->file = stdin;
		seq->file_name = "<stdin>";
	} else {
		seq->file = fopen(name, "r");
		if (!seq->file) {
			perror("can't open input file");
			exit(EXIT_FAILURE);
		}
	}
}

void seq_init(struct seq *seq, const char *file_name)
{
	/* allocate one voice by default */
	seq->voices = xmalloc(sizeof(seq->voices[0]));
	voice_init(&seq->voices[0]);
	seq->nvoices = 1;
	seq->current_voice = 0;

	seq->samples_per_tick = 60 * global.sample_rate / 80;
	seq->samples_done = 0; 

	seq->file_name = file_name;
	seq_open_file(seq, file_name);
	seq->line = 1;
	seq->column = 1;
}

/* error reporting function for the parser */
void seq_error(struct seq *seq, const char *fmt, ...)
{
	assert(seq);
	assert(fmt);

	va_list ap;

	/* "file:line:column: error text\n" */
	fprintf(stderr, "%s:%d:%d: ", seq->file_name, seq->line, seq->column);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	
	fputc('\n', stderr);

	exit(EXIT_FAILURE);
}

/* read a character from the input file, adjust position counters */
int seq_getc(struct seq *seq, bool eof_allowed)
{
	assert(seq && seq->file);

	int ch = fgetc(seq->file);
	if (ch == EOF && !eof_allowed)
		seq_error(seq, "Unexpected end of file");

	if (ch == '\n') {
		seq->line++;
		seq->column = 1;
	} else {
		seq->column++;
	}

	return ch;
}

/* read and parse an integer number and return the next character */
int seq_read_int(struct seq *seq, int *number, char first)
{
	int ch;
	int value = first? (first - '0'):0;

	while(isdigit((ch = seq_getc(seq, true))))
		value = 10*value + ch - '0';

	*number = value;

	return ch;
}

/* read a floating point number. + and - are not supported! */
int seq_read_float(struct seq *seq, float *number, char first)
{
	float factor = 1.0;
	float value = 0;
	bool fractional = false;
	bool seen_digit = false;
	char ch = first;

	if (!ch)
		ch = seq_getc(seq, true);

	while(true) {
		if (ch == '.') {
			if (fractional)
				seq_error(seq, "You can't put two points in a number!");	
			else
				fractional = true;
		} else if (isdigit(ch)) {
			if (fractional) {
				factor *= .1;
				value += factor * (ch - '0');
			} else {
				value *= 10;
				value += ch - '0';
			}
			seen_digit = true;
		} else {
			if (seen_digit)
				break;
			else
				seq_error(seq, "numbers need digits!");
		}
		ch = seq_getc(seq, true);
	}

	*number = value;
	return ch;
}

void seq_expect(struct seq *seq, int expected, int got)
{
	if (expected == got)
		return;

	if (got == EOF)
		seq_error(seq, "unexpected end of file ('%c' character was expected)", expected);
	else
		seq_error(seq, "expected '%c', got '%c'.", expected, got);
}

/* either get the current voice, or iterate through all voices */
struct voice *seq_get_voice(struct seq *seq, int *index)
{
	struct voice *voice = NULL;

	if (seq->current_voice == ALL_VOICES) {
		if (*index < seq->nvoices)
			voice = &seq->voices[*index];
	} else {
		if (*index == 0)
			voice = &seq->voices[seq->current_voice];
	}
	++*index;

	return voice;
}

bool seq_parse(struct seq *seq)
{
	while (1) {
		int ch = fgetc(seq->file);
reparse:
		if (ch == EOF)
			break;

		/* for iteration with seq_get_voice */
		int index;
		struct voice *voice;

		switch (ch) {
		case 'C': /* config */
			ch = seq_getc(seq, false);
			if (ch == 'v') {
				/* set the number of voices. all voice state is lost! */
				int n, i;
				ch = seq_read_int(seq, &n, 0);

				free(seq->voices);
				seq->voices = xmalloc(n * sizeof(seq->voices[0]));
				for (i = 0; i < n; i++)
					voice_init(&seq->voices[i]);
				seq->nvoices = n;
				seq->current_voice = 0;

				goto reparse;
			} else if (ch == 't') {
				/* set ticks per minute */
				int t;
				ch = seq_read_int(seq, &t, 0);
				seq->samples_per_tick = 60 * global.sample_rate / t;

				goto reparse;
			}
			break;
		case '/':
			ch = seq_getc(seq, false);
			if (ch == '/') /* next tick */
				return false;
			if (ch == 'q') /* quit processing */
				return true;
			if (ch == 'a') { /* apply changes to all voices */
				seq->current_voice = ALL_VOICES;
				break;
			}

			if (!isdigit(ch))
				seq_error(seq, "unknown command '/%c'", ch);

			int v;
			ch = seq_read_int(seq, &v, ch);
			if (v < seq->nvoices)
				seq->current_voice = v;
			else
				seq_error(seq, "invalid voice #%d, only %d have been allocated",
						voice, seq->nvoices);
			goto reparse;
		case 'A': { /* set ADSR */
			float a, d, s, r;

			ch = seq_read_float(seq, &a, 0);
			seq_expect(seq, ',', ch);
			ch = seq_read_float(seq, &d, 0);
			seq_expect(seq, ',', ch);
			ch = seq_read_float(seq, &s, 0);
			seq_expect(seq, ',', ch);
			ch = seq_read_float(seq, &r, 0);

			for (index = 0; voice = seq_get_voice(seq, &index); ) {
				voice->adsr.attack = a;
				voice->adsr.decay = d;
				voice->adsr.sustain = s;
				voice->adsr.release = r;
			}

			goto reparse;
		} case 'P': { /* set parameter (0 .. 100) */
			float p;

			ch = seq_read_float(seq, &p, 0); /* TODO: Pl */
			index = 0;
			while ((voice = seq_get_voice(seq, &index)))
				voice->osc.parameter = p / 100;
			goto reparse;
		} case 'V': { /* 'velocity' (really just volume of a voice) */
			float v;
			ch = seq_read_float(seq, &v, 0);
			for (index = 0; voice = seq_get_voice(seq, &index); )
				voice->velocity = v / 100;
			goto reparse;
		} case 'N': { /* set note */
			int note;
			ch = seq_read_int(seq, &note, 0);
			for (index = 0; voice = seq_get_voice(seq, &index); )
				osc_set_note(&voice->osc, note);
			goto reparse;
		} case 'W': /* set waveform */
			ch = seq_getc(seq, false);
			for (index = 0; voice = seq_get_voice(seq, &index); ) {
				if (ch == 'p')
					voice->osc.waveform = WF_PWM;
				else if (ch == 's')
					voice->osc.waveform = WF_SAW;
				else if (ch == 'n')
					voice->osc.waveform = WF_NOISE;
				else
					seq_error(seq, "Invalid waveform '%c'", ch);
			}
			break;
		case 'T': /* trigger */
			for (index = 0; voice = seq_get_voice(seq, &index); )
				voice_retrigger(voice);
			break;
		case 'R': /* release */
			for (index = 0; voice = seq_get_voice(seq, &index); )
				voice_release(voice);
			break;
		case '#': /* comment */
			do {
				ch = seq_getc(seq, true);
			} while (ch != '\n' && ch != EOF);
			break;
		default:
			/* ignore space outside commands */
			if (isspace(ch))
				break;
			
			seq_error(seq, "unknown command '%c'", ch);
		}
	}

	return false;
}

float seq_next(struct seq *seq, bool *stop)
{
	if ((seq->samples_done % seq->samples_per_tick) == 0)
		*stop = seq_parse(seq);
	seq->samples_done++;

	float value = 0;
	int i;
	for (i = 0; i < seq->nvoices; i++)
		value += voice_next(&seq->voices[i]);

	return value;
}

FILE *sound_dev;

void open_sound_dev(void)
{
	//sound_dev = fopen("/dev/audio", "w");
	sound_dev = fopen("/dev/dsp", "w");
	if (!sound_dev) {
		perror("can't open /dev/dsp");
		exit(EXIT_FAILURE);
	}

	global.sample_rate = 8000;
}

void close_sound_dev(void)
{
	fclose(sound_dev);
}

/* convert a floating point sample (-1..+1) to a uint8_t sample (0..127) */
uint8_t float_to_uint8(float f)
{
	int i = (f + 1.f) * .5 * 127;

	assert(i > 0);
	assert(i < 128);

	return i;
}

void write_sample(float f)
{
	assert(sound_dev != NULL);

	putc(float_to_uint8(f), sound_dev);
}

void close_seq_file(FILE *file)
{
	if (file != stdin)
		fclose(file);
}

int main(int argc, char **argv)
{
	struct seq seq;
	bool stop = false;
	FILE *seq_file;

	srand(time(NULL)); /* don't do this in crypto */
	open_sound_dev();

	seq_init(&seq, argv[1]);
	
	while (!stop)
		write_sample(seq_next(&seq, &stop));

	close_sound_dev();

	return EXIT_SUCCESS;
}
