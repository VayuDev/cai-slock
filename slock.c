/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500
#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <X11/X.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

#include <pango-1.0/pango/pango.h>
#include <pango-1.0/pango/pangocairo.h>

#include "arg.h"
#include "util.h"

char *argv0;

enum {
	INIT,
	INPUT,
	FAILED,
	NUMCOLS
};

struct lock {
	int screen;
	Window root, win;
	Pixmap pmap, fullpmap;
	unsigned long colors[NUMCOLS];


	cairo_surface_t *surface;
	cairo_t *cr;

	PangoLayout *fontLayout;
	PangoFontDescription *fontDesc;
	int rowHeight;
	XImage *screenshot;
	GC gc;
};

struct xrandr {
	int active;
	int evbase;
	int errbase;
};

#include "config.h"

static void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

#ifdef __linux__
#include <fcntl.h>
#include <linux/oom.h>

static void
dontkillme(void)
{
	FILE *f;
	const char oomfile[] = "/proc/self/oom_score_adj";

	if (!(f = fopen(oomfile, "w"))) {
		if (errno == ENOENT)
			return;
		die("slock: fopen %s: %s\n", oomfile, strerror(errno));
	}
	fprintf(f, "%d", OOM_SCORE_ADJ_MIN);
	if (fclose(f)) {
		if (errno == EACCES)
			die("slock: unable to disable OOM killer. "
			    "Make sure to suid or sgid slock.\n");
		else
			die("slock: fclose %s: %s\n", oomfile, strerror(errno));
	}
}
#endif

static const char *
gethash(void)
{
	const char *hash;
	struct passwd *pw;

	/* Check if the current user has a password entry */
	errno = 0;
	if (!(pw = getpwuid(getuid()))) {
		if (errno)
			die("slock: getpwuid: %s\n", strerror(errno));
		else
			die("slock: cannot retrieve password entry\n");
	}
	hash = pw->pw_passwd;

#if HAVE_SHADOW_H
	if (!strcmp(hash, "x")) {
		struct spwd *sp;
		if (!(sp = getspnam(pw->pw_name)))
			die("slock: getspnam: cannot retrieve shadow entry. "
			    "Make sure to suid or sgid slock.\n");
		hash = sp->sp_pwdp;
	}
#else
	if (!strcmp(hash, "*")) {
#ifdef __OpenBSD__
		if (!(pw = getpwuid_shadow(getuid())))
			die("slock: getpwnam_shadow: cannot retrieve shadow entry. "
			    "Make sure to suid or sgid slock.\n");
		hash = pw->pw_passwd;
#else
		die("slock: getpwuid: cannot retrieve shadow entry. "
		    "Make sure to suid or sgid slock.\n");
#endif /* __OpenBSD__ */
	}
#endif /* HAVE_SHADOW_H */

	return hash;
}

static void
readpw(Display *dpy, struct xrandr *rr, struct lock **locks, int nscreens,
       const char *hash)
{
	XRRScreenChangeNotifyEvent *rre;
	char buf[32], passwd[256], *inputhash;
	int num, screen, running, failure;
	unsigned int len, color;
	KeySym ksym;
	XEvent ev;

	len = 0;
	running = 1;
	failure = 0;

	goto render;
	

	while (running && !XNextEvent(dpy, &ev)) {
		if (ev.type == KeyPress) {
			explicit_bzero(&buf, sizeof(buf));
			num = XLookupString(&ev.xkey, buf, sizeof(buf), &ksym, 0);
			if (IsKeypadKey(ksym)) {
				if (ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if (ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}
			if (IsFunctionKey(ksym) ||
			    IsKeypadKey(ksym) ||
			    IsMiscFunctionKey(ksym) ||
			    IsPFKey(ksym) ||
			    IsPrivateKeypadKey(ksym))
				continue;
			switch (ksym) {
			case XK_Return:
				passwd[len] = '\0';
				errno = 0;
				if (!(inputhash = crypt(passwd, hash)))
					fprintf(stderr, "slock: crypt: %s\n", strerror(errno));
				else
					running = !!strcmp(inputhash, hash);
				if (running) {
					XBell(dpy, 100);
					failure = 1;
				}
				explicit_bzero(&passwd, sizeof(passwd));
				len = 0;
				break;
			case XK_Escape:
				explicit_bzero(&passwd, sizeof(passwd));
				len = 0;
				break;
			case XK_BackSpace:
				if (len)
					passwd[len--] = '\0';
				break;
			default:
				if (num && !iscntrl((int)buf[0]) &&
				    (len + num < sizeof(passwd))) {
					memcpy(passwd + len, buf, num);
					len += num;
				}
				break;
			}
			
			//if (running && oldc != color) 
render:
			if(len != 0) {
				color = INPUT;
			} else if(failonclear || failure) {
				color = FAILED;
			} else {
				color = INIT;
			}
			{
				for (screen = 0; screen < nscreens; screen++) {
					const unsigned int w = DisplayWidth(dpy, locks[screen]->screen);
					const unsigned int h = DisplayHeight(dpy, locks[screen]->screen);

					
					XPutImage(dpy, locks[screen]->fullpmap, locks[screen]->gc, locks[screen]->screenshot, 0, 0, 0, 0, w, h);

					cairo_t *cr = locks[screen]->cr;
					
					double centerX = w/2, centerY = h/2;
					double radius = 130 * 2;
					switch(color) {
					case INPUT:
						//random green point
						cairo_set_source_rgba(cr, 0, 1, 0, 0.5);
						
						double x, y;
						do {
							x = (rand() % 15 + 0.5)/15.0 * w;
							y = (rand() % 8 + 0.5)/8.0 * h;
							//printf("%f vs %f\n", x, centerX - radius);
							//fflush(stdout);
						} while(!((x < centerX - radius || x > centerX + radius) || (y < centerY - radius || y > centerY + radius)));
						
						
						cairo_arc(cr, x, y, 30, 0, 2 * G_PI);
						cairo_fill(cr);
					case INIT:
						cairo_set_source_rgba(cr, 0, 0, 0, 0);
						cairo_stroke(cr);

						cairo_set_line_width(cr, 20);
						cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
						cairo_arc(cr, w/2, h/2, radius / 2, 0, 2 * G_PI);
						//cairo_close_path(cr);
						cairo_stroke(cr);
						cairo_set_line_width(cr, 0);
						
						cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
						cairo_arc(cr, w/2, h/2, radius / 2, 0, 2 * G_PI);
						cairo_fill(cr);


						cairo_set_source_rgba(cr, 1, 1, 1, 1);
						pango_layout_set_text(locks[screen]->fontLayout, "Locked", -1);
						int tw = 0, th = 0;
						pango_layout_get_pixel_size(locks[screen]->fontLayout, &tw, &th);

						cairo_move_to(locks[screen]->cr, w/2 - tw/2, h/2 - th/2);
						
            			pango_cairo_show_layout(cr, locks[screen]->fontLayout);
						break;
					case FAILED:
						cairo_set_source_rgba(cr, 1, 0.1, 0.1, 0.5);
						cairo_rectangle(cr, 0, 0, w, h);
						cairo_fill(cr);
						break;
					}
					
					//XSetWindowBackgroundPixmap(dpy, locks[screen]->win, locks[screen]->fullpmap);
    				XClearWindow(dpy, locks[screen]->win);
				}
				XFlush(dpy);
			}
		} else if (rr->active && ev.type == rr->evbase + RRScreenChangeNotify) {
			rre = (XRRScreenChangeNotifyEvent*)&ev;
			for (screen = 0; screen < nscreens; screen++) {
				if (locks[screen]->win == rre->window) {
					XResizeWindow(dpy, locks[screen]->win,
					              rre->width, rre->height);
					XClearWindow(dpy, locks[screen]->win);
				}
			}
		} else for (screen = 0; screen < nscreens; screen++)
			XRaiseWindow(dpy, locks[screen]->win);
	}
}

static struct lock *
lockscreen(Display *dpy, struct xrandr *rr, int screen)
{
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	int i, ptgrab, kbgrab;
	struct lock *lock;
	XColor color, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;

	if (dpy == NULL || screen < 0 || !(lock = malloc(sizeof(struct lock))))
		return NULL;

	lock->screen = screen;
	lock->root = RootWindow(dpy, lock->screen);

	for (i = 0; i < NUMCOLS; i++) {
		XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen),
		                 colorname[i], &color, &dummy);
		lock->colors[i] = color.pixel;
	}

	//take screenshot
	XWindowAttributes gwa;
	XGetWindowAttributes(dpy, DefaultRootWindow(dpy), &gwa);
   	int width = gwa.width;
   	int height = gwa.height;

	XImage *tmpImg = XGetImage(dpy, DefaultRootWindow(dpy), 0, 0, width, height, AllPlanes, ZPixmap);

	Visual *v = DefaultVisual(dpy, lock->screen);
	lock->screenshot = XSubImage(tmpImg, 0, 0, width, height);

	printf("%i", lock->screenshot->bits_per_pixel);
	
	fflush(stdout);

	int stride = rand() % 20 - 10;
	for(int x = 0; x < width; ++x) {
		for(int y = 0; y < height; ++y) {
			if(x % 5 == 0) {
				stride =  rand() % 20 - 10;;
		}
			
		
			/*lock->screenshot->data[] = 0;
			lock->screenshot->data[y * 3 + height * x * 3 + 1] = 0;
			lock->screenshot->data[y * 3 + height * x * 3 + 2] = 0;*/

			char r, g, b;

			if(stride + x < 0) {
				r = 0;
				b = 0;
				g = 0;
			}
			else if(stride + x >= width) {
				r = 0;
				b = 0;
				g = 0;
			} else {
				r = tmpImg->data[(y + stride) * 4 + height * (x) * 4 + 0];
				g = tmpImg->data[(y + stride) * 4 + height * (x) * 4 + 1];
				b = tmpImg->data[(y + stride) * 4 + height * (x) * 4 + 2];
			}
			

			/*short sum = r + g + b;
			sum /= 3;
			r = sum; 
			g = sum;
			b = sum;*/
			//r = 128 - r;
			//g = 128 - g;
			//b = 128 - b;

			lock->screenshot->data[y * 4 + height * x * 4 + 0] = r;
			lock->screenshot->data[y * 4 + height * x * 4 + 1] = g;
			lock->screenshot->data[y * 4 + height * x * 4 + 2] = b;
		}
	}



	/* init */
	wa.override_redirect = 1;
	lock->win = XCreateWindow(dpy, lock->root, 0, 0,
	                          DisplayWidth(dpy, lock->screen),
	                          DisplayHeight(dpy, lock->screen),
	                          0, DefaultDepth(dpy, lock->screen),
	                          CopyFromParent,
	                          DefaultVisual(dpy, lock->screen),
	                          CWOverrideRedirect, &wa);
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap,
	                                &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);

	//cairo stuff
	lock->fullpmap = XCreatePixmap(dpy, lock->win, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen), 24);
	XSetWindowBackgroundPixmap(dpy, lock->win, lock->fullpmap);

	lock->gc = XCreateGC(dpy, lock->fullpmap, 0, NULL);;

	
	lock->surface = cairo_xlib_surface_create(dpy, lock->fullpmap, v, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen));
	lock->cr = cairo_create(lock->surface);

	lock->fontLayout = pango_cairo_create_layout(lock->cr);
	lock->fontDesc = pango_font_description_from_string("Sans 40");

    pango_layout_set_font_description(lock->fontLayout, lock->fontDesc);
	pango_cairo_update_layout (lock->cr, lock->fontLayout);

	pango_layout_set_text(lock->fontLayout, "This text is for checking the text height ", -1);
    
    int tmp;
	pango_layout_get_pixel_size(lock->fontLayout, &tmp, &lock->rowHeight);
	//end off cairo stuff

	/* Try to grab mouse pointer *and* keyboard for 600ms, else fail the lock */
	for (i = 0, ptgrab = kbgrab = -1; i < 6; i++) {
		if (ptgrab != GrabSuccess) {
			ptgrab = XGrabPointer(dpy, lock->root, False,
			                      ButtonPressMask | ButtonReleaseMask |
			                      PointerMotionMask, GrabModeAsync,
			                      GrabModeAsync, None, invisible, CurrentTime);
		}
		if (kbgrab != GrabSuccess) {
			kbgrab = XGrabKeyboard(dpy, lock->root, True,
			                       GrabModeAsync, GrabModeAsync, CurrentTime);
		}

		/* input is grabbed: we can lock the screen */
		if (ptgrab == GrabSuccess && kbgrab == GrabSuccess) {
			XMapRaised(dpy, lock->win);
			if (rr->active)
				XRRSelectInput(dpy, lock->win, RRScreenChangeNotifyMask);

			XSelectInput(dpy, lock->root, SubstructureNotifyMask);
			return lock;
		}

		/* retry on AlreadyGrabbed but fail on other errors */
		if ((ptgrab != AlreadyGrabbed && ptgrab != GrabSuccess) ||
		    (kbgrab != AlreadyGrabbed && kbgrab != GrabSuccess))
			break;

		usleep(100000);
	}

	/* we couldn't grab all input: fail out */
	if (ptgrab != GrabSuccess)
		fprintf(stderr, "slock: unable to grab mouse pointer for screen %d\n",
		        screen);
	if (kbgrab != GrabSuccess)
		fprintf(stderr, "slock: unable to grab keyboard for screen %d\n",
		        screen);
	return NULL;
}

static void
usage(void)
{
	die("usage: slock [-v] [cmd [arg ...]]\n");
}

int
main(int argc, char **argv) {
	srand(time(0));
	struct xrandr rr;
	struct lock **locks;
	struct passwd *pwd;
	struct group *grp;
	uid_t duid;
	gid_t dgid;
	const char *hash;
	Display *dpy;
	int s, nlocks, nscreens;

	ARGBEGIN {
	case 'v':
		fprintf(stderr, "slock-"VERSION"\n");
		return 0;
	default:
		usage();
	} ARGEND

	/* validate drop-user and -group */
	errno = 0;
	if (!(pwd = getpwnam(user)))
		die("slock: getpwnam %s: %s\n", user,
		    errno ? strerror(errno) : "user entry not found");
	duid = pwd->pw_uid;
	errno = 0;
	if (!(grp = getgrnam(group)))
		die("slock: getgrnam %s: %s\n", group,
		    errno ? strerror(errno) : "group entry not found");
	dgid = grp->gr_gid;

#ifdef __linux__
	dontkillme();
#endif

	hash = gethash();
	errno = 0;
	if (!crypt("", hash))
		die("slock: crypt: %s\n", strerror(errno));

	if (!(dpy = XOpenDisplay(NULL)))
		die("slock: cannot open display\n");

	/* drop privileges */
	if (setgroups(0, NULL) < 0)
		die("slock: setgroups: %s\n", strerror(errno));
	if (setgid(dgid) < 0)
		die("slock: setgid: %s\n", strerror(errno));
	if (setuid(duid) < 0)
		die("slock: setuid: %s\n", strerror(errno));

	/* check for Xrandr support */
	rr.active = XRRQueryExtension(dpy, &rr.evbase, &rr.errbase);

	/* get number of screens in display "dpy" and blank them */
	nscreens = ScreenCount(dpy);
	if (!(locks = calloc(nscreens, sizeof(struct lock *))))
		die("slock: out of memory\n");
	for (nlocks = 0, s = 0; s < nscreens; s++) {
		if ((locks[s] = lockscreen(dpy, &rr, s)) != NULL)
			nlocks++;
		else
			break;
	}
	XSync(dpy, 0);

	/* did we manage to lock everything? */
	if (nlocks != nscreens)
		return 1;

	/* run post-lock command */
	if (argc > 0) {
		switch (fork()) {
		case -1:
			die("slock: fork failed: %s\n", strerror(errno));
		case 0:
			if (close(ConnectionNumber(dpy)) < 0)
				die("slock: close: %s\n", strerror(errno));
			execvp(argv[0], argv);
			fprintf(stderr, "slock: execvp %s: %s\n", argv[0], strerror(errno));
			_exit(1);
		}
	}

	/* everything is now blank. Wait for the correct password */
	readpw(dpy, &rr, locks, nscreens, hash);

	return 0;
}
