#include <ncurses.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>

#define TAU 6.283185307179586
#define MAXPAL 96

static int rows, cols;
static int npalette;
static int has256;
static unsigned char *prevbuf;
static volatile sig_atomic_t g_quit;

static void cleanup(int sig)
{
    (void)sig;
    g_quit = 1;
    endwin();
    _exit(0);
}

static void hsv2rgb(double h, double s, double v, int *R, int *G, int *B)
{
    h = h - floor(h / 360.0) * 360.0;
    double c = v * s;
    double x = c * (1 - fabs(fmod(h / 60.0, 2.0) - 1));
    double m = v - c;
    double r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    *R = (int)((r + m) * 255.0 + 0.5);
    *G = (int)((g + m) * 255.0 + 0.5);
    *B = (int)((b + m) * 255.0 + 0.5);
}

static int rgb2idx256(int r, int g, int b)
{
    static int cube[6] = {0, 95, 135, 175, 215, 255};
    int best = 16, bestd = 1 << 30, i;
    for (i = 0; i < 6; i++) {
        int dr = r - cube[i];
        for (int j = 0; j < 6; j++) {
            int dg = g - cube[j];
            for (int k = 0; k < 6; k++) {
                int db = b - cube[k];
                int d = dr * dr + dg * dg + db * db;
                if (d < bestd) { bestd = d; best = 16 + 36 * i + 6 * j + k; }
            }
        }
    }
    int gray = 232 + (int)((r * 299 + g * 587 + b * 114) / 1000 * 24 / 255);
    int gr = 8 + (gray - 232) * 10;
    int dg = (r - gr) * (r - gr) + (g - gr) * (g - gr) + (b - gr) * (b - gr);
    if (dg < bestd) best = gray;
    return best;
}

static int rgb2idx8(int r, int g, int b)
{
    static const int lv[8] = {0, 128, 170, 215, 215, 215, 255, 255};
    static const int basic[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    int best = 0, bestd = 1 << 30;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            for (int k = 0; k < 8; k++) {
                int dr = r - lv[i], dg = g - lv[j], db = b - lv[k];
                int d = dr * dr + dg * dg + db * db;
                if (d < bestd) {
                    bestd = d;
                    best = basic[i] | (basic[j] << 1) | (basic[k] << 2);
                    if (best >= COLORS) best = COLORS - 1;
                }
            }
        }
    }
    return best;
}

static void set_palette(double t, double hue_speed)
{
    int i;
    for (i = 0; i < npalette; i++) {
        double hue = (double)i * 360.0 / npalette + t * hue_speed;
        int r, g, b;
        double sat = 1.0;
        double val = 1.0;
        hsv2rgb(hue, sat, val, &r, &g, &b);
        int c = has256 ? rgb2idx256(r, g, b) : rgb2idx8(r, g, b);
        init_pair((short)(i + 1), (short)c, 0);
    }
}

static unsigned int hash3(unsigned int a, unsigned int b, unsigned int c)
{
    a ^= b << 13; a ^= c >> 7; a ^= b << 5; a ^= c << 11;
    a *= 2654435761u; b *= 2246822519u; c *= 3266489917u;
    return a ^ b ^ c;
}

static int plasma(double x, double y, double cx, double cy, double t)
{
    double v = sin(x * 0.070 + t)
             + sin(y * 0.090 - t * 0.71)
             + sin((x + y) * 0.045 + t * 0.53)
             + sin(sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy) * 3.6) * 0.11 - t * 1.27);
    int idx = (int)((v + 4.0) * npalette / 8.0);
    return idx;
}

static int tunnel(double x, double y, double cx, double cy, double t)
{
    double dx = x - cx;
    double dy = (y - cy) * 2.0;
    double r = sqrt(dx * dx + dy * dy) + 0.001;
    double a = atan2(dy, dx) / TAU;
    double u = 3.0 / r + t * 1.4;
    int idx = (int)((u * 1.5 + a * npalette * 4.0) * 0.9);
    return idx;
}

static int kaleido(double x, double y, double cx, double cy, double t)
{
    double dx = x - cx;
    double dy = (y - cy) * 2.0;
    double r = sqrt(dx * dx + dy * dy);
    double a = fabs(atan2(dy, dx));
    double sect = fmod(a, TAU / 6.0);
    if (sect > TAU / 12.0) sect = TAU / 6.0 - sect;
    double v = sin(r * 0.20 - t * 2.2)
             + sin(sect * 7.0 + t * 0.9)
             + sin(r * 0.06 * sin(sect * 3.0) * 4.0 + t * 1.4);
    int idx = (int)((v + 3.0) * npalette / 6.0);
    return idx;
}

static const char *mode_names[] = {"PLASMA", "TUNNEL", "KALEIDO"};
static const char glyphs[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    "@#$%&*+=?<>~/\\^|!~";
static const int nglyphs = (int)(sizeof glyphs - 1);

int main(void)
{
    if (!isatty(STDOUT_FILENO)) {
        fprintf(stderr, "psychedelic: stdout is not a terminal\n");
        return 1;
    }

    initscr();
    if (has_colors() == FALSE) {
        endwin();
        fprintf(stderr, "psychedelic: terminal has no color support\n");
        return 1;
    }
    start_color();
    use_default_colors();
    curs_set(0);
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);

    has256 = (COLORS >= 256);
    if (has256) {
        npalette = 64;
        if (npalette > COLOR_PAIRS - 1) npalette = COLOR_PAIRS - 1;
        if (npalette > MAXPAL) npalette = MAXPAL;
    } else {
        npalette = COLORS >= 16 ? 16 : 8;
        if (npalette > COLOR_PAIRS - 1) npalette = COLOR_PAIRS - 1;
    }
    for (int i = 0; i < npalette; i++)
        init_pair((short)(i + 1), (short)(i % COLORS), 0);

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGSEGV, cleanup);

    getmaxyx(stdscr, rows, cols);
    prevbuf = calloc((size_t)rows * cols, 1);
    if (!prevbuf) { endwin(); return 1; }

    int mode = 0;
    double t = 0.0;
    double speed = 1.0;
    int paused = 0;
    int show_hud = 0;
    unsigned long frame = 0;
    struct timeval last, now;
    gettimeofday(&last, NULL);

    bkgd(COLOR_PAIR(1));
    erase();

    while (!g_quit) {
        int ch;
        while ((ch = getch()) != ERR) {
            switch (ch) {
            case 'q': case 'Q': case 27:
                goto done;
            case ' ':
                paused = !paused;
                break;
            case 'm': case 'M':
                mode = (mode + 1) % 3;
                erase();
                memset(prevbuf, 0, (size_t)rows * cols);
                break;
            case '+': case '=':
                speed *= 1.25;
                if (speed > 8.0) speed = 8.0;
                break;
            case '-': case '_':
                speed /= 1.25;
                if (speed < 0.1) speed = 0.1;
                break;
            case 'h': case 'H':
                show_hud = !show_hud;
                if (!show_hud) erase();
                memset(prevbuf, 0, (size_t)rows * cols);
                break;
            case KEY_RESIZE:
                getmaxyx(stdscr, rows, cols);
                free(prevbuf);
                prevbuf = calloc((size_t)rows * cols, 1);
                erase();
                break;
            }
        }

        gettimeofday(&now, NULL);
        double dt = (now.tv_sec - last.tv_sec) + (now.tv_usec - last.tv_usec) / 1e6;
        last = now;
        if (dt > 0.25) dt = 0.25;
        if (!paused) t += dt * speed;

        set_palette(t, mode == 1 ? 55.0 : 38.0);

        double cx = cols / 2.0, cy = rows / 2.0;
        unsigned char *cur = malloc((size_t)rows * cols);
        unsigned char *curc = malloc((size_t)rows * cols);
        if (!cur || !curc) { free(cur); free(curc); break; }

        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                int idx;
                if (mode == 0)      idx = plasma(x, y, cx, cy, t);
                else if (mode == 1) idx = tunnel(x, y, cx, cy, t);
                else                idx = kaleido(x, y, cx, cy, t);

                idx %= npalette;
                if (idx < 0) idx += npalette;

                unsigned int h = hash3((unsigned)x, (unsigned)y, (unsigned)frame);
                if ((h & 31) == 0) {
                    idx = (idx + (int)((h >> 8) % 3) - 1 + npalette) % npalette;
                }
                size_t o = (size_t)y * cols + x;
                cur[o] = (unsigned char)(idx + 1);
                curc[o] = (unsigned char)(glyphs[(h >> 16) % nglyphs]);
            }
        }

        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                size_t o = (size_t)y * cols + x;
                unsigned char v = cur[o];
                if (prevbuf[o] == v) continue;
                prevbuf[o] = v;
                chtype a = COLOR_PAIR(v);
                if ((v & 3) == 0) a |= A_BOLD;
                mvaddch(y, x, (chtype)curc[o] | a);
            }
        }
        free(curc);

        if (show_hud) {
            char hud[128];
            int len = snprintf(hud, sizeof hud, " %s  x%.2f  %s ",
                               mode_names[mode], speed, paused ? "PAUSED" : "play");
            attron(A_BOLD);
            for (int i = 0; i < len && i < cols; i++)
                mvaddch(rows - 1, i, (chtype)hud[i] | A_REVERSE);
            attroff(A_BOLD);
            for (int i = 0; i < len && i < cols; i++)
                prevbuf[(size_t)(rows - 1) * cols + i] = 255;
        }

        refresh();
        free(cur);
        frame++;

        napms(28);
    }

done:
    free(prevbuf);
    endwin();
    return 0;
}
