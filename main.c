#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

#define EDITOR_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

struct editorConfig {
    int cx, cy;
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

static struct editorConfig E;

void terminate(const char* str) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 4);
    
    perror(str);
    exit(1);
}

void disableRawMode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        terminate("tcsetattr");
}

void enableRawMode() {
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        terminate("tcgetattr");

    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        terminate("tcsetattr");
}

int editorReadKey() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN)
            terminate("read");
    }
    if(c == '\x1b') {
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '[') {
            switch(seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

int getCursorPosition(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;

    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    while(i < sizeof(buf) - 1) {
        if(read(STDIN_FILENO, &buf[i], 1) != 1 )
            break;

        if(buf[i] == 'R')
            break;

        i++;
    }

    buf[i] = 0;

    if(buf[0] != '\x1b' || buf[1] != '[') 
        return -1;

    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

int getWindowSize(int* rows, int* cols) {
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;

        return getCursorPosition(rows, cols);
    } else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
}

struct abuf {
    char* b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf* ab, const char* str, int len) {
    char* new = realloc(ab->b, ab->len + len);

    if(!new) return;

    memcpy(&new[ab->len], str, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf* ab) {
    free(ab->b);
}
void editorDrawRows(struct abuf* ab) {
    int y;
    for(y = 0; y < E.screenrows; y++) {
        if(y == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Text Editor -- version %s", EDITOR_VERSION);
            if(welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if(padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while(padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if(y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorMoveCursor(int key) {
    switch(key) {
        case ARROW_LEFT:
            if(E.cx != 0) {
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            if(E.cx != E.screencols - 1) {
                E.cx++;
            }
            break;
        case ARROW_UP:
            if(E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if(E.cy != E.screenrows - 1) {
                E.cy++;
            }
            break;
    }
}
void editorProcessKeypress() {
    int c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 4);

            exit(0);
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

void initEditor() {
    E.cx = 0;
    E.cy = 0;

    if(getWindowSize(&E.screenrows, &E.screencols) == -1)
        terminate("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
