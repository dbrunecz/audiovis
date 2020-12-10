#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#include "dbx.h"

/******************************************************************************/

#include	<math.h>

#define FLOAT	float
#define SIN	sinf
#define COS	cosf
#define POW	powf

void complex_dft(int cnt, FLOAT *tre, FLOAT *tim, FLOAT *fre, FLOAT *fim)
{
	FLOAT a = 2 * M_PI / (FLOAT) cnt;
	FLOAT re, im;
	int f, t;

	memset(fre, 0, sizeof(*fre) * cnt);
	memset(fim, 0, sizeof(*fim) * cnt);

	for (f = 0; f < cnt; f++) {
		for (t = 0; t < cnt; t++) {
			re = COS(a * f * t);
			im = -1.0 * SIN(a * f * t);
			fre[f] += re * tre[t] - im * tim[t];
			fim[f] += im * tre[t] + re * tim[t];
		}
	}
}

void complex_idft(int cnt, FLOAT *tre, FLOAT *tim, FLOAT *fre, FLOAT *fim)
{
	int i;

	//memset(tre, 0, sizeof(*tre) * cnt);
	//memset(tim, 0, sizeof(*tim) * cnt);

	for (i = 0; i < cnt; i++)
		fim[i] *= -1.0f;

	complex_dft(cnt, fre, fim, tre, tim);

	for (i = 0; i < cnt; i++) {
		tre[i] = tre[i] / (FLOAT)cnt;
		tim[i] = tim[i] / (cnt * -1.0f);
	}
}

/******************************************************************************/

int _random(int min, int max)
{
	static int once;

	if (!once) {
		srand(0);
		once = 1;
	}

	return (int)transform(0, RAND_MAX, rand(), min, max);
}

struct audioparam {
	snd_pcm_t               *sp;
	snd_pcm_hw_params_t     *hwprm;
	snd_pcm_uframes_t       frames;
	u32                     rate;
	u32                     period_us;
	int                     channels;
	char                    *buf;
	u32                     buf_sz;
};

struct audioparam g_in_ap;
struct audioparam g_out_ap;

int audio_write(struct audioparam *ap, u8 *buf, int size)
{
	int sz = ap->frames * 2 * ap->channels;
	int err, i;

	for (i = 0; i < size; i += sz) {
		snd_pcm_wait(ap->sp, -1);
		err = snd_pcm_writei(ap->sp, &buf[i], ap->frames);
		if (err == -EPIPE) {
			/* EPIPE means underrun */
			fprintf(stderr, "underrun occurred %d\n", i);
			snd_pcm_prepare(ap->sp);
		} else if (err < 0) {
			fprintf(stderr, "error from writei: %s\n", snd_strerror(err));
		} else if (err != (int)ap->frames) {
			fprintf(stderr, "short write, write %d frames\n", err);
			return -1;
		}
	}
	return 0;
}

static s16 *audio_read(struct audioparam *ap)
{
	int err;

	err = snd_pcm_readi(ap->sp, ap->buf, ap->frames);
	if (err == (int)ap->frames)
		return (s16 *)ap->buf;

	if (err == -EPIPE) {
		/* EPIPE means overrun */
		fprintf(stderr, "overrun occurred\n");
		snd_pcm_prepare(ap->sp);
	} else if (err < 0) {
		fprintf(stderr, "error from read: %s\n",
			snd_strerror(err));
	} else if (err != (int)ap->frames) {
		fprintf(stderr, "short read, read %d frames\n", err);
	}

	return NULL;
}

static void audio_close(struct audioparam *ap)
{
	snd_pcm_drain(ap->sp);
	snd_pcm_close(ap->sp);
	if (ap->buf)
		munmap(ap->buf, ap->buf_sz);
}

struct audioparam *audio_open(int channels, u32 rate, u32 frames, int capture)
{
	struct audioparam *ap = capture ? &g_in_ap : &g_out_ap;
	int err, dir;

	err = snd_pcm_open(&ap->sp, "default", capture ? SND_PCM_STREAM_CAPTURE
						       : SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		fprintf(stderr, "unable to open pcm device: %s\n",
			snd_strerror(err));
		return NULL;
	}

	snd_pcm_hw_params_alloca(&ap->hwprm);
	snd_pcm_hw_params_any(ap->sp, ap->hwprm);
	snd_pcm_hw_params_set_access(ap->sp, ap->hwprm,
				     SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(ap->sp, ap->hwprm, SND_PCM_FORMAT_S16_LE);
	snd_pcm_hw_params_set_channels(ap->sp, ap->hwprm, channels);
	ap->rate = rate;
	snd_pcm_hw_params_set_rate_near(ap->sp, ap->hwprm, &ap->rate, &dir);
	ap->frames = frames;
	snd_pcm_hw_params_set_period_size_near(ap->sp, ap->hwprm, &ap->frames,
					       &dir);

	err = snd_pcm_hw_params(ap->sp, ap->hwprm);
	if (err < 0) {
		fprintf(stderr, "unable to set hw parameters: %s\n",
			snd_strerror(err));
		return NULL;
	}

	ap->channels = channels;
	snd_pcm_hw_params_get_period_size(ap->hwprm, &ap->frames, &dir);
	snd_pcm_hw_params_get_period_time(ap->hwprm, &ap->period_us, &dir);

	if (capture) {
		/* 2bytes/sample * channels */
		ap->buf_sz = ap->frames * 2 * ap->channels;
		ap->buf = mmap(NULL, ap->buf_sz, PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (!ap->buf)
			return NULL;
	}

	printf("rate:%d channels:%d frames:%d period:%dus buf_sz:%d\n",
	       rate, ap->channels, (int)ap->frames, ap->period_us, ap->buf_sz);
	return ap;
}
/******************************************************************************/

#define GREEN1	RGB(0x10, 0xa0, 0x10)
#define RED1	RGB(0xa0, 0x10, 0x10)
#define BLUE1	RGB(0x30, 0x50, 0xff)

struct clr_ctrl {
	int up;
	int dn;
} r, g, b;

int color_delta(struct clr_ctrl *c)
{
	if ( c->up &&  c->dn) return 0;
	if (!c->up && !c->dn) return 0;

	return c->up ? 4 : -4;
}

int fg_n_bg = 1;
u32 fg_color = GREEN1;
u32 bg_color = 0;
int rainbow_static = 1;

void update_color_component(u32 *c, int offset, int delta)
{
	int v = (*c >> offset) & 0xff;
	*c &= ~(0xff << offset);
	int_mod(&v, 0, 0xff, delta);
	*c |= v << offset;
}

void update_color(void)
{
	u32 *color = fg_n_bg ? &fg_color : &bg_color;
	int d;

	if ((d = color_delta(&r)))
		update_color_component(color, 16, d);
	if ((d = color_delta(&g)))
		update_color_component(color, 8, d);
	if ((d = color_delta(&b)))
		update_color_component(color, 0, d);
}

void clr_ctrl_upd(char k, struct clr_ctrl *c, int press)
{
	if (!press) {
		c->up = c->dn = 0;
		return;
	}
	if (isupper(k))
		c->up = 1;
	else
		c->dn = 1;
}

static s16 *tone = NULL;
int t_sz;

volatile int do_tone;

void tone_populate(int hz, int frames)
{
	struct audioparam *ap = &g_out_ap;
	static float p = 0.000022f;
	float f;
	int i;

	for (i = 0; i < ap->frames * frames; i++) {
		//f = sinf(2.0f * 3.14159f * 1000 * p);
		//tone[2 * i] = (int)transform(-1.0f, 1.0f, f, -22000, 22000);
		//f = sinf(2.0f * 3.14159f * 200 * p);
		//tone[2 * i + 1] = (int)transform(-1.0f, 1.0f, f, -32000, 32000);
		f = sinf(2.0f * 3.14159f * hz * p);
		tone[2 * i] = (int)transform(-1.0f, 1.0f, f, -32000, 32000);
		tone[2 * i + 1] = tone[2 * i];
		p += 0.000022f;
	}
}

void send_tone(int hz, int frames)
{
	struct audioparam *ap = &g_out_ap;

	tone_populate(hz, frames);
	audio_write(ap, (u8 *)tone, ap->frames * sizeof(s16) * 2 * frames);
}

void *thread_routine(void *param)
{
	struct audioparam *ap = &g_out_ap;

	for ( ;; ) {
		snd_pcm_prepare(ap->sp);
		for (; do_tone;) {
			send_tone(440, 10);
			/*
			tone_populate();
			audio_write(ap, (u8 *)tone,
				//t_sz);
				ap->frames * sizeof(s16) * 2 * 10);
			*/
			usleep(1000 * 260);
		}
		snd_pcm_drop(ap->sp);

		for (; !do_tone;)
			usleep(1000 * 50);
	}
	return NULL;
}

void tone_out(int hz)
{
	struct audioparam *ap = &g_out_ap;
	//pthread_t tid;

	if (!tone) {
		t_sz = ap->frames * sizeof(s16) * 2 * 30;
		tone = mmap(NULL, t_sz, PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		t_sz = ap->frames * sizeof(s16) * 2 * 30;
		if (!tone) {
			printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
			exit(0);
		}
		//  //tone_populate();
		//pthread_create(&tid, NULL, thread_routine, NULL);
	}

	send_tone(hz, 10);
	//audio_write(ap, (u8 *)tone, t_sz);//sizeof(tone));
}

int _pause;

static int key(struct dbx *d, int code, int key, int press)
{
	//u32 *color = fg_n_bg ? &fg_color : &bg_color;

	if (key == ' ') {
		if (press)
			_pause = !_pause;
		return 0;
	}

#if 0
	if (key == '4') {
		rainbow_static = 1;
		return 0;
	}
#endif


	switch (key) {
#if 0
	case '1': *color = GREEN1; rainbow_static = 0; break;
	case '2': *color = RED1; rainbow_static = 0; break;
	case '3': *color = BLUE1; rainbow_static = 0; break;
#endif
	case 'R':
	case 'r':
		clr_ctrl_upd(key, &r, press);
		break;
	case 'G':
	case 'g':
		clr_ctrl_upd(key, &g, press);
		break;
	case 'B':
	case 'b':
		clr_ctrl_upd(key, &b, press);
		break;

	case 't':
		if (press)
			fg_n_bg = !fg_n_bg;
		break;

	//case '0':
	//	if (press)
	//		do_tone = !do_tone;
	//	break;

	case '1': if (press) tone_out(80); break;
	case '2': if (press) tone_out(180); break;
	case '3': if (press) tone_out(280); break;
	case '4': if (press) tone_out(380); break;
	case '5': if (press) tone_out(480); break;
	case '6': if (press) tone_out(580); break;
	case '7': if (press) tone_out(680); break;
	case '8': if (press) tone_out(780); break;
	case '9': if (press) tone_out(880); break;
	case '0': if (press) tone_out(980); break;

	case 'q':
		return -1;
	}
	return 0;
}

struct xp {
	u32 clr;
	u32 x;
	u32 y;
} xp[8];
u32 xpcnt;
static int button(struct dbx *d, int button, int x, int y, int press)
{
	xp[xpcnt].clr = 0xffffff;
	xp[xpcnt].x = x;
	xp[xpcnt].y = y;
	xpcnt = (xpcnt + 1) & 7;

	printf("%d [%d %d] %d\n", button, x, y, press);
	return 0;
}

#define DISPLAY_AMP	6

int display_amp(void)
{
	static int a;
	char *s;

	if (a)
		return a;

	printf("set DBAUD_DISP_AMP to override display amplification\n");
	s = getenv("DBAUD_DISP_AMP");
	if (!s) {
		a = DISPLAY_AMP;
		goto exit;
	}
	a = strtol(s, NULL, 0);
	if (a < 1 || a > 20)
		a = DISPLAY_AMP;
exit:
	printf("display amplification: %d\n", a);
	return a;
}

#define XP_STEP		(255 / (200 / 30))
static void do_xps(struct dbx *d)
{
	int ht = dbx_height(d);
	int wd = dbx_width(d);
	int i, flash = 0, sz;

	for (i = 0; i < ARRAY_SIZE(xp); i++) {
		if (!xp[i].clr)
			continue;

		if (xp[i].clr == 0xffffff) {
			flash = 1;
			xp[i].clr = 0xffc0c0;
			continue;
		}

		if (!flash) {
			sz = 300 - (xp[i].clr >> 16) / 2;
			dbx_fill_circle(d, xp[i].x - sz / 2, xp[i].y - sz / 2, sz,
					xp[i].clr);
		}
		update_color_component(&xp[i].clr, 16, -XP_STEP);
		update_color_component(&xp[i].clr, 8, -XP_STEP - 1);
		update_color_component(&xp[i].clr, 0, -XP_STEP - 1);
	}

	if (flash)
		dbx_fill_rectangle(d, 0, 0, wd, ht, 0xffffff);
}

void random_color(u32 *c)
{
	switch (_random(0, 3)) {
	case 0: *c = RED1;   break;
	case 1: *c = BLUE1;  break;
	case 2: *c = GREEN1; break;
	}
}

float *do_dft(s16 *b, int count)
{
	static FLOAT *tre;
	static FLOAT *tim;
	static FLOAT *fre;
	static FLOAT *fim;
	int i;

	if (!tre) {
		tre = malloc(sizeof(*tre) * count);
		tim = malloc(sizeof(*tim) * count);
		fre = malloc(sizeof(*fre) * count);
		fim = malloc(sizeof(*fim) * count);
	}

	memset(tim, 0, sizeof(*tim) * count);
	for (i = 0; i < count; i++)
		tre[i] = transform(-32767, 32766, b[i], -1.0, 1.0);
	complex_dft(count, tre, tim, fre, fim);

		//(FLOAT) i / (FLOAT) duration,
	for (i = 0; i < count / 2; i++)
		fre[i] = 0.5f * POW(fre[i] * fre[i] + fim[i] * fim[i], 0.5f);
	return fre;
}

#define DFT_BORDER		80
#define SKIP_END_FRAMES		70
void display_spectrum(struct dbx *d, struct audioparam *ap, s16 *b)
{
	int ht = dbx_height(d);
	int wd = dbx_width(d);
	int px = DFT_BORDER, py = -1;
	int x, y, i;
	float fmin, fmax, fs, fy, v;

	float *f = do_dft(b, ap->frames);

	fmin = 1000.0f;
	fmax = 0.0f;
	for (i = SKIP_END_FRAMES; i < ap->frames / 2 - SKIP_END_FRAMES; i++) {
		if (f[i] > fmax)
			fmax = f[i];
		if (f[i] < fmin)
			fmin = f[i];
	}

	for (x = DFT_BORDER; x < wd - DFT_BORDER; x++) {
		fs = transform(DFT_BORDER, wd - DFT_BORDER, x,
				SKIP_END_FRAMES, ap->frames / 2 - SKIP_END_FRAMES);
		v = f[(int)fs];

		fy = transform(fmin, fmax, v, ht - 40, ht - 120);
		y = (int)fy;
		if (py < 0)
			py = y;

		dbx_draw_line(d, px, py, x, y, GREEN1);
		px = x;
		py = y;
	}
}

static int state_update(struct dbx *d)
{
	struct audioparam *ap = &g_in_ap;
	int ht = dbx_height(d);
	int wd = dbx_width(d);
	float fs, fy;
	s16 *b;
	int x, y, v;
	int px = 0, py = ht / 2;

	b = audio_read(ap);
	if (!b)
		return 0;

	if (_pause)
		return 0;

	//dbx_blank_pixmap(d);

	if (!rainbow_static)
		update_color();
	else if (!fg_n_bg)
		random_color(&bg_color);

	dbx_fill_rectangle(d, 0, 0, wd, ht, bg_color);

	for (x = 0; x < wd; x++) {
		fs = transform(0, wd, x, 0, ap->frames);
		v = b[(int)fs];

		int_mod(&v, -32767, 32766, v * display_amp());

		fy = transform(-32767, 32766, v, ht - 20, 20);
		y = (int)fy;

		if (fg_n_bg && rainbow_static)
			random_color(&fg_color);

		//dbx_draw_point(d, x, y, fg_color);
		dbx_draw_line(d, px, py, x, y, fg_color);
		px = x;
		py = y;
	}

	display_spectrum(d, ap, b);

	do_xps(d);

	return 0;
}

#define UPDATE_PERIOD_MS	30
int main(int argc, char *argv[])
{
	struct dbx_ops ops = {
		.update = state_update,
		.motion = NULL,
		.key = key,
		.configure = NULL,
		.button = button,
	};
	struct audioparam *iap, *oap;
	int rate, channels, frames;

	/*
	 * samples / second
	 * 1000000 (microseconds == 1 second)
	 *  1000000 / 44100 ~= 22 microseconds / sample
	 */
	rate = 44100;
	channels = 1;
	/*
	 * API poll rate
	 * 10 milliseconds == 10000 microseconds
	 *  10000 / 22 == 454 samples
	 * 1 frame == 1 sample per channel
	 */
	frames = (UPDATE_PERIOD_MS * 1000) / (1000000 / rate);

	/* buffer size == samples * channels * bits per sample */
	iap = audio_open(channels, rate, frames + 10, 1);
	if (!iap) {
		printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
		exit(0);
	}

	oap = audio_open(2, rate, frames, 0);
	if (!oap) {
		printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
		exit(0);
	}
	//tone_out();

	dbx_run(argc, argv, &ops, UPDATE_PERIOD_MS);

	do_tone = 0;
	usleep(1000 * 10);
	audio_close(iap);
	audio_close(oap);
	return EXIT_SUCCESS;
}
