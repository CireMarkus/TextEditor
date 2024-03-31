/*** includes ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define TRILL_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f) //bitwise AND operation to turn off the 5th and 6th bits of the character

enum editorKey{
    ARROW_LEFT = 1000,
    ARROW_RIGHT ,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY, 
    PAGE_UP, 
    PAGE_DOWN
};

/*** data ***/
typedef struct erow{ //editor row
    int size;
    char *chars;
}erow;

struct editorConfig{
    int cx, cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    struct termios orig_termios;
};

struct editorConfig E;


/*** append buffer ***/
struct abuf{
    char *b;
    int len;
};

void abAppend(struct abuf *ab, const char *s, int len){
    char *new = realloc(ab->b, ab->len + len); //request a new block of memory the size of the old string + the new string

    if(new == NULL) return;
    memcpy(&new[ab->len], s, len); //copy the new string to the end of the old string
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab){ //free the memory allocated for the buffer
    free(ab->b);
}

#define  ABUF_INIT {NULL, 0} //initialize the buffer to NULL and 0


/*** output ***/

void editorScroll(){ //Checks to see if the cursor has moved outside the visible window and scrolls the window accordingly
    
    if(E.cy < E.rowoff){ //if the cursor is above the visible window then scroll up. 
        E.rowoff = E.cy;
    }
    if(E.cy >= E.rowoff + E.screenrows){ //if the cursor is below the visible window then scroll down
        E.rowoff = E.cy - E.screenrows + 1;
    }
    
    if (E.cx < E.coloff) //if the cursor is to the left of the visible window then scroll left
    {
        E.coloff = E.cx;
    }
    if(E.cx >= E.coloff + E.screencols) //if the cursor is to the right of the visible window then scroll right
    {
        E.coloff = E.cx - E.screencols + 1;
    }
    

}

void editorDrawRows(struct abuf *ab){
    int y;
    for(y = 0; y < E.screenrows; y++){
        int filerow = y + E.rowoff;
        if(filerow >= E.numrows){
            if(E.numrows == 0 && y == E.screenrows / 3){
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), 
                    "Trill editor -- version %s", TRILL_VERSION);
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if(padding){
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);
                    abAppend(ab, welcome, welcomelen);
            }
            else
            {
                abAppend(ab, "~", 1);
            }
        }else{
            int len = E.row[filerow].size - E.coloff; //get the length of the row
            if (len < 0) len = 0;
            if(len > E.screencols) len = E.screencols; //if the length of the row is greater than the screen width, set the length to the screen width
            abAppend(ab, &E.row[filerow].chars[E.coloff], len); // append the row to the buffer
        }
        
        

        abAppend(ab, "\x1b[K", 3); //clear the line
        if(y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
        
    }
}


void editorRefreshScreen(int exit){
    editorScroll();
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // hide the cursor
    abAppend(&ab, "\x1b[2J", 4); // clear the entire screen
    abAppend(&ab, "\x1b[H", 3); // reposition the cursor to top of screen.

    if(exit != -1){
        editorDrawRows(&ab);
        char buf[32];
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy + 1 - E.rowoff), (E.cx + 1  - E.coloff) + 1); //reposition the cursor to the current position
        abAppend(&ab, buf, strlen(buf));
    }

    
    abAppend(&ab, "\x1b[?25h", 6); // show the cursor
    write(STDOUT_FILENO, ab.b, ab.len); 

    abFree(&ab); // free the memory allocated for the buffer
}



/*** terminal ***/
void die(const char *s){
    //error handling function and exits the program
    editorRefreshScreen(-1);
    perror(s);
    exit(1);
}

// Disable raw mode and returns terminal to normal. 
void disableRawMode(){
    //error handling
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1){
        die("tcsetattr");
    }
    
}

// Enable raw mode
void enableRawMode(){
    
    //error handling
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1){
        die("tcgetattr");
    }

    atexit(disableRawMode); //atexit calls the disable raw mode function when the program exits.

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK| ISTRIP | IXON); //turn off ctrl-c, ctrl-s, ctrl-q, ctrl-v, ctrl-m
    raw.c_oflag &= ~(OPOST); //turn off output processing
    raw.c_cflag |= (CS8); //set character size to 8 bits per byte
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); //turn off echo, canonical mode, ctrl-v, ctrl-c, ctrl-z
    
    raw.c_cc[VMIN] = 0; //set VMIN to 1 to read input byte by byte
    raw.c_cc[VTIME] = 1; //set VTIME to 0 to wait indefinitely for input
    
    //error handling
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); //Writes all pending output to the terminal and discards any input that hasn't been read.
}

int editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }

    if(c == '\x1b'){
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if(seq[0] == '['){
            if(seq[1] >= '0' && seq[1] <= '9'){
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                    if(seq[2] == '~'){
                        switch(seq[1]){
                            case '1': return HOME_KEY;
                            case '3': return DEL_KEY;
                            case '4': return END_KEY;
                            case '5': return PAGE_UP;
                            case '6': return PAGE_DOWN;
                            case '7': return HOME_KEY;
                            case '8': return END_KEY;
                            
                        }
                    }
            }else{
                switch(seq[1]){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
            
        }else if (seq[0] == 'O'){
            switch(seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    }else{
        return c;
    }

    
}

int getCursorPosition(int* rows, int* cols){
    char buf[32];
    unsigned int i = 0;
    
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while(i < sizeof(buf) - 1){
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols){
    struct winsize ws; 

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B",12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/

void editorAppendRow(char *s, size_t len){
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len + 1;
    
    E.row[at].chars = malloc(len + 2);
    E.row[at].chars[0] = '~'; 
    memcpy(E.row[at].chars + 1, s, len);
    E.row[at].chars[len + 1] = '\0';
    E.numrows++;
}

/*** file i/o ***/
void editorOpen(char *filename){
    FILE *fp = fopen(filename, "r");
    if(!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    
    while((linelen = getline(&line, &linecap, fp)) != -1){
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')){
            linelen--;
        }
        editorAppendRow(line, linelen); 
    }
    free(line);
    fclose(fp);    
}

/*** input ***/
void editorMoveCursor(int key){
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];  //get the current row

    switch(key){
        case ARROW_UP:
            if (E.cy != 0)
                E.cy--;
            break;
        case ARROW_LEFT:
            if(E.cx != 0)
                E.cx--;
            break;
        case ARROW_DOWN:
            if(E.cy < E.numrows)
                E.cy++;
            break;
        case ARROW_RIGHT:
            if(row && E.cx < row->size) //Allows the cursor to move the cursor past end of the row. 
                E.cx++;
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy]; //get the new row
    int rowlen = row ? row->size : 0; //get the length of the new row
    if(E.cx > rowlen) //if the cursor is past the end of the row, set the cursor to the end of the row
        E.cx = rowlen;
}

void editorProcessKeypress(){
    int c = editorReadKey();
    switch(c){
        case CTRL_KEY('q'):
            editorRefreshScreen(-1);
            exit(0);
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while(times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/
void initEditor(){
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;

    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}


int main(int argc, char *argv[]){
    enableRawMode();
    initEditor();
    if(argc >= 2){
        editorOpen(argv[1]);
    }

    while(1){
        editorRefreshScreen(0);
        editorProcessKeypress();
    }
    return 0; 
}