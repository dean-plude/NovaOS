/*
 * um_console.c — consoles: a program's standard input/output ↔ a Terminal
 *
 * Output is a ring with one reader (the desktop thread) and writers taking
 * turns (out_lock: programs' threads, without the big kernel lock); a
 * program that writes faster than the Terminal drains it waits.
 *
 * Input is a queue of Windows input records (INPUT_RECORD: key events,
 * and whatever programs add with WriteConsoleInput).  The Terminal adds
 * them: a whole line at a time while the program reads in line mode (the
 * Terminal edits the line), each key as it is pressed in raw mode.  Reads
 * in line mode wait for Enter; raw reads take whatever characters are
 * there; ReadConsoleInput takes the records themselves.  The console's
 * object (UO_CONSOLE) is signaled while records are queued, so programs
 * can wait on their input handle.
 *
 * Each screen buffer keeps its cells, which libvterm (the Terminal's VT
 * emulator) edits from what programs write, so a full-screen program
 * leaves the same cells whether a Terminal or a pseudo console's host
 * draws its output.
 */

#include "um_internal.h"
#include "../ke/printf.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "vterm.h"

#define OUT_SIZE  (64 * 1024)
#define IN_RECS   16384                         /* two lines of 8,191 characters (the Terminal's longest) */
#define CON_SCREENS 8
#define CON_MAX_CELLS (1024u * 1024u)
#define CON_NEWLINE_NO_RETURN 0x0008     /* DISABLE_NEWLINE_AUTO_RETURN */
#define CELL_LEADING   0x0100            /* COMMON_LVB_LEADING_BYTE: the left half of a wide character */
#define CELL_TRAILING  0x0200            /* COMMON_LVB_TRAILING_BYTE: its right half */
#define CELL_REVERSE   0x4000            /* COMMON_LVB_REVERSE_VIDEO */
#define CELL_UNDERLINE 0x8000            /* COMMON_LVB_UNDERSCORE */

/* A screen buffer's cells.  libvterm's parser and state (the emulator the
 * Terminal uses) interpret what programs write and edit the cells through
 * the callbacks below, so the cells match what the Terminal or a pseudo
 * console's host draws from the same output. */
typedef struct {
    UmConsoleCell *cells;
    UmConsoleCell *main_cells;     /* the main screen's cells while the alternate screen is up */
    UINT16 cols, rows, cursor_x, cursor_y, attr;
    UINT32 refs;
    VTerm *vt;
    char pend[4];                  /* an unfinished UTF-8 character (UmVtermWrite) */
    UINT8 pend_n;
    struct UmConsole *con;
    bool sized;                    /* SetConsoleScreenBufferSize chose its size */
    bool used;
} UmConsoleScreen;

struct UmConsole {
    volatile int    refs;
    char            out[OUT_SIZE];
    volatile UINT32 out_head, out_tail;      /* head: next write, tail: next read */
    UmLock          out_lock;                /* one writer at a time */
    KSpinLock       in_lock;                 /* the input queue and pend[] */
    UmConInput      in[IN_RECS];
    UINT32          in_head, in_tail;
    volatile bool   in_eof;
    volatile bool   waiting;                 /* a program is blocked reading */
    char            pend[4];                 /* UTF-8 bytes of a character a read could not fit */
    int             pend_n;
    volatile UINT32 in_mode, out_mode;       /* SetConsoleMode */
    volatile UINT16 cols, rows;              /* the Terminal's size in cells */
    volatile UINT32 cursor_style;          /* low byte: size 1..100, bit 8: visible */
    UmConsoleScreen screen[CON_SCREENS];
    UINT8 active_screen;
    UmObject       *ob;                      /* UO_CONSOLE: signaled while input is queued */
    UmObject       *out_ob;                  /* a pseudo console's object (not referenced): signaled
                                                while output waits to be read or the console has ended */
    volatile bool   out_ended;               /* ClosePseudoConsole: its reader stops */
};

static int console_write_locked(UmConsole *c, const char *data, int len);

static UmConsoleScreen *console_screen(UmConsole *c, UINT32 id)
{
    return c && id < CON_SCREENS && c->screen[id].used ? &c->screen[id] : NULL;
}

static void screen_free(UmConsoleScreen *s)
{
    if (s->vt) vterm_free(s->vt);
    kfree(s->cells);
    kfree(s->main_cells);
    memset(s, 0, sizeof(*s));
}

static void screen_release_locked(UmConsole *c, UINT32 id)
{
    if (id && id < CON_SCREENS && id != c->active_screen &&
        c->screen[id].used && !c->screen[id].refs)
        screen_free(&c->screen[id]);
}

static void screen_blank(UmConsoleScreen *s, UmConsoleCell cell)
{
    UINT32 n = (UINT32)s->cols * s->rows;
    for (UINT32 i = 0; i < n; i++) s->cells[i] = cell;
}

static int screen_sgr(char *seq, size_t cap, UINT16 attr)
{
    static const UINT8 map[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    return ksnprintf(seq, cap, "\x1b[0;%u;%u%s%sm",
                     30 + map[attr & 7] + ((attr & 8) ? 60 : 0),
                     40 + map[(attr >> 4) & 7] + ((attr & 0x80) ? 60 : 0),
                     (attr & CELL_UNDERLINE) ? ";4" : "", (attr & CELL_REVERSE) ? ";7" : "");
}

/* A Windows colour (0-15) for a libvterm colour: the 16 ANSI colours map
 * across, others to the nearest of them */
static UINT16 vt_color_attr(VTermState *st, VTermColor col, bool fg)
{
    static const UINT8 map[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    if (fg ? VTERM_COLOR_IS_DEFAULT_FG(&col) : VTERM_COLOR_IS_DEFAULT_BG(&col)) return fg ? 7 : 0;
    int idx = VTERM_COLOR_IS_INDEXED(&col) ? col.indexed.idx : 16;
    if (idx >= 16) {
        vterm_state_convert_color_to_rgb(st, &col);
        int best = 1 << 30;
        for (int i = 0; i < 16; i++) {
            VTermColor p;
            vterm_state_get_palette_color(st, i, &p);
            int dr = p.rgb.red - col.rgb.red, dg = p.rgb.green - col.rgb.green, db = p.rgb.blue - col.rgb.blue;
            int d = dr * dr + dg * dg + db * db;
            if (d < best) { best = d; idx = i; }
        }
    }
    return (UINT16)(map[idx & 7] | (idx & 8));
}

/* The screen's attributes follow libvterm's pen */
static int vt_setpenattr(VTermAttr which, VTermValue *val, void *user)
{
    (void)which; (void)val;
    UmConsoleScreen *s = user;
    VTermState *st = vterm_obtain_state(s->vt);
    VTermValue v;
    vterm_state_get_penattr(st, VTERM_ATTR_FOREGROUND, &v);
    UINT16 attr = vt_color_attr(st, v.color, true);
    vterm_state_get_penattr(st, VTERM_ATTR_BACKGROUND, &v);
    attr |= (UINT16)(vt_color_attr(st, v.color, false) << 4);
    vterm_state_get_penattr(st, VTERM_ATTR_BOLD, &v);
    if (v.boolean) attr |= 8;                           /* SGR 1: FOREGROUND_INTENSITY, as in conhost */
    vterm_state_get_penattr(st, VTERM_ATTR_REVERSE, &v);
    if (v.boolean) attr |= CELL_REVERSE;
    vterm_state_get_penattr(st, VTERM_ATTR_UNDERLINE, &v);
    if (v.number) attr |= CELL_UNDERLINE;
    s->attr = attr;
    return 1;
}

static int vt_putglyph(VTermGlyphInfo *info, VTermPos pos, void *user)
{
    UmConsoleScreen *s = user;
    if (pos.row < 0 || pos.row >= s->rows || pos.col < 0 || pos.col >= s->cols) return 1;
    UINT32 ch = info->chars[0];
    UINT16 c16 = ch && ch < 0x10000 && (ch < 0xD800 || ch >= 0xE000) ? (UINT16)ch : '?';
    UmConsoleCell *cell = &s->cells[pos.row * s->cols + pos.col];
    if (info->width == 2 && pos.col + 1 < s->cols) {     /* as conhost: the character in both cells */
        cell[0] = (UmConsoleCell){ c16, (UINT16)(s->attr | CELL_LEADING) };
        cell[1] = (UmConsoleCell){ c16, (UINT16)(s->attr | CELL_TRAILING) };
    } else cell[0] = (UmConsoleCell){ c16, s->attr };
    return 1;
}

static int vt_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    (void)oldpos; (void)visible;
    UmConsoleScreen *s = user;
    s->cursor_x = (UINT16)pos.col;
    s->cursor_y = (UINT16)pos.row;
    return 1;
}

/* (scrolling: libvterm moves the rows and erases what scrolled in) */
static int vt_moverect(VTermRect dest, VTermRect src, void *user)
{
    UmConsoleScreen *s = user;
    int h = src.end_row - src.start_row, w = src.end_col - src.start_col;
    if (h <= 0 || w <= 0) return 1;
    bool up = dest.start_row <= src.start_row;
    for (int i = 0; i < h; i++) {
        int k = up ? i : h - 1 - i;
        memmove(&s->cells[(dest.start_row + k) * s->cols + dest.start_col],
                &s->cells[(src.start_row + k) * s->cols + src.start_col], (size_t)w * sizeof(UmConsoleCell));
    }
    return 1;
}

static int vt_erase(VTermRect rect, int selective, void *user)
{
    (void)selective;
    UmConsoleScreen *s = user;
    UmConsoleCell blank = { ' ', (UINT16)(s->attr & 0xFF) };
    for (int y = rect.start_row; y < rect.end_row; y++)
        for (int x = rect.start_col; x < rect.end_col; x++) s->cells[y * s->cols + x] = blank;
    return 1;
}

static int vt_settermprop(VTermProp prop, VTermValue *val, void *user)
{
    UmConsoleScreen *s = user;
    size_t bytes = (size_t)s->cols * s->rows * sizeof(UmConsoleCell);
    if (prop == VTERM_PROP_ALTSCREEN) {                 /* ?1049h: libvterm erases the alternate screen */
        if (val->boolean && !s->main_cells) {
            s->main_cells = kmalloc(bytes);
            if (!s->main_cells) return 0;
            memcpy(s->main_cells, s->cells, bytes);
        } else if (!val->boolean && s->main_cells) {
            memcpy(s->cells, s->main_cells, bytes);
            kfree(s->main_cells);
            s->main_cells = NULL;
        }
    } else if (prop == VTERM_PROP_CURSORVISIBLE && s == &s->con->screen[s->con->active_screen]) {
        UINT32 style = s->con->cursor_style;
        __atomic_store_n(&s->con->cursor_style, val->boolean ? style | 0x100 : style & ~0x100u, __ATOMIC_RELEASE);
    }
    return 1;
}

static const VTermStateCallbacks g_screen_cbs = {
    .putglyph = vt_putglyph, .movecursor = vt_movecursor, .moverect = vt_moverect,
    .erase = vt_erase, .setpenattr = vt_setpenattr, .settermprop = vt_settermprop,
};

/* (answers to queries such as DSR come from the Terminal's own libvterm) */
static void vt_discard(const char *s, size_t len, void *user) { (void)s; (void)len; (void)user; }

/* A new screen buffer of @cols by @rows blank cells in attributes 7 */
static bool screen_init(UmConsole *c, UmConsoleScreen *s, UINT32 cols, UINT32 rows)
{
    memset(s, 0, sizeof(*s));
    s->cells = kmalloc((size_t)cols * rows * sizeof(*s->cells));
    s->vt = s->cells ? vterm_new((int)rows, (int)cols) : NULL;
    if (!s->vt) { kfree(s->cells); s->cells = NULL; return false; }
    s->con = c;
    s->cols = (UINT16)cols;
    s->rows = (UINT16)rows;
    s->attr = 7;
    s->used = true;
    screen_blank(s, (UmConsoleCell){ ' ', 7 });
    vterm_set_utf8(s->vt, 1);
    vterm_output_set_callback(s->vt, vt_discard, NULL);
    VTermState *st = vterm_obtain_state(s->vt);
    vterm_state_set_callbacks(st, &g_screen_cbs, s);
    vterm_state_reset(st, 1);
    return true;
}

static UmConsoleCell *cells_resized(const UmConsoleCell *old, UINT32 old_cols, UINT32 old_rows,
                                    UINT32 cols, UINT32 rows, UINT16 attr)
{
    UmConsoleCell *cells = kmalloc((size_t)cols * rows * sizeof(*cells));
    if (!cells) return NULL;
    for (UINT32 i = 0; i < cols * rows; i++) cells[i] = (UmConsoleCell){ ' ', attr };
    UINT32 copy_cols = cols < old_cols ? cols : old_cols;
    UINT32 copy_rows = rows < old_rows ? rows : old_rows;
    for (UINT32 y = 0; y < copy_rows; y++)
        memcpy(cells + y * cols, old + y * old_cols, copy_cols * sizeof(*cells));
    return cells;
}

static bool screen_resize(UmConsoleScreen *s, UINT32 cols, UINT32 rows)
{
    if (!cols || !rows || cols > 32767 || rows > 32767 || cols * (UINT64)rows > CON_MAX_CELLS)
        return false;
    UINT16 attr = (UINT16)(s->attr & 0xFF);
    UmConsoleCell *cells = cells_resized(s->cells, s->cols, s->rows, cols, rows, attr);
    UmConsoleCell *main_cells = s->main_cells ? cells_resized(s->main_cells, s->cols, s->rows, cols, rows, attr) : NULL;
    if (!cells || (s->main_cells && !main_cells)) { kfree(cells); kfree(main_cells); return false; }
    kfree(s->cells);
    kfree(s->main_cells);
    s->cells = cells;
    s->main_cells = main_cells;
    s->cols = (UINT16)cols;
    s->rows = (UINT16)rows;
    vterm_set_size(s->vt, (int)rows, (int)cols);
    VTermPos pos;
    vterm_state_get_cursorpos(vterm_obtain_state(s->vt), &pos);
    s->cursor_x = (UINT16)pos.col;
    s->cursor_y = (UINT16)pos.row;
    return true;
}

static int utf8_len(UINT8 b) { return b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1; }

void UmVtermWrite(struct VTerm *vt, char *pend, UINT8 *pend_n, const char *data, size_t len)
{
    if (*pend_n) {
        int need = utf8_len((UINT8)pend[0]);
        while (len && *pend_n < need && ((UINT8)*data & 0xC0) == 0x80) {
            pend[(*pend_n)++] = *data++;
            len--;
        }
        if (*pend_n < need && !len) return;            /* still unfinished */
        vterm_input_write(vt, pend, *pend_n);
        *pend_n = 0;
    }
    size_t keep = 0;
    for (size_t k = 1; k <= 3 && k <= len; k++) {
        UINT8 b = (UINT8)data[len - k];
        if ((b & 0xC0) == 0x80) continue;
        if (b >= 0xC0 && utf8_len(b) > (int)k) keep = k;
        break;
    }
    if (len > keep) vterm_input_write(vt, data, len - keep);
    memcpy(pend, data + len - keep, keep);
    *pend_n = (UINT8)keep;
}

/* Caller holds out_lock: @data through @s's emulator, and on to the
 * Terminal or a pseudo console's host when @s is the active screen.  As in
 * a Windows console, a line feed also returns the carriage unless the
 * program set DISABLE_NEWLINE_AUTO_RETURN; a pseudo console's host gets the
 * carriage return written out, as from conhost (the Terminal adds its own). */
static int screen_feed(UmConsole *c, UmConsoleScreen *s, const char *data, int len)
{
    bool active = s == &c->screen[c->active_screen];
    bool cr = !(c->out_mode & CON_NEWLINE_NO_RETURN);
    bool pseudo = __atomic_load_n(&c->out_ob, __ATOMIC_ACQUIRE) != NULL;
    int from = 0, done = 0;
    for (int i = 0; i <= len; i++) {
        if (i < len && (data[i] != '\n' || !cr)) continue;
        if (i > from) {
            UmVtermWrite(s->vt, s->pend, &s->pend_n, data + from, (size_t)(i - from));
            int n = active ? console_write_locked(c, data + from, i - from) : i - from;
            done += n;
            if (n < i - from) return done;
        }
        if (i < len) {
            UmVtermWrite(s->vt, s->pend, &s->pend_n, "\r\n", 2);
            if (active && console_write_locked(c, pseudo ? "\r\n" : "\n", pseudo ? 2 : 1) != (pseudo ? 2 : 1))
                return done;
            done++;
        }
        from = i + 1;
    }
    return done;
}

/* A cell as the Terminal or a pseudo console's host draws it: UTF-8, the
 * right half of a wide character left out */
static int cell_utf8(const UmConsoleCell *row, UINT32 x, UINT32 cols, char *out)
{
    const UmConsoleCell *cell = &row[x];
    UINT16 ch = cell->ch;
    if (cell->attr & CELL_TRAILING) {
        if (x && (row[x - 1].attr & CELL_LEADING) && row[x - 1].ch == ch) return 0;
        ch = ' ';
    } else if ((cell->attr & CELL_LEADING) &&
               (x + 1 >= cols || !(cell[1].attr & CELL_TRAILING) || cell[1].ch != ch)) ch = ' ';
    if (ch < 0x20 || ch == 0x7F) ch = ' ';
    if (ch < 0x80) { out[0] = (char)ch; return 1; }
    if (ch < 0x800) {
        out[0] = (char)(0xC0 | (ch >> 6));
        out[1] = (char)(0x80 | (ch & 0x3F));
        return 2;
    }
    if (ch >= 0xD800 && ch < 0xE000) { out[0] = '?'; return 1; }
    out[0] = (char)(0xE0 | (ch >> 12));
    out[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
    out[2] = (char)(0x80 | (ch & 0x3F));
    return 3;
}

/* Caller holds out_lock: rows @top..@bottom of @s drawn again on the
 * Terminal (@clear: the whole screen, cleared first), then the current
 * attributes and cursor put back */
static void screen_render_rows(UmConsole *c, UmConsoleScreen *s, UINT32 top, UINT32 bottom, bool clear)
{
    char seq[64];
    int n;
    if (clear) {
        n = ksnprintf(seq, sizeof(seq), "\x1b[2J\x1b[H");
        console_write_locked(c, seq, n);
    }
    UINT16 old_attr = 0xFFFF;
    for (UINT32 y = top; y <= bottom && y < s->rows; y++) {
        n = ksnprintf(seq, sizeof(seq), "\x1b[%u;1H", y + 1);
        console_write_locked(c, seq, n);
        const UmConsoleCell *row = &s->cells[y * s->cols];
        for (UINT32 x = 0; x < s->cols; x++) {
            UINT16 attr = row[x].attr & (0xFF | CELL_REVERSE | CELL_UNDERLINE);
            if (attr != old_attr) {
                n = screen_sgr(seq, sizeof(seq), attr);
                console_write_locked(c, seq, n);
                old_attr = attr;
            }
            char ch[3];
            int count = cell_utf8(row, x, s->cols, ch);
            if (count) console_write_locked(c, ch, count);
        }
    }
    /* (through the emulator too: a cursor waiting to wrap at the end of a
     * line stops waiting on both sides) */
    UINT16 attr = s->attr;
    n = screen_sgr(seq, sizeof(seq), attr);
    n += ksnprintf(seq + n, sizeof(seq) - (size_t)n, "\x1b[%u;%uH", s->cursor_y + 1, s->cursor_x + 1);
    screen_feed(c, s, seq, n);
    s->attr = attr;
}

static void screen_render(UmConsole *c, UmConsoleScreen *s)
{
    screen_render_rows(c, s, 0, s->rows ? s->rows - 1u : 0, true);
}

/* Caller holds out_lock: the first screen buffer keeps the Terminal's size
 * until a program sets its size (SetConsoleScreenBufferSize) */
static void screen0_follow(UmConsole *c)
{
    UmConsoleScreen *s = &c->screen[0];
    if (s->sized || !c->cols || !c->rows || (s->cols == c->cols && s->rows == c->rows)) return;
    screen_resize(s, c->cols, c->rows);
}

UmConsole *UmConsoleNew(void)
{
    UmConsole *c = kzalloc(sizeof(*c));
    if (!c) return NULL;
    c->ob = kzalloc(sizeof(UmObject));
    if (!c->ob) { kfree(c); return NULL; }
    c->ob->type = UO_CONSOLE;
    c->ob->refs = 1;
    c->ob->ptr  = c;
    c->refs = 1;
    c->in_lock  = (KSpinLock)KSPINLOCK_INIT;
    c->in_mode  = CON_IN_DEFAULT;
    c->out_mode = CON_OUT_DEFAULT;
    c->cols = 80;
    c->rows = 25;
    c->cursor_style = 25 | 0x100;
    if (!screen_init(c, &c->screen[0], 80, 25)) {
        um_ob_unref(c->ob);
        kfree(c);
        return NULL;
    }
    return c;
}

UmConsole *um_console_ref(UmConsole *c)
{
    if (c) __atomic_add_fetch(&c->refs, 1, __ATOMIC_ACQ_REL);
    return c;
}

void UmConsoleRelease(UmConsole *c)
{
    if (c && __atomic_sub_fetch(&c->refs, 1, __ATOMIC_ACQ_REL) == 0) {
        IrqState s = ob_lock();
        c->ob->ptr = NULL;                      /* waiters may still hold it */
        ob_unlock(s);
        for (int i = 0; i < CON_SCREENS; i++) screen_free(&c->screen[i]);
        um_ob_unref(c->ob);
        kfree(c);
    }
}

UmObject *um_console_object(UmConsole *c)
{
    return c ? um_ob_ref(c->ob) : NULL;
}

/* The unfinished line of a detached process, to the kernel log */
void um_console_flush_log(UmProcess *p)
{
    if (!p || !p->log_n) return;
    p->log_line[p->log_n] = 0;
    kprintf("[UM] %s (PID %u, detached): %s\n", p->name, p->pid, p->log_line);
    p->log_n = 0;
}

/* The pseudo console object's state follows the output ring */
static void out_changed(UmConsole *c)
{
    UmObject *o = __atomic_load_n(&c->out_ob, __ATOMIC_ACQUIRE);
    if (!o) return;
    IrqState s = ob_lock();
    bool sig = __atomic_load_n(&c->out_head, __ATOMIC_ACQUIRE) != c->out_tail || c->out_ended;
    if (sig && !o->signaled) um_ob_wake(o);
    o->signaled = sig;
    ob_unlock(s);
}

/* Caller holds out_lock; a control sequence stays together with ordinary output. */
static int console_write_locked(UmConsole *c, const char *data, int len)
{
    UmProcess *p = UmCurrent();
    int done = 0;
    while (done < len) {
        UINT32 used = c->out_head - c->out_tail;
        if (used == OUT_SIZE) {                         /* full: let the Terminal drain */
            if (p && um_stopping()) break;
            out_changed(c);                             /* (a pseudo console's reader may be waiting) */
            sched_wait();
            continue;
        }
        UINT32 n = OUT_SIZE - used;
        if (n > (UINT32)(len - done)) n = (UINT32)(len - done);
        for (UINT32 i = 0; i < n; i++) c->out[(c->out_head + i) % OUT_SIZE] = data[done + i];
        __atomic_store_n(&c->out_head, c->out_head + n, __ATOMIC_RELEASE);
        done += (int)n;
    }
    out_changed(c);
    return done;
}

/* @o (NULL: none) is signaled while output waits or the console has ended */
void um_console_set_output_object(UmConsole *c, UmObject *o)
{
    um_lock(&c->out_lock);
    __atomic_store_n(&c->out_ob, o, __ATOMIC_RELEASE);
    um_unlock(&c->out_lock);
    out_changed(c);
}

void um_console_end_output(UmConsole *c)
{
    c->out_ended = true;
    out_changed(c);
}

/* Program → Terminal */
int um_console_write(UmConsole *c, const char *data, int len)
{
    if (!c) {
        /* No console (a detached process handed its creator's console
         * handles, as Firefox's sandbox does for its child processes):
         * the kernel log, a line at a time.  An unfinished line waits in
         * the process for the rest (VLC writes its log a character at a
         * time) and goes out when it ends or the process does. */
        UmProcess *me = UmCurrent();
        if (!me) return len;
        for (int i = 0; i < len; i++) {
            char ch = data[i];
            if (ch != '\n' && ch != '\r' && me->log_n < (int)sizeof(me->log_line) - 1) me->log_line[me->log_n++] = ch;
            if ((ch == '\n' || me->log_n == (int)sizeof(me->log_line) - 1) && me->log_n) um_console_flush_log(me);
        }
        return len;
    }
    return um_console_write_screen(c, 0, data, len);
}

int um_console_write_screen(UmConsole *c, UINT32 id, const char *data, int len)
{
    if (!c) return um_console_write(NULL, data, len);
    um_lock(&c->out_lock);
    screen0_follow(c);
    UmConsoleScreen *s = console_screen(c, id);
    if (!s) { um_unlock(&c->out_lock); return 0; }
    int done = screen_feed(c, s, data, len);
    um_unlock(&c->out_lock);
    return done;
}


int UmConsoleRead(UmConsole *c, char *buf, int cap)
{
    UINT32 head = __atomic_load_n(&c->out_head, __ATOMIC_ACQUIRE);
    int n = 0;
    while (c->out_tail + (UINT32)n != head && n < cap) {
        buf[n] = c->out[(c->out_tail + (UINT32)n) % OUT_SIZE];
        n++;
    }
    __atomic_store_n(&c->out_tail, c->out_tail + (UINT32)n, __ATOMIC_RELEASE);
    out_changed(c);
    return n;
}

/* -----------------------------------------------------------------------
 * Input queue
 * ----------------------------------------------------------------------- */
/* Under in_lock: the object's state follows the queue */
static void in_changed(UmConsole *c)
{
    bool sig = c->in_head != c->in_tail || c->in_eof;
    IrqState s = ob_lock();
    if (sig && !c->ob->signaled) um_ob_wake_boost(c->ob, BOOST_KEYBOARD);   /* (console input, as from the keyboard) */
    c->ob->signaled = sig;
    ob_unlock(s);
}

static bool is_key(const UmConInput *r)  { return r->type == CON_KEY_EVENT; }
static bool key_down(const UmConInput *r) { return r->key.down != 0; }

int UmConsolePushInput(UmConsole *c, const UmConInput *recs, int n)
{
    if (!c) return 0;
    IrqState s = spin_lock_irqsave(&c->in_lock);
    int done = 0;
    for (; done < n && c->in_head - c->in_tail < IN_RECS; done++)
        c->in[c->in_head++ % IN_RECS] = recs[done];
    in_changed(c);
    spin_unlock_irqrestore(&c->in_lock, s);
    return done;
}

void UmConsoleKey(UmConsole *c, UINT16 vk, UINT16 scan, UINT16 ch, UINT32 ctrl, bool down)
{
    UmConInput r;
    memset(&r, 0, sizeof(r));
    r.type = CON_KEY_EVENT;
    r.key.down   = down;
    r.key.repeat = 1;
    r.key.vk     = vk;
    r.key.scan   = scan;
    r.key.ch     = ch;
    r.key.ctrl   = ctrl;
    UmConsolePushInput(c, &r, 1);
}

/* Terminal → program: text typed (a line in line mode).  "\r\n" or "\n"
 * is Enter. */
void UmConsoleWrite(UmConsole *c, const char *data, int len)
{
    for (int i = 0; i < len; i++) {
        UINT32 ch = (UINT8)data[i];
        if (ch == '\r' && i + 1 < len && data[i + 1] == '\n') continue;
        if (ch == '\n') ch = '\r';
        else if (ch >= 0xC0 && i + 1 < len) {           /* UTF-8 */
            int k = ch >= 0xF0 ? 3 : ch >= 0xE0 ? 2 : 1;
            ch &= 0x3F >> k;
            for (int j = 0; j < k && i + 1 < len; j++) ch = ch << 6 | ((UINT8)data[++i] & 0x3F);
            if (ch >= 0x10000) ch = '?';
        }
        UINT16 vk = ch == '\r' ? 0x0D : ch == 8 ? 0x08 : ch == 9 ? 0x09 : ch == 0x1B ? 0x1B :
                    (ch >= 'a' && ch <= 'z') ? (UINT16)(ch - 32) : (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' ? (UINT16)ch : 0;
        UmConsoleKey(c, vk, 0, (UINT16)ch, 0, true);
    }
}

void UmConsoleEof(UmConsole *c)
{
    IrqState s = spin_lock_irqsave(&c->in_lock);
    c->in_eof = true;
    in_changed(c);
    spin_unlock_irqrestore(&c->in_lock, s);
}

bool   UmConsoleWantsInput(UmConsole *c)  { return c->waiting; }
UINT32 UmConsoleInputMode(UmConsole *c)   { return c->in_mode; }
UINT32 UmConsoleOutputMode(UmConsole *c)  { return c->out_mode; }

void UmConsoleSetSize(UmConsole *c, int cols, int rows)
{
    if (cols == c->cols && rows == c->rows) return;
    c->cols = (UINT16)cols;
    c->rows = (UINT16)rows;
    if (c->in_mode & CON_ENABLE_WINDOW_INPUT) {         /* WINDOW_BUFFER_SIZE_EVENT */
        UmConInput r;
        memset(&r, 0, sizeof(r));
        r.type = CON_WINDOW_EVENT;
        r.raw[0] = (UINT32)cols | (UINT32)rows << 16;
        UmConsolePushInput(c, &r, 1);
    }
}

/* Under in_lock: append character @ch to @buf as UTF-8, keeping what does
 * not fit for the next read */
static int put_utf8(UmConsole *c, char *buf, int n, int cap, UINT32 ch)
{
    char t[4];
    int k = 0;
    if (ch < 0x80) t[k++] = (char)ch;
    else if (ch < 0x800) { t[k++] = (char)(0xC0 | ch >> 6); t[k++] = (char)(0x80 | (ch & 0x3F)); }
    else { t[k++] = (char)(0xE0 | ch >> 12); t[k++] = (char)(0x80 | ((ch >> 6) & 0x3F)); t[k++] = (char)(0x80 | (ch & 0x3F)); }
    for (int i = 0; i < k; i++) {
        if (n < cap) buf[n++] = t[i];
        else c->pend[c->pend_n++] = t[i];
    }
    return n;
}

/* Under in_lock: whether a line (up to Enter) is queued */
static bool line_ready(UmConsole *c)
{
    for (UINT32 i = c->in_tail; i != c->in_head; i++) {
        const UmConInput *r = &c->in[i % IN_RECS];
        if (is_key(r) && key_down(r) && r->key.ch == '\r') return true;
    }
    return false;
}

/* Under in_lock: whether a character is queued */
static bool char_ready(UmConsole *c)
{
    for (UINT32 i = c->in_tail; i != c->in_head; i++) {
        const UmConInput *r = &c->in[i % IN_RECS];
        if (is_key(r) && key_down(r) && r->key.ch) return true;
    }
    return false;
}

int um_console_read(UmConsole *c, char *buf, int cap, UmProcess *p)
{
    /* Line mode: wait for a complete line (or EOF); return at most that
     * line, with Enter as "\r\n".  Raw mode: wait for a character; return
     * the characters queued.  Records that are not characters (key
     * releases, arrows...) are dropped on the way, as on Windows. */
    if (!c) return 0;                                   /* no console: end of file */
    if (cap <= 0) return 0;
    for (;;) {
        IrqState s = spin_lock_irqsave(&c->in_lock);
        bool line = c->in_mode & CON_ENABLE_LINE_INPUT;
        int n = 0;
        while (c->pend_n && n < cap) {                  /* the rest of a character */
            buf[n++] = c->pend[0];
            memmove(c->pend, c->pend + 1, (size_t)--c->pend_n);
        }
        if (n || (line ? line_ready(c) : char_ready(c))) {
            while (c->in_tail != c->in_head && n < cap && !c->pend_n) {
                UmConInput *r = &c->in[c->in_tail % IN_RECS];
                c->in_tail++;
                if (!is_key(r) || !key_down(r) || !r->key.ch) continue;
                UINT32 ch = r->key.ch;
                if (ch >= 0xD800 && ch < 0xDC00) ch = '?';      /* (no surrogate pairs from keys) */
                if (ch == '\r' && line) {
                    n = put_utf8(c, buf, n, cap, '\r');
                    n = put_utf8(c, buf, n, cap, '\n');
                    break;
                }
                n = put_utf8(c, buf, n, cap, ch);
            }
            c->waiting = false;
            in_changed(c);
            spin_unlock_irqrestore(&c->in_lock, s);
            return n;
        }
        if (c->in_eof) {
            c->in_eof = false;
            c->waiting = false;
            in_changed(c);
            spin_unlock_irqrestore(&c->in_lock, s);
            return 0;
        }
        spin_unlock_irqrestore(&c->in_lock, s);
        if (p && um_stopping()) { c->waiting = false; return -1; }
        c->waiting = true;
        sched_wait();
    }
}

/* ReadConsoleInput / PeekConsoleInput: up to @max records into @out;
 * @wait: block until there is one */
int um_console_records(UmConsole *c, UmConInput *out, int max, bool remove, bool wait)
{
    if (!c) return 0;
    for (;;) {
        IrqState s = spin_lock_irqsave(&c->in_lock);
        int n = 0;
        for (UINT32 i = c->in_tail; i != c->in_head && n < max; i++) out[n++] = c->in[i % IN_RECS];
        if (n || !wait) {
            if (remove) { c->in_tail += (UINT32)n; in_changed(c); }
            spin_unlock_irqrestore(&c->in_lock, s);
            return n;
        }
        spin_unlock_irqrestore(&c->in_lock, s);
        if (um_stopping()) return -1;
        c->waiting = true;
        sched_wait();
    }
}

int um_console_count(UmConsole *c)
{
    if (!c) return 0;
    IrqState s = spin_lock_irqsave(&c->in_lock);
    int n = (int)(c->in_head - c->in_tail);
    spin_unlock_irqrestore(&c->in_lock, s);
    return n;
}

void um_console_flush(UmConsole *c)
{
    if (!c) return;
    IrqState s = spin_lock_irqsave(&c->in_lock);
    c->in_tail = c->in_head;
    c->pend_n = 0;
    in_changed(c);
    spin_unlock_irqrestore(&c->in_lock, s);
}

void um_console_set_mode(UmConsole *c, bool input, UINT32 mode)
{
    if (!c) return;
    if (input) c->in_mode = mode;
    else c->out_mode = mode;
}

void um_console_size(UmConsole *c, int *cols, int *rows)
{
    *cols = c ? c->cols : 80;
    *rows = c ? c->rows : 25;
}

/* The classic cursor APIs use the Terminal's existing VT screen. No user
 * output-mode bit is needed: this sequence was generated by the console. */
UINT32 um_console_cursor(UmConsole *c, UINT32 op, UINT32 value, UINT32 *result)
{
    if (!c) return 0xC0000008u;
    if (op == 10) { *result = UmConsoleCursorStyle(c); return 0; }
    UmConsoleScreen *screen = console_screen(c, 0);
    if (!screen) return 0xC0000008u;
    char seq[32];
    int n;
    if (op == 9) {
        int x = (INT16)(value & 0xFFFF), y = (INT16)(value >> 16);
        int cols = screen->sized ? screen->cols : c->cols, rows = screen->sized ? screen->rows : c->rows;
        if (x < 0 || y < 0 || x >= cols || y >= rows) return 0xC000000Du;
        n = ksnprintf(seq, sizeof(seq), "\x1b[%d;%dH", y + 1, x + 1);
    } else if (op == 11) {
        if ((value & 0xFF) < 1 || (value & 0xFF) > 100 || (value & ~0x1FFu)) return 0xC000000Du;
        n = ksnprintf(seq, sizeof(seq), "\x1b[?25%c", (value & 0x100) ? 'h' : 'l');
    } else return 0xC000000Du;
    um_lock(&c->out_lock);
    while (OUT_SIZE - (c->out_head - c->out_tail) < (UINT32)n) {
        if (um_stopping()) { um_unlock(&c->out_lock); return 0xC0000120u; }
        sched_wait();
    }
    int done = screen_feed(c, screen, seq, n);
    if (done == n && op == 11) __atomic_store_n(&c->cursor_style, value, __ATOMIC_RELEASE);
    um_unlock(&c->out_lock);
    return done == n ? 0 : 0xC0000120u;
}

UINT32 um_console_screen_create(UmConsole *c, UINT32 *id)
{
    if (!c || !id) return 0xC0000008u;
    um_lock(&c->out_lock);
    UINT32 i;
    for (i = 1; i < CON_SCREENS && c->screen[i].used; i++) {}
    if (i == CON_SCREENS) { um_unlock(&c->out_lock); return 0xC000009Au; }
    UmConsoleScreen *s = &c->screen[i];
    if (!screen_init(c, s, c->cols, c->rows)) { um_unlock(&c->out_lock); return 0xC0000017u; }
    s->refs = 1;
    *id = i;
    um_unlock(&c->out_lock);
    return 0;
}

void um_console_screen_ref(UmConsole *c, UINT32 id)
{
    if (!c || !id) return;
    um_lock(&c->out_lock);
    UmConsoleScreen *s = console_screen(c, id);
    if (s) s->refs++;
    um_unlock(&c->out_lock);
}

void um_console_screen_unref(UmConsole *c, UINT32 id)
{
    if (!c || !id) return;
    um_lock(&c->out_lock);
    UmConsoleScreen *s = console_screen(c, id);
    if (s && s->refs) {
        s->refs--;
        screen_release_locked(c, id);
    }
    um_unlock(&c->out_lock);
}

static bool screen_coord(const UmConsoleScreen *s, int x, int y)
{
    return x >= 0 && y >= 0 && x < s->cols && y < s->rows;
}

UINT32 um_console_screen_call(UmConsole *c, UINT32 id, UINT32 op, void *data, UINT32 len, UINT32 *result)
{
    if (!c) return 0xC0000008u;
    if (result) *result = 0;
    um_lock(&c->out_lock);
    screen0_follow(c);
    UmConsoleScreen *s = console_screen(c, id);
    if (!s) { um_unlock(&c->out_lock); return 0xC0000008u; }
    UINT32 status = 0;
    bool redraw = false, clear = false;
    UINT32 top = 0, bottom = s->rows ? s->rows - 1u : 0;   /* the rows to draw again */
    if (op == CON_SCREEN_INFO) {
        if (!data || len < sizeof(UmConsoleScreenInfo)) status = 0xC000000Du;
        else {
            UINT16 win_cols = s->cols < c->cols ? s->cols : c->cols;
            UINT16 win_rows = s->rows < c->rows ? s->rows : c->rows;
            UmConsoleScreenInfo info = {
                (INT16)s->cols, (INT16)s->rows, (INT16)s->cursor_x, (INT16)s->cursor_y,
                s->attr, 0, 0, (INT16)(win_cols - 1), (INT16)(win_rows - 1),
                (INT16)c->cols, (INT16)c->rows
            };
            memcpy(data, &info, sizeof(info));
        }
    } else if (op == CON_SCREEN_CREATE) {
        status = 0xC000000Du;
    } else if (op == CON_SCREEN_ACTIVATE) {
        UINT32 old = c->active_screen;
        c->active_screen = (UINT8)id;
        screen_render(c, s);
        screen_release_locked(c, old);
    } else if (op == CON_SCREEN_SET_ATTR || op == CON_SCREEN_SET_CURSOR ||
               op == CON_SCREEN_SET_SIZE || op == CON_SCREEN_FILL_CHAR ||
               op == CON_SCREEN_FILL_ATTR || op == CON_SCREEN_READ_TEXT ||
               op == CON_SCREEN_READ_ATTR || op == CON_SCREEN_WRITE_CELLS ||
               op == CON_SCREEN_READ_CELLS || op == CON_SCREEN_SCROLL) {
        if (!data || len < sizeof(UmConsoleScreenRequest)) status = 0xC000000Du;
        else {
            UmConsoleScreenRequest *r = data;
            UINT32 total = (UINT32)s->cols * s->rows;
            if (op == CON_SCREEN_SET_ATTR) {
                char seq[32];                           /* what is written next: in these colours */
                int n = screen_sgr(seq, sizeof(seq), r->value);
                screen_feed(c, s, seq, n);
                s->attr = r->value;
            } else if (op == CON_SCREEN_SET_CURSOR) {
                if (!screen_coord(s, r->x, r->y)) status = 0xC000000Du;
                else {
                    char seq[32];
                    int n = ksnprintf(seq, sizeof(seq), "\x1b[%d;%dH", r->y + 1, r->x + 1);
                    if (screen_feed(c, s, seq, n) != n) status = 0xC0000120u;
                }
            } else if (op == CON_SCREEN_SET_SIZE) {
                if (r->width <= 0 || r->height <= 0) status = 0xC000000Du;
                else if (r->width > 32767 || r->height > 32767 ||
                         (UINT64)(UINT16)r->width * (UINT16)r->height > CON_MAX_CELLS) status = 0xC000000Du;
                else if (!screen_resize(s, (UINT16)r->width, (UINT16)r->height)) status = 0xC0000017u;
                else {
                    s->sized = true;
                    redraw = clear = id == c->active_screen;
                    bottom = s->rows - 1u;
                }
            } else if (op == CON_SCREEN_FILL_CHAR || op == CON_SCREEN_FILL_ATTR ||
                       op == CON_SCREEN_READ_TEXT || op == CON_SCREEN_READ_ATTR) {
                UINT32 at;
                if (!screen_coord(s, r->x, r->y)) status = 0xC000000Du;
                else if (r->count > (len - sizeof(*r)) / (op == CON_SCREEN_READ_TEXT ? 2u :
                                                            op == CON_SCREEN_READ_ATTR ? 2u : 1u) &&
                         (op == CON_SCREEN_READ_TEXT || op == CON_SCREEN_READ_ATTR)) status = 0xC000000Du;
                else {
                    at = (UINT32)r->y * s->cols + (UINT32)r->x;
                    UINT32 done = r->count < total - at ? r->count : total - at;
                    if (op == CON_SCREEN_FILL_CHAR)
                        for (UINT32 i = 0; i < done; i++) s->cells[at + i].ch = r->value;
                    else if (op == CON_SCREEN_FILL_ATTR)
                        for (UINT32 i = 0; i < done; i++) s->cells[at + i].attr = r->value;
                    else if (op == CON_SCREEN_READ_TEXT) {
                        UINT16 *out = (UINT16 *)((UINT8 *)data + sizeof(*r));
                        for (UINT32 i = 0; i < done; i++) out[i] = s->cells[at + i].ch;
                    } else {
                        UINT16 *out = (UINT16 *)((UINT8 *)data + sizeof(*r));
                        for (UINT32 i = 0; i < done; i++) out[i] = s->cells[at + i].attr;
                    }
                    if (result) *result = done;
                    redraw = (op == CON_SCREEN_FILL_CHAR || op == CON_SCREEN_FILL_ATTR) && id == c->active_screen && done;
                    top = at / s->cols;
                    bottom = (at + (done ? done - 1 : 0)) / s->cols;
                }
            } else if (op == CON_SCREEN_WRITE_CELLS || op == CON_SCREEN_READ_CELLS) {
                if (r->width <= 0 || r->height <= 0 || r->left > r->right || r->top > r->bottom ||
                    r->x < 0 || r->y < 0 || r->x + r->right - r->left >= r->width ||
                    r->y + r->bottom - r->top >= r->height ||
                    (UINT64)(UINT16)r->width * (UINT16)r->height >
                        (len - sizeof(*r)) / sizeof(UmConsoleCell)) status = 0xC000000Du;
                else {
                    int l = r->left, t = r->top, rr = r->right, b = r->bottom;
                    if (l < 0) l = 0;
                    if (t < 0) t = 0;
                    if (rr >= s->cols) rr = s->cols - 1;
                    if (b >= s->rows) b = s->rows - 1;
                    if (l > rr || t > b) status = 0xC000000Du;
                    else {
                        UmConsoleCell *cells = (UmConsoleCell *)((UINT8 *)data + sizeof(*r));
                        for (int y = t; y <= b; y++) for (int x = l; x <= rr; x++) {
                            UmConsoleCell *dst = &s->cells[y * s->cols + x];
                            UmConsoleCell *src = &cells[((r->y + y - r->top) * r->width) + r->x + x - r->left];
                            if (op == CON_SCREEN_WRITE_CELLS) *dst = *src;
                            else *src = *dst;
                        }
                        r->left = (INT16)l; r->top = (INT16)t; r->right = (INT16)rr; r->bottom = (INT16)b;
                        redraw = op == CON_SCREEN_WRITE_CELLS && id == c->active_screen;
                        top = (UINT32)t;
                        bottom = (UINT32)b;
                    }
                }
            } else if (op == CON_SCREEN_SCROLL) {
                if (r->left < 0 || r->top < 0 || r->left > r->right || r->top > r->bottom ||
                    r->right >= s->cols || r->bottom >= s->rows ||
                    (r->flags && (r->clip_left > r->clip_right || r->clip_top > r->clip_bottom)))
                    status = 0xC000000Du;
                else {
                    int width = r->right - r->left + 1, height = r->bottom - r->top + 1;
                    size_t bytes = (size_t)width * height * sizeof(UmConsoleCell);
                    UmConsoleCell *copy = kmalloc(bytes);
                    if (!copy) status = 0xC0000017u;
                    else {
                        UmConsoleCell fill = { r->value, r->attributes };
                        for (int y = 0; y < height; y++)
                            memcpy(copy + y * width, s->cells + (r->top + y) * s->cols + r->left,
                                   width * sizeof(*copy));
                        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
                            int sy = r->top + y, sx = r->left + x;
                            s->cells[sy * s->cols + sx] = fill;
                            int dy = r->dest_y + y, dx = r->dest_x + x;
                            bool in_clip = !r->flags ||
                                (dx >= r->clip_left && dx <= r->clip_right &&
                                 dy >= r->clip_top && dy <= r->clip_bottom);
                            if (in_clip && dx >= 0 && dy >= 0 && dx < s->cols && dy < s->rows)
                                s->cells[dy * s->cols + dx] = copy[y * width + x];
                        }
                        kfree(copy);
                        redraw = id == c->active_screen;
                        int lo = r->top < r->dest_y ? r->top : r->dest_y;
                        int hi = r->bottom > r->dest_y + height - 1 ? r->bottom : r->dest_y + height - 1;
                        top = lo < 0 ? 0 : (UINT32)lo;
                        bottom = hi >= s->rows ? s->rows - 1u : (UINT32)hi;
                    }
                }
            }
        }
    } else status = 0xC000000Du;
    if (!status && redraw && top <= bottom) screen_render_rows(c, s, top, bottom, clear);
    um_unlock(&c->out_lock);
    return status;
}

UINT32 UmConsoleCursorStyle(UmConsole *c)
{
    return c ? __atomic_load_n(&c->cursor_style, __ATOMIC_ACQUIRE) : 25 | 0x100;
}
