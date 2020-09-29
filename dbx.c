#include <stdio.h>
#include <time.h>

#include "dbx.h"

u32 tickcount_ms(void)
{
	struct timespec tp;
	static u32 start;

	if (clock_gettime(CLOCK_MONOTONIC, &tp)) {
		printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
		return 0;
	}
	if (!start)
		start = tp.tv_sec * 1000 + tp.tv_nsec / 1000000;
	return (tp.tv_sec * 1000 + tp.tv_nsec / 1000000) - start;
}

float transform(float min_in, float max_in, float in, float min_out, float max_out)
{
	return min_out + ((in - min_in) * (max_out - min_out)) / (max_in - min_in);
}

void int_mod(int *v, int min, int max, int delta)
{
	*v += delta;
	*v = MIN(*v, max);
	*v = MAX(*v, min);
}

/******************************************************************************/

#define CLR_CNT	2500

struct dbx {
	XFontStruct *font;
	Display *display;
	Pixmap pixmap;
	Colormap cm;
	Window win;
	int screen;
	int width;
	int height;
	GC gc;
	XColor colors[CLR_CNT];
	u32 rgbs[CLR_CNT];
	int clr_cnt;
};

static int keycode(Display *display, int k, int shift)
{
	static int syms_per_code;
	static int min, max;
	static KeySym *ks;

	//k = XLookupKeysym(e, 0);
	if (!max) {
		XDisplayKeycodes(display, &min, &max);
		ks = XGetKeyboardMapping(display, min, max - min + 1, &syms_per_code);
	}
	return ks[(k - min) * syms_per_code + shift];
}

static int handle_events(struct dbx *d, struct dbx_ops *ops)
{
	XEvent e;
	int k;

	for ( ;XPending(d->display); ) {
		XNextEvent(d->display, &e);

		switch (e.type) {
		case Expose:
			if (e.xexpose.count)
				break;

			if (!XCopyArea(d->display, d->pixmap, d->win, d->gc, 0, 0,
					d->width, d->height, 0, 0)) {
				printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
				return -1;
			}
			break;
		case MotionNotify:
			if (ops->motion)
				if (ops->motion(d, &e.xmotion))
					return -1;
			break;
		case ConfigureNotify:
			if (ops->configure)
				if (ops->configure(d, &e.xconfigure))
					return -1;
			break;
		case KeyPress:
		case KeyRelease:
			if (ops->key) {
				k = keycode(d->display, e.xkey.keycode,
						e.xkey.state & 1);
				if (ops->key(d, e.xkey.keycode, k,
					     e.type == KeyPress))
					return -1;
			}
			break;
		case ButtonPress://XEvent
			if (ops->button)
				if (ops->button(d, e.xbutton.button, e.xbutton.x, e.xbutton.y,
						e.xbutton.type == ButtonPress))
					return -1;
			break;
		}
	}
	return 0;
}

static void dbx_loop(struct dbx *d, struct dbx_ops *ops, u32 t_ms)
{
	int fd = ConnectionNumber(d->display);
	struct timeval tv = { .tv_sec = 0 };
	fd_set in_fds;
	u32 tc, t;
	int ret;

	ops->update(d);

	tc = tickcount_ms();
	for ( ;; ) {
		FD_ZERO(&in_fds);
		FD_SET(fd, &in_fds);

		t = tickcount_ms();
		tv.tv_usec = 1000 * (t_ms - MIN(t_ms, (t - tc)));

		ret = select(fd + 1, &in_fds, NULL, NULL, &tv);
		if (ret < 0)
			printf("An error occured!\n");
		else if (ret == 0) {
			ops->update(d);
			if (!XCopyArea(d->display, d->pixmap, d->win, d->gc, 0, 0,
					d->width, d->height, 0, 0)) {
				printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
				return;
			}
			tc = tickcount_ms();
		}

		if (handle_events(d, ops))
			return;
	}
}

static int set_window_properties(Display *display, Window win, int argc, char *argv[])
{
	char *window_name = "DBB X Test";
	char *icon_name = "HelloX";
	XClassHint *class_hints;
	XSizeHints *size_hints;
	XWMHints *wm_hints;
	XTextProperty windowName;
	XTextProperty iconName;
	char * appname = argv[0];

	if (!XStringListToTextProperty(&window_name, 1, &windowName)) {
		fprintf(stderr,
			"%s: structure allocation for windowName failed.\n",
			appname);
		return -1;
	}

	if (!XStringListToTextProperty(&icon_name, 1, &iconName)) {
		fprintf(stderr,
			"%s: structure allocation for iconName failed.\n",
			appname);
		return -1;
	}

	size_hints  = XAllocSizeHints();
	wm_hints    = XAllocWMHints();
	class_hints = XAllocClassHint();
	if (!size_hints || !wm_hints || !class_hints) {
		fprintf(stderr, "%s: couldn't allocate memory.\n", appname);
		return -1;
	}

	size_hints->flags = PPosition | PSize | PMinSize;
	size_hints->min_width = 200;
	size_hints->min_height = 100;

	wm_hints->flags         = StateHint | InputHint;
	wm_hints->initial_state = NormalState;
	wm_hints->input         = True;

	class_hints->res_name = appname;
	class_hints->res_class = "hellox";

	XSetWMProperties(display, win, &windowName, &iconName, argv, argc,
			 size_hints, wm_hints, class_hints);
	return 0;
}

static Colormap init_colormap(struct dbx *d, Window rwin)
{
	XVisualInfo vi;

	d->clr_cnt = 0;

	if (!XMatchVisualInfo(d->display, d->screen, 24, TrueColor, &vi)) {
		printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
		return -1;
	}

	return XCreateColormap(d->display, rwin, vi.visual, AllocNone);
}

static int dbx_init(struct dbx *d, int argc, char *argv[])
{
	char *display_name = NULL;
	u32 display_width;
	u32 display_height;
	u32 border_width = 1;
	XGCValues values;
	Window rwin;
	int depth;

	d->display = XOpenDisplay(display_name);
	if (!d->display) {
		fprintf(stderr, "%s: couldn't connect to X server %s\n",
			argv[0], display_name);
		return -1;
	}

	d->screen = DefaultScreen(d->display);

	rwin = RootWindow(d->display, d->screen);

	display_width = DisplayWidth(d->display, d->screen);
	display_height = DisplayHeight(d->display, d->screen);
	depth = DefaultDepth(d->display, d->screen);

	d->width  = display_width / 2;
	d->height = display_height / 2;

	d->win = XCreateSimpleWindow(d->display, rwin, 0, 0, d->width, d->height,
					border_width,
					BlackPixel(d->display, d->screen),
					WhitePixel(d->display, d->screen));
	set_window_properties(d->display, d->win, argc, argv);

	d->cm = init_colormap(d, rwin);
	XSetWindowColormap(d->display, d->win, d->cm);

	d->gc = XCreateGC(d->display, d->win, 0, &values);

	d->pixmap = XCreatePixmap(d->display, d->win, d->width, d->height, depth);

	XSelectInput(d->display, d->win, ExposureMask | KeyPressMask |
			KeyReleaseMask | PointerMotionMask | ButtonPressMask |
			StructureNotifyMask);

	d->font = XLoadQueryFont(d->display, "9x15");
	if (!d->font) {
		fprintf(stderr, "%s: cannot open 9x15 font.\n", argv[0]);
		return -1;
	}
	XSetFont(d->display, d->gc, d->font->fid);

	XMapWindow(d->display, d->win);

	return 0;
}

static void dbx_deinit(struct dbx *d)
{
	XFreePixmap(d->display, d->pixmap);
	XUnloadFont(d->display, d->font->fid);
	XFreeGC(d->display, d->gc);
	XCloseDisplay(d->display);
}

void dbx_run(int argc, char *argv[], struct dbx_ops *ops, u32 t_ms)
{
	struct dbx d;

	dbx_init(&d, argc, argv);
	dbx_loop(&d, ops, t_ms);
	dbx_deinit(&d);
}

/******************************************************************************/

#define CLR(v)		(((v) & 0xff) << 8)
static void color_init(XColor *c, u32 v)
{
	c->red   = CLR(v >> 16);
	c->green = CLR(v >>  8);
	c->blue  = CLR(v >>  0);
	c->flags = DoRed | DoGreen | DoBlue;
}

static unsigned long rgb2pixel(struct dbx *d, u32 rgb)
{
	XColor *c;
	int i;

	if (!rgb)
		return BlackPixel(d->display, d->screen);
	if (rgb == RGB(255, 255, 255))
		return WhitePixel(d->display, d->screen);

	for (i = 0; i < d->clr_cnt; i++)
		if (d->rgbs[i] == rgb)
			return d->colors[i].pixel;

	if (ARRAY_SIZE(d->colors) == d->clr_cnt + 1) {
		printf("! color array size (%u)\n", tickcount_ms());
		return WhitePixel(d->display, d->screen);
	}

	c = &d->colors[d->clr_cnt];
	d->rgbs[d->clr_cnt++] = rgb;

	color_init(c, rgb);
	if (!XAllocColor(d->display, d->cm, c))
		printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
	else
		printf("RGB %04x %04x %04x | %06lx\n", c->red, c->green, c->blue,
			c->pixel);

	return c->pixel;
}

int dbx_set_foreground(struct dbx *d, u32 rgb)
{
	static unsigned long p = 0xbadf00d;
	unsigned long pixel = rgb2pixel(d, rgb);

	if (p == pixel)
		return 0;

	if (!XSetForeground(d->display, d->gc, pixel))
		printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
	else
		p = pixel;
	return 0;
}

int dbx_fill_rectangle(struct dbx *d, int x, int y, int wd, int ht,
		    u32 rgb)
{
	dbx_set_foreground(d, rgb);
	if (!XFillRectangle(d->display, d->pixmap, d->gc, x, y, wd, ht))
		printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
	return 0;
}

int dbx_blank_pixmap(struct dbx *d)
{
	dbx_fill_rectangle(d, 0, 0, d->width, d->height, RGB(0, 0, 0));
	return 0;
}

int dbx_draw_string(struct dbx *d, int x, int y, const char *s, size_t len,
		    u32 rgb)
{
	dbx_set_foreground(d, rgb);
	if (XDrawString(d->display, d->pixmap, d->gc, x, y, s, len))
		printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
	return 0;
}

int dbx_fill_circle(struct dbx *d, int x, int y, int dia, u32 rgb)
{
	dbx_set_foreground(d, rgb);
	if (!XFillArc(d->display, d->pixmap, d->gc, x, y, dia, dia, 0, 360 * 64))
		printf("%s:%d %s()\n", __FILE__, __LINE__, __func__);
	return 0;
}

int dbx_draw_point(struct dbx *d, int x, int y, u32 rgb)
{
	dbx_set_foreground(d, rgb);
	XDrawPoint(d->display, d->pixmap, d->gc, x, y);
	return 0;
}

int dbx_draw_line(struct dbx *d, int x1, int y1, int x2, int y2, u32 rgb)
{
	dbx_set_foreground(d, rgb);
	XDrawLine(d->display, d->pixmap, d->gc, x1, y1, x2, y2);
	return 0;
}

int dbx_width(struct dbx *d)
{
	return d->width;
}

int dbx_height(struct dbx *d)
{
	return d->height;
}
