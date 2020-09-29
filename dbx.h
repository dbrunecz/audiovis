#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <stdint.h>

typedef uint32_t	u32;
typedef uint16_t	u16;
typedef uint8_t		u8;

typedef  int16_t	s16;

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))

#define MIN(x, y)	(((x) < (y)) ? (x) : (y))
#define MAX(x, y)	(((x) > (y)) ? (x) : (y))

#define RGB(r, g, b)	((((r) & 0xff) << 16) | \
			 (((g) & 0xff) <<  8) | \
			 (((b) & 0xff) <<  0))

u32 tickcount_ms(void);
float transform(float min_in, float max_in, float in,
		float min_out, float max_out);
void int_mod(int *v, int min, int max, int delta);

/******************************************************************************/

struct dbx;

struct dbx_ops {
	int (*update)(struct dbx *);
	int (*motion)(struct dbx *, XMotionEvent *);
	int (*configure)(struct dbx *, XConfigureEvent *);
	int (*key)(struct dbx *, int , int , int );
	int (*button)(struct dbx *, int button, int x, int y, int press);
};

void dbx_run(int argc, char *argv[], struct dbx_ops *ops, u32 t_ms);

int dbx_width(struct dbx *d);
int dbx_height(struct dbx *d);

int dbx_blank_pixmap(struct dbx *d);
int dbx_fill_rectangle(struct dbx *d, int x, int y, int wd, int ht, u32 rgb);
int dbx_fill_circle(struct dbx *d, int x, int y, int dia, u32 rgb);
int dbx_draw_string(struct dbx *d, int x, int y, const char *s, size_t len, u32 rgb);
int dbx_draw_point(struct dbx *d, int x, int y, u32 rgb);
int dbx_draw_line(struct dbx *d, int x1, int y1, int x2, int y2, u32 rgb);
