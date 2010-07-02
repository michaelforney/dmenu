#include <X11/Xlib.h>
#include <draw.h>
#include "config.h"

/* macros */
#define INRECT(X,Y,RX,RY,RW,RH) ((X) >= (RX) && (X) < (RX) + (RW) && (Y) >= (RY) && (Y) < (RY) + (RH))
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(a, b)               ((a) > (b) ? (a) : (b))
#define IS_UTF8_1ST_CHAR(c)     ((((c) & 0xc0) == 0xc0) || !((c) & 0x80))

/* forward declarations */
void drawbar(void);
void grabkeyboard(void);
void kpress(XKeyEvent *e);
void run(void);
void setup(unsigned int lines);

/* variables */
extern char *prompt;
extern char text[4096];
extern int promptw;
extern int screen;
extern unsigned int numlockmask;
extern unsigned int mw, mh;
extern unsigned long normcol[ColLast];
extern unsigned long selcol[ColLast];
extern Bool topbar;
extern DC dc;
extern Display *dpy;
extern Window win, root;
