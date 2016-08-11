/*  $Revision: 1.4 $
**
**  Main editing routines for editline library.
*/
#include "editline.h"
#include <ctype.h>

/*
**  Manifest constants.
*/
#define SCREEN_WIDTH 80
#define SCREEN_ROWS  24
#define NO_ARG       (-1)
#define DEL          127
#define CTL(x)       ((x) & 0x1F)
#define ISCTL(x)     ((x) && (x) < ' ')
#define UNCTL(x)     ((x) + 64)
#define META(x)      ((x) | 0x80)
#define ISMETA(x)    ((x) & 0x80)
#define UNMETA(x)    ((x) & 0x7F)

#ifndef HIST_SIZE
#define HIST_SIZE    20
#endif  /* !defined(HIST_SIZE) */

/*
**  Command status codes.
*/
typedef enum _STATUS {
    CSdone,
    CSeof,
    CSmove,
    CSdispatch,
    CSstay
} STATUS;

/*
**  The type of case-changing to perform.
*/
typedef enum _CASE {
    TOupper,
    TOlower
} CASE;

/*
**  Key to command mapping.
*/
typedef struct _KEYMAP {
    CHAR Key;
    STATUS (*Function)(void);
} KEYMAP;

/*
**  Command history structure.
*/
typedef struct _HISTORY {
    int Size;
    int Pos;
    CHAR *Lines[HIST_SIZE];
} HISTORY;

/*
**  Globals.
*/
int rl_eof;
int rl_erase;
int rl_intr;
int rl_kill;

STATIC CHAR NIL[] = "";
STATIC CONST CHAR *Input = NIL;
STATIC CHAR *Line;
STATIC CONST char *Prompt;
STATIC CHAR *Yanked;
STATIC char *Screen;
STATIC char NEWLINE[] = CRLF;
STATIC HISTORY H;
int rl_quit;
STATIC int Repeat;
STATIC int End;
STATIC int Mark;
STATIC int OldPoint;
STATIC int Point;
STATIC int PushBack;
STATIC int Pushed;
STATIC SIZE_T Length;
STATIC SIZE_T ScreenCount;
STATIC SIZE_T ScreenSize;
STATIC char *backspace;
STATIC int TTYwidth;
STATIC int TTYrows;

/*
 *  Removed FORWARD addressing ...
 */

STATIC STATUS accept_line(void);
STATIC STATUS beg_line(void);
STATIC STATUS bk_char(void);
STATIC STATUS bk_del_char(void);
STATIC STATUS bk_kill_word(void);
STATIC STATUS mk_set(void);
STATIC STATUS bk_word(void);
STATIC STATUS fd_kill_word(void);
STATIC STATUS fd_word(void);
STATIC STATUS fd_char(void);
STATIC STATUS case_down_word(void);
STATIC STATUS case_up_word(void);
STATIC STATUS copy_region(void);
STATIC STATUS del_char(void);
STATIC STATUS end_line(void);
STATIC STATUS exchange(void);
STATIC STATUS kill_line(void);
STATIC STATUS last_argument(void);
STATIC STATUS c_possible(void);
STATIC STATUS c_complete(void);
STATIC STATUS meta(void);
STATIC STATUS move_to_char(void);
STATIC STATUS h_first(void);
STATIC STATUS h_last(void);
STATIC STATUS h_next(void);
STATIC STATUS h_prev(void);
STATIC STATUS h_search(void);
STATIC STATUS quote(void);
STATIC STATUS transpose(void);
STATIC STATUS redisplay(void);
STATIC STATUS ring_bell(void);
STATIC STATUS wipe(void);
STATIC STATUS yank(void);

STATIC KEYMAP Map[33] = {
    { CTL('@'), ring_bell },
    { CTL('A'), beg_line },
    { CTL('B'), bk_char },
    { CTL('D'), del_char },
    { CTL('E'), end_line },
    { CTL('F'), fd_char },
    { CTL('G'), ring_bell },
    { CTL('H'), bk_del_char },
    { CTL('I'), c_complete },
    { CTL('J'), accept_line },
    { CTL('K'), kill_line },
    { CTL('L'), redisplay },
    { CTL('M'), accept_line },
    { CTL('N'), h_next },
    { CTL('O'), ring_bell },
    { CTL('P'), h_prev },
    { CTL('Q'), ring_bell },
    { CTL('R'), h_search },
    { CTL('S'), ring_bell },
    { CTL('T'), transpose },
    { CTL('U'), ring_bell },
    { CTL('V'), quote },
    { CTL('W'), wipe },
    { CTL('X'), exchange },
    { CTL('Y'), yank },
    { CTL('Z'), ring_bell },
    { CTL('['), meta },
    { CTL(']'), move_to_char },
    { CTL('^'), ring_bell },
    { CTL('_'), ring_bell },
    { 0, NULL }
};

STATIC KEYMAP MetaMap[16] = {
    { CTL('H'), bk_kill_word },
    { DEL, bk_kill_word },
    { ' ', mk_set },
    { '.', last_argument },
    { '<', h_first },
    { '>', h_last },
    { '?', c_possible },
    { 'b', bk_word },
    { 'd', fd_kill_word },
    { 'f', fd_word },
    { 'l', case_down_word },
    { 'u', case_up_word },
    { 'y', yank },
    { 'w', copy_region },
    { 0, NULL }
};

/* Display print 8-bit chars as `M-x' or as the actual 8-bit char? */
int rl_meta_chars = 1;

/*
**  Declarations.
*/
STATIC CHAR *editinput(void);

/*
**  TTY input/output functions.
*/

STATIC void TTYflush(void)
{
    if (ScreenCount) {
        if (write(1, Screen, ScreenCount) != 1) {
            /* FIXME: handle error */
        }
        ScreenCount = 0;
    }
}

STATIC void TTYput(CHAR c)
{
    Screen[ScreenCount] = c;
    if (++ScreenCount >= ScreenSize - 1) {
        ScreenSize += SCREEN_INC;
        RENEW(Screen, char, ScreenSize);
    }
}

STATIC void TTYputs(CHAR *p)
{
    while (*p) {
        TTYput(*p++);
    }
}

STATIC void TTYshow(CHAR c)
{
    if (c == DEL) {
        TTYput('^');
        TTYput('?');
    } else if (ISCTL(c)) {
        TTYput('^');
        TTYput(UNCTL(c));
    } else if (rl_meta_chars && ISMETA(c)) {
        TTYput('M');
        TTYput('-');
        TTYput(UNMETA(c));
    } else {
        TTYput(c);
    }
}

STATIC void TTYstring(CHAR *p)
{
    while (*p) {
        TTYshow(*p++);
    }
}

STATIC unsigned int TTYget(void)
{
    CHAR c;

    TTYflush();
    if (Pushed) {
        Pushed = 0;
        return PushBack;
    }
    if (*Input) {
        return *Input++;
    }
    return read(0, &c, (SIZE_T)1) == 1 ? c : EOF;
}

#define TTYback() (backspace ? TTYputs((CHAR *)backspace) : TTYput('\b'))

STATIC void TTYbackn(int n)
{
    while (--n >= 0) {
        TTYback();
    }
}

STATIC void TTYinfo(void)
{
    static int init;

#ifdef USE_TERMCAP
    char *term;
    char buff[2048];
    char *bp;
#endif  /* defined(USE_TERMCAP) */

#ifdef TIOCGWINSZ
    struct winsize W;
#endif  /* defined(TIOCGWINSZ) */

    if (init) {
#ifdef TIOCGWINSZ
        /* Perhaps we got resized. */
        if (ioctl(0, TIOCGWINSZ, &W) >= 0 && W.ws_col > 0 && W.ws_row > 0) {
            TTYwidth = (int)W.ws_col;
            TTYrows = (int)W.ws_row;
        }
#endif  /* defined(TIOCGWINSZ) */
        return;
    }
    init++;

    TTYwidth = TTYrows = 0;
#ifdef USE_TERMCAP
    bp = &buff[0];
    if ((term = getenv("TERM")) == NULL) {
        term = "dumb";
    }
    if (tgetent(buff, term) < 0) {
       TTYwidth = SCREEN_WIDTH;
       TTYrows = SCREEN_ROWS;
       return;
    }
    backspace = tgetstr("le", &bp);
    TTYwidth = tgetnum("co");
    TTYrows = tgetnum("li");
#endif  /* defined(USE_TERMCAP) */

#ifdef TIOCGWINSZ
    if (ioctl(0, TIOCGWINSZ, &W) >= 0) {
        TTYwidth = (int)W.ws_col;
        TTYrows = (int)W.ws_row;
    }
#endif  /* defined(TIOCGWINSZ) */

    if (TTYwidth <= 0 || TTYrows <= 0) {
        TTYwidth = SCREEN_WIDTH;
        TTYrows = SCREEN_ROWS;
    }
}

/*
**  Print an array of words in columns.
*/
STATIC void columns(int ac, CHAR **av)
{
    CHAR *p;
    int i;
    int j;
    int k;
    int len;
    int skip;
    int longest;
    int cols;

    /* Find longest name, determine column count from that. */
    for (longest = 0, i = 0; i < ac; i++) {
        if ((j = strlen((char *)av[i])) > longest) {
            longest = j;
        }
    }
    cols = TTYwidth / (longest + 3);

    TTYputs((CHAR *)NEWLINE);
    for (skip = ac / cols + 1, i = 0; i < skip; i++) {
        for (j = i; j < ac; j += skip) {
            for (p = av[j], len = strlen((char *)p), k = len; --k >= 0; p++) {
                TTYput(*p);
            }
            if (j + skip < ac) {
                while (++len < longest + 3) {
                    TTYput(' ');
                }
            }
        }
        TTYputs((CHAR *)NEWLINE);
    }
}

STATIC void reposition(void)
{
    int i;
    CHAR *p;

    TTYput('\r');
    TTYputs((CHAR *)Prompt);
    for (i = Point, p = Line; --i >= 0; p++) {
        TTYshow(*p);
    }
}

STATIC void left(STATUS Change)
{
    TTYback();
    if (Point) {
        if (ISCTL(Line[Point - 1])) {
            TTYback();
        } else if (rl_meta_chars && ISMETA(Line[Point - 1])) {
            TTYback();
            TTYback();
        }
    }
    if (Change == CSmove) {
        Point--;
    }
}

STATIC void right(STATUS Change)
{
    TTYshow(Line[Point]);
    if (Change == CSmove) {
        Point++;
    }
}

STATIC STATUS ring_bell(void)
{
    TTYput('\07');
    TTYflush();
    return CSstay;
}

STATIC STATUS do_macro(unsigned int c)
{
    CHAR name[4];

    name[0] = '_';
    name[1] = c;
    name[2] = '_';
    name[3] = '\0';

    if ((Input = (CHAR *)getenv((char *)name)) == NULL) {
        Input = NIL;
        return ring_bell();
    }
    return CSstay;
}

STATIC STATUS do_forward(STATUS move)
{
    int i;
    CHAR *p;

    i = 0;
    do {
        p = &Line[Point];
        for ( ; Point < End && (*p == ' ' || !isalnum(*p)); Point++, p++) {
            if (move == CSmove) {
                right(CSstay);
            }
        }

        for (; Point < End && isalnum(*p); Point++, p++) {
            if (move == CSmove) {
                right(CSstay);
            }
        }

        if (Point == End) {
            break;
        }
    } while (++i < Repeat);

    return CSstay;
}

STATIC STATUS do_case(CASE type)
{
    int i;
    int end;
    int count;
    CHAR *p;

    do_forward(CSstay);
    if (OldPoint != Point) {
        if ((count = Point - OldPoint) < 0) {
            count = -count;
        }
        Point = OldPoint;
        if ((end = Point + count) > End) {
            end = End;
        }
        for (i = Point, p = &Line[i]; i < end; i++, p++) {
            if (type == TOupper) {
                if (islower(*p)) {
                    *p = toupper(*p);
                }
            } else if (isupper(*p)) {
                *p = tolower(*p);
            }
            right(CSmove);
        }
    }
    return CSstay;
}

STATIC STATUS case_down_word(void)
{
    return do_case(TOlower);
}

STATIC STATUS case_up_word(void)
{
    return do_case(TOupper);
}

STATIC void ceol(void)
{
    int extras;
    int i;
    CHAR *p;

    for (extras = 0, i = Point, p = &Line[i]; i <= End; i++, p++) {
        TTYput(' ');
        if (ISCTL(*p)) {
            TTYput(' ');
            extras++;
        } else if (rl_meta_chars && ISMETA(*p)) {
            TTYput(' ');
            TTYput(' ');
            extras += 2;
        }
    }

    for (i += extras; i > Point; i--) {
        TTYback();
    }
}

STATIC void clear_line(void)
{
    Point = -strlen(Prompt);
    TTYput('\r');
    ceol();
    Point = 0;
    End = 0;
    Line[0] = '\0';
}

STATIC STATUS insert_string(CHAR *p)
{
    SIZE_T len;
    int i;
    CHAR *new;
    CHAR *q;

    len = strlen((char *)p);
    if (End + len >= Length) {
        if ((new = NEW(CHAR, Length + len + MEM_INC)) == NULL) {
            return CSstay;
        }
        if (Length) {
            COPYFROMTO(new, Line, Length);
            DISPOSE(Line);
        }
        Line = new;
        Length += len + MEM_INC;
    }

    for (q = &Line[Point], i = End - Point; --i >= 0;) {
        q[len + i] = q[i];
    }
    COPYFROMTO(&Line[Point], p, len);
    End += len;
    Line[End] = '\0';
    TTYstring(&Line[Point]);
    Point += len;

    return Point == End ? CSstay : CSmove;
}

STATIC CHAR *next_hist(void)
{
    return H.Pos >= H.Size - 1 ? NULL : H.Lines[++H.Pos];
}

STATIC CHAR *prev_hist(void)
{
    return H.Pos == 0 ? NULL : H.Lines[--H.Pos];
}

STATIC STATUS do_insert_hist(CHAR *p)
{
    if (p == NULL) {
        return ring_bell();
    }
    Point = 0;
    reposition();
    ceol();
    End = 0;
    return insert_string(p);
}

STATIC STATUS do_hist(CHAR *(*move)(void))
{
    CHAR *p;
    int i;

    i = 0;
    do {
        if ((p = (*move)()) == NULL) {
            return ring_bell();
        }
    } while (++i < Repeat);
    return do_insert_hist(p);
}

STATIC STATUS h_next(void)
{
    return do_hist(next_hist);
}

STATIC STATUS h_prev(void)
{
    return do_hist(prev_hist);
}

STATIC STATUS h_first(void)
{
    return do_insert_hist(H.Lines[H.Pos = 0]);
}

STATIC STATUS h_last(void)
{
    return do_insert_hist(H.Lines[H.Pos = H.Size - 1]);
}

/*
**  Return zero if pat appears as a substring in text.
*/
STATIC int substrcmp(const char *text, const char *pat, size_t len)
{
    CHAR c;

    if ((c = *pat) == '\0') {
        return *text == '\0';
    }
    for ( ; *text; text++) {
        if (*text == c && strncmp(text, pat, len) == 0) {
            return 0;
        }
    }
    return 1;
}

STATIC CHAR *search_hist(CHAR *search, CHAR *(*move)(void))
{
    static CHAR *old_search;
    int len;
    int pos;
    int (*match)(const char *, const char *, size_t);
    char *pat;

    /* Save or get remembered search pattern. */
    if (search && *search) {
        if (old_search) {
            DISPOSE(old_search);
        }
        old_search = (CHAR *)strdup((char *)search);
    } else {
        if (old_search == NULL || *old_search == '\0') {
            return NULL;
        }
        search = old_search;
    }

    /* Set up pattern-finder. */
    if (*search == '^') {
        match = strncmp;
        pat = (char *)(search + 1);
    } else {
        match = substrcmp;
        pat = (char *)search;
    }
    len = strlen(pat);

    for (pos = H.Pos; (*move)() != NULL;) {
        if ((*match)((char *)H.Lines[H.Pos], pat, len) == 0) {
            return H.Lines[H.Pos];
        }
    }
    H.Pos = pos;
    return NULL;
}

STATIC STATUS h_search(void)
{
    static int Searching;
    CONST char *old_prompt;
    CHAR *(*move)(void);
    CHAR *p;

    if (Searching) {
        return ring_bell();
    }
    Searching = 1;

    clear_line();
    old_prompt = Prompt;
    Prompt = "Search: ";
    TTYputs((CHAR *)Prompt);
    move = Repeat == NO_ARG ? prev_hist : next_hist;
    p = search_hist(editinput(), move);
    clear_line();
    Prompt = old_prompt;
    TTYputs((CHAR *)Prompt);

    Searching = 0;
    return do_insert_hist(p);
}

STATIC STATUS fd_char(void)
{
    int i;

    i = 0;
    do {
        if (Point >= End) {
            break;
        }
        right(CSmove);
    } while (++i < Repeat);
    return CSstay;
}

STATIC void save_yank(int begin, int i)
{
    if (Yanked) {
        DISPOSE(Yanked);
        Yanked = NULL;
    }

    if (i < 1) {
        return;
    }

    if ((Yanked = NEW(CHAR, (SIZE_T)i + 1)) != NULL) {
        COPYFROMTO(Yanked, &Line[begin], i);
        Yanked[i] = '\0';
    }
}

STATIC STATUS delete_string(int count)
{
    int i;
    CHAR *p;

    if (count <= 0 || End == Point) {
        return ring_bell();
    }

    if (count == 1 && Point == End - 1) {
        /* Optimize common case of delete at end of line. */
        End--;
        p = &Line[Point];
        i = 1;
        TTYput(' ');
        if (ISCTL(*p)) {
            i = 2;
            TTYput(' ');
        } else if (rl_meta_chars && ISMETA(*p)) {
            i = 3;
            TTYput(' ');
            TTYput(' ');
        }
        TTYbackn(i);
        *p = '\0';
        return CSmove;
    }
    if (Point + count > End && (count = End - Point) <= 0) {
        return CSstay;
    }

    if (count > 1) {
        save_yank(Point, count);
    }

    for (p = &Line[Point], i = End - (Point + count) + 1; --i >= 0; p++) {
        p[0] = p[count];
    }
    ceol();
    End -= count;
    TTYstring(&Line[Point]);
    return CSmove;
}

STATIC STATUS bk_char(void)
{
    int i;

    i = 0;
    do {
        if (Point == 0) {
            break;
        }
        left(CSmove);
    } while (++i < Repeat);

    return CSstay;
}

STATIC STATUS bk_del_char(void)
{
    int i;

    i = 0;
    do {
        if (Point == 0) {
            break;
        }
        left(CSmove);
    } while (++i < Repeat);

    return delete_string(i);
}

STATIC STATUS redisplay(void)
{
    TTYputs((CHAR *)NEWLINE);
    TTYputs((CHAR *)Prompt);
    TTYstring(Line);
    return CSmove;
}

STATIC STATUS kill_line(void)
{
    int i;

    if (Repeat != NO_ARG) {
        if (Repeat < Point) {
            i = Point;
            Point = Repeat;
            reposition();
            delete_string(i - Point);
        } else if (Repeat > Point) {
            right(CSmove);
            delete_string(Repeat - Point - 1);
        }
        return CSmove;
    }

    save_yank(Point, End - Point);
    Line[Point] = '\0';
    ceol();
    End = Point;
    return CSstay;
}

STATIC STATUS insert_char(int c)
{
    STATUS s;
    CHAR buff[2];
    CHAR *p;
    CHAR *q;
    int i;

    if (Repeat == NO_ARG || Repeat < 2) {
        buff[0] = c;
        buff[1] = '\0';
        return insert_string(buff);
    }

    if ((p = NEW(CHAR, Repeat + 1)) == NULL) {
        return CSstay;
    }
    for (i = Repeat, q = p; --i >= 0;) {
        *q++ = c;
    }
    *q = '\0';
    Repeat = 0;
    s = insert_string(p);
    DISPOSE(p);
    return s;
}

STATIC STATUS meta(void)
{
    unsigned int c;
    KEYMAP *kp;

    if ((c = TTYget()) == EOF)
        return CSeof;
#ifdef ANSI_ARROWS
    /* Also include VT-100 arrows. */
    if (c == '[' || c == 'O') {
        switch (c = TTYget()) {
            default:
                return ring_bell();
            case EOF:
                return CSeof;
            case 'A':
                return h_prev();
            case 'B':
                return h_next();
            case 'C':
                return fd_char();
            case 'D':
                return bk_char();
        }
    }
#endif  /* defined(ANSI_ARROWS) */

    if (isdigit(c)) {
        for (Repeat = c - '0'; (c = TTYget()) != EOF && isdigit(c);) {
            Repeat = Repeat * 10 + c - '0';
        }
        Pushed = 1;
        PushBack = c;
        return CSstay;
    }

    if (isupper(c)) {
        return do_macro(c);
    }
    for (OldPoint = Point, kp = MetaMap; kp->Function; kp++) {
        if (kp->Key == c) {
            return (*kp->Function)();
        }
    }

    return ring_bell();
}

STATIC STATUS emacs(unsigned int c)
{
    STATUS s;
    KEYMAP *kp;

    if (ISMETA(c)) {
        Pushed = 1;
        PushBack = UNMETA(c);
        return meta();
    }
    for (kp = Map; kp->Function; kp++) {
        if (kp->Key == c) {
            break;
        }
    }
    s = kp->Function ? (*kp->Function)() : insert_char((int)c);
    if (!Pushed) {
        /* No pushback means no repeat count; hacky, but true. */
        Repeat = NO_ARG;
    }
    return s;
}

STATIC STATUS TTYspecial(unsigned int c)
{
    if (ISMETA(c)) {
        return CSdispatch;
    }

    if (c == rl_erase || c == DEL) {
        return bk_del_char();
    }
    if (c == rl_kill) {
        if (Point != 0) {
            Point = 0;
            reposition();
        }
        Repeat = NO_ARG;
        return kill_line();
    }
    if (c == rl_intr || c == rl_quit) {
        Point = End = 0;
        Line[0] = '\0';
        return redisplay();
    }
    if (c == rl_eof && Point == 0 && End == 0) {
        return CSeof;
    }

    return CSdispatch;
}

STATIC CHAR *editinput(void)
{
    unsigned int c;

    Repeat = NO_ARG;
    OldPoint = Point = Mark = End = 0;
    Line[0] = '\0';

    while ((c = TTYget()) != EOF) {
        switch (TTYspecial(c)) {
            case CSdone:
                return Line;
            case CSeof:
                return NULL;
            case CSmove:
                reposition();
                break;
            case CSdispatch:
                switch (emacs(c)) {
                    case CSdone:
                        return Line;
                    case CSeof:
                        return NULL;
                    case CSmove:
                        reposition();
                        break;
                    case CSdispatch:
                    case CSstay:
                        break;
                }
                break;
            case CSstay:
                break;
        }
    }
    return NULL;
}

STATIC void hist_add(CHAR *p)
{
    int i;

    if ((p = (CHAR *)strdup((char *)p)) == NULL) {
        return;
    }
    if (H.Size < HIST_SIZE) {
        H.Lines[H.Size++] = p;
    } else {
        DISPOSE(H.Lines[0]);
        for (i = 0; i < HIST_SIZE - 1; i++) {
            H.Lines[i] = H.Lines[i + 1];
        }
        H.Lines[i] = p;
    }
    H.Pos = H.Size - 1;
}

/*
**  For compatibility with FSF readline.
*/
/* ARGSUSED0 */
void rl_reset_terminal(char *p)
{
}

void rl_initialize(void)
{
}

char *readline(CONST char *prompt)
{
    CHAR *line;

    if (Line == NULL) {
        Length = MEM_INC;
        if ((Line = NEW(CHAR, Length)) == NULL) {
            return NULL;
        }
    }

    TTYinfo();
    rl_ttyset(0);
    hist_add(NIL);
    ScreenSize = SCREEN_INC;
    Screen = NEW(char, ScreenSize);
    Prompt = prompt ? prompt : (char *)NIL;
    TTYputs((CHAR *)Prompt);
    if ((line = editinput()) != NULL) {
        line = (CHAR *)strdup((char *)line);
        TTYputs((CHAR *)NEWLINE);
        TTYflush();
    }
    rl_ttyset(1);
    DISPOSE(Screen);
    DISPOSE(H.Lines[--H.Size]);
    return (char *)line;
}

void add_history(const char *p)
{
    if (p == NULL || *p == '\0') {
        return;
    }

#ifdef UNIQUE_HISTORY
    if (H.Pos && strcmp(p, H.Lines[H.Pos - 1]) == 0) {
        return;
    }
#endif  /* defined(UNIQUE_HISTORY) */

    hist_add((CHAR *)p);
}

STATIC STATUS beg_line(void)
{
    if (Point) {
        Point = 0;
        return CSmove;
    }
    return CSstay;
}

STATIC STATUS del_char(void)
{
    return delete_string(Repeat == NO_ARG ? 1 : Repeat);
}

STATIC STATUS end_line(void)
{
    if (Point != End) {
        Point = End;
        return CSmove;
    }
    return CSstay;
}

/*
**  Move back to the beginning of the current word and return an
**  allocated copy of it.
*/
STATIC CHAR *find_word(void)
{
    static char SEPS[] = "#;&|^$=`'{}()<>\n\t ";
    CHAR *p;
    CHAR *new;
    SIZE_T len;

    for (p = &Line[Point]; p > Line && strchr(SEPS, (char)p[-1]) == NULL; p--) {
        continue;
    }
    len = Point - (p - Line) + 1;
    if ((new = NEW(CHAR, len)) == NULL) {
        return NULL;
    }
    COPYFROMTO(new, p, len);
    new[len - 1] = '\0';
    return new;
}

STATIC STATUS c_complete(void)
{
    CHAR *p;
    CHAR *word;
    int unique;
    STATUS s;

    word = find_word();
    p = (CHAR *)rl_complete((char *)word, &unique);
    if (word) {
        DISPOSE(word);
    }
    if (p && *p) {
        s = insert_string(p);
        if (!unique) {
            ring_bell();
        }
        DISPOSE(p);
        return s;
    }
    return ring_bell();
}

STATIC STATUS c_possible(void)
{
    CHAR **av;
    CHAR *word;
    int ac;

    word = find_word();
    ac = rl_list_possib((char *)word, (char ***)&av);
    if (word) {
        DISPOSE(word);
    }
    if (ac) {
        columns(ac, av);
        while (--ac >= 0) {
            DISPOSE(av[ac]);
        }
        DISPOSE(av);
        return CSmove;
    }
    return ring_bell();
}

STATIC STATUS accept_line(void)
{
    Line[End] = '\0';
    return CSdone;
}

STATIC STATUS transpose(void)
{
    CHAR c;

    if (Point) {
        if (Point == End) {
            left(CSmove);
        }
        c = Line[Point - 1];
        left(CSstay);
        Line[Point - 1] = Line[Point];
        TTYshow(Line[Point - 1]);
        Line[Point++] = c;
        TTYshow(c);
    }
    return CSstay;
}

STATIC STATUS quote(void)
{
    unsigned int c;

    return (c = TTYget()) == EOF ? CSeof : insert_char((int)c);
}

STATIC STATUS wipe(void)
{
    int i;

    if (Mark > End) {
        return ring_bell();
    }

    if (Point > Mark) {
        i = Point;
        Point = Mark;
        Mark = i;
        reposition();
    }

    return delete_string(Mark - Point);
}

STATIC STATUS mk_set(void)
{
    Mark = Point;
    return CSstay;
}

STATIC STATUS exchange(void)
{
    unsigned int c;

    if ((c = TTYget()) != CTL('X')) {
        return c == EOF ? CSeof : ring_bell();
    }

    if ((c = Mark) <= End) {
        Mark = Point;
        Point = c;
        return CSmove;
    }
    return CSstay;
}

STATIC STATUS yank(void)
{
    if (Yanked && *Yanked) {
        return insert_string(Yanked);
    }
    return CSstay;
}

STATIC STATUS copy_region(void)
{
    if (Mark > End) {
        return ring_bell();
    }

    if (Point > Mark) {
        save_yank(Mark, Point - Mark);
    } else {
        save_yank(Point, Mark - Point);
    }

    return CSstay;
}

STATIC STATUS move_to_char(void)
{
    unsigned int c;
    int i;
    CHAR *p;

    if ((c = TTYget()) == EOF) {
        return CSeof;
    }
    for (i = Point + 1, p = &Line[i]; i < End; i++, p++) {
        if (*p == c) {
            Point = i;
            return CSmove;
        }
    }
    return CSstay;
}

STATIC STATUS fd_word(void)
{
    return do_forward(CSmove);
}

STATIC STATUS fd_kill_word(void)
{
    int i;

    do_forward(CSstay);
    if (OldPoint != Point) {
        i = Point - OldPoint;
        Point = OldPoint;
        return delete_string(i);
    }
    return CSstay;
}

STATIC STATUS bk_word(void)
{
    int i;
    CHAR *p;

    i = 0;
    do {
        for (p = &Line[Point]; p > Line && !isalnum(p[-1]); p--) {
            left(CSmove);
        }

        for (; p > Line && p[-1] != ' ' && isalnum(p[-1]); p--) {
            left(CSmove);
        }

        if (Point == 0) {
            break;
        }
    } while (++i < Repeat);

    return CSstay;
}

STATIC STATUS bk_kill_word(void)
{
    (void)bk_word();
    if (OldPoint != Point) {
        return delete_string(OldPoint - Point);
    }
    return CSstay;
}

STATIC int argify(CHAR *line, CHAR ***avp)
{
    CHAR *c;
    CHAR **p;
    CHAR **new;
    int ac;
    int i;

    i = MEM_INC;
    if ((*avp = p = NEW(CHAR*, i)) == NULL) {
         return 0;
    }

    for (c = line; isspace(*c); c++) {
        continue;
    }
    if (*c == '\n' || *c == '\0') {
        return 0;
    }

    for (ac = 0, p[ac++] = c; *c && *c != '\n'; ) {
        if (isspace(*c)) {
            *c++ = '\0';
            if (*c && *c != '\n') {
                if (ac + 1 == i) {
                    new = NEW(CHAR*, i + MEM_INC);
                    if (new == NULL) {
                        p[ac] = NULL;
                        return ac;
                    }
                    COPYFROMTO(new, p, i * sizeof (char **));
                    i += MEM_INC;
                    DISPOSE(p);
                    *avp = p = new;
                }
                p[ac++] = c;
            }
        } else {
            c++;
        }
    }
    *c = '\0';
    p[ac] = NULL;
    return ac;
}

STATIC STATUS last_argument(void)
{
    CHAR **av;
    CHAR *p;
    STATUS s;
    int ac;

    if (H.Size == 1 || (p = H.Lines[H.Size - 2]) == NULL) {
        return ring_bell();
    }

    if ((p = (CHAR *)strdup((char *)p)) == NULL) {
        return CSstay;
    }
    ac = argify(p, &av);

    if (Repeat != NO_ARG) {
        s = Repeat < ac ? insert_string(av[Repeat]) : ring_bell();
    } else {
        s = ac ? insert_string(av[ac - 1]) : CSstay;
    }

    if (ac) {
        DISPOSE(av);
    }
    DISPOSE(p);
    return s;
}
