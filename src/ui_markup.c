#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#if __has_include(<ncurses.h>)
#include <ncurses.h>
#elif __has_include(<ncurses/ncurses.h>)
#include <ncurses/ncurses.h>
#elif __has_include(<curses.h>)
#include <curses.h>
#else
#error "ncurses headers not found"
#endif
#else
#include <curses.h>
#endif

#include "ui_markup.h"

static int g_ncurses_ready = 0; // global var to traack if ncurses has been initialized.
static ui_markup_config_t g_markup_cfg;
static int g_markup_cfg_ready = 0;

static void ensure_ncurses(void);

void ui_markup_config_default_game(ui_markup_config_t *cfg)
{
    if (!cfg) return;
    // keep the current game UI behavior as the default.
    cfg->no_echo = 1;
    cfg->cbreak_mode = 1;
    cfg->cursor_visible = 0;
    cfg->keypad_enabled = 1;
    cfg->nodelay_enabled = 1;
    cfg->init_colors = 1;
    cfg->use_default_colors = 1;
}

void ui_markup_config_default_admin(ui_markup_config_t *cfg)
{
    if (!cfg) return;
    // admin panels - visible typing and a visible cursor.
    cfg->no_echo = 0;
    cfg->cbreak_mode = 1;
    cfg->cursor_visible = 1;
    cfg->keypad_enabled = 1;
    cfg->nodelay_enabled = 0;
    cfg->init_colors = 1;
    cfg->use_default_colors = 1;
}

static void ensure_default_config(void)
{
    if (g_markup_cfg_ready) return;
    ui_markup_config_default_game(&g_markup_cfg);
    // only seed defaults once, then keep whatever the caller selected.
    g_markup_cfg_ready = 1;
}

int ui_markup_init_with_config(const ui_markup_config_t *cfg)
{
    if (!cfg) return 0;
    // strict behavior: active sessions cannot be reconfigured.
    if (g_ncurses_ready) return 0;

    // copy the caller config so later render calls use the same policy.
    g_markup_cfg = *cfg;
    g_markup_cfg_ready = 1;

    ensure_ncurses();
    return g_ncurses_ready;
}

// function requires nothing and returns nothing. probably change this later for generalised cases (ESC)
// I need to set noecho to false for the admin panel
static void ensure_ncurses(void)
{
    // avoid re-init if we already flipped terminal into ncurses mode.
    if (g_ncurses_ready) return;

    ensure_default_config();

    // initialize screen
    initscr();
    // policy-driven terminal behavior so external projects can pick different UX.
    if (g_markup_cfg.no_echo) noecho();
    else echo();

    if (g_markup_cfg.cbreak_mode) cbreak();
    else nocbreak();

    (void)curs_set(g_markup_cfg.cursor_visible ? 1 : 0);
    keypad(stdscr, g_markup_cfg.keypad_enabled ? TRUE : FALSE);
    nodelay(stdscr, g_markup_cfg.nodelay_enabled ? TRUE : FALSE);

    // init colors (if not already initialised)
    if (g_markup_cfg.init_colors && has_colors()) {
        start_color();
        if (g_markup_cfg.use_default_colors) {
            use_default_colors();
        }

        // init colors (-1 means default bg col)
        init_pair(1, COLOR_CYAN,    -1);
        init_pair(2, COLOR_YELLOW,  -1);
        init_pair(3, COLOR_MAGENTA, -1);
        init_pair(4, COLOR_GREEN,   -1);
        init_pair(5, COLOR_RED,     -1);
        init_pair(6, COLOR_BLUE,    -1);
        init_pair(7, COLOR_WHITE,   -1);

        // ghost block
        init_pair(8, COLOR_WHITE,   -1);
        init_pair(9, COLOR_CYAN,    -1);
    }
    // it is now ready
    g_ncurses_ready = 1;
}

// cleanup functions so my terminal doesn't die after leaving the game
void ui_markup_shutdown(void)
{
    // if ncurses was never initialized it doesn't really matter
    if (!g_ncurses_ready) return;

    // endwin is the archnemesis of initscr
    endwin();
    // it is now not ready
    g_ncurses_ready = 0;
}


static void move_cursor(int row, int col)
{
    // cursor movement always goes through this wrapper 
    // easier to modify thru here in case change renderer (like ANSI)
    ensure_ncurses();
    move(row, col);
}

// init UI template
void ui_template_init(ui_template_t *tpl)
{
    if (!tpl) return;
    // set whole template struct to 0. Template struct is calculated from size of struct, which
    // is size of nodes array + count (integer).
    // Calculation is done at compile time
    memset(tpl, 0, sizeof(*tpl));
}

// same as above btw
void ui_bindings_init(ui_bindings_t *bind)
{
    if (!bind) return;
    // clear all key/value slots before use.
    memset(bind, 0, sizeof(*bind));
}

// set kv pairs
int ui_bindings_set(ui_bindings_t *bind, const char *key, const char *value)
{
    // null pointer of invalid key
    if (!bind || !key || !value || key[0] == '\0') return 0;

    // if key present update it
    for (int i = 0; i < bind->count; i++) {
        if (strcmp(bind->items[i].key, key) == 0) {
            snprintf(bind->items[i].value, sizeof(bind->items[i].value), "%s", value);
            return 1;
        }
    }

    // if too many kv pairs don't add another
    if (bind->count >= UI_BIND_MAX) return 0;

    // insert new key/value (bind.count < max bind AND key not present)
    snprintf(bind->items[bind->count].key, sizeof(bind->items[bind->count].key), "%s", key);
    snprintf(bind->items[bind->count].value, sizeof(bind->items[bind->count].value), "%s", value);
    bind->count++;
    return 1;
}

// getter function
const char *ui_bindings_get(const ui_bindings_t *bind, const char *key)
{
    if (!bind || !key || key[0] == '\0') return NULL;

    // check each kv pair iteratively. C doesn't have hash table iirc
    for (int i = 0; i < bind->count; i++) {
        if (strcmp(bind->items[i].key, key) == 0) {
            return bind->items[i].value;
        }
    }

    return NULL;
}

// given a line of markup, find an int attribute and parse it into an int. 1 on success, 0 on failure.
// *line = memory addr of line of markup, char *key = memory addr of attr key,
// *out = where the parsed int will be stored.
static int parse_int_attr(const char *line, const char *key, int *out)
{
    // set p to first occurence of key in line. If not found, return 0.
    // strstr returns a pointer to first occurence of key substring in line.
    const char *p = strstr(line, key);
    if (!p || !out) return 0;

    // move pointer p to first occurence of " after key."
    p = strchr(p, '"');
    if (!p) return 0;
    p++; // then move again to point to the value. So now p points to the actual int

    // init end to null. String to long will set the endpoint 
    char *endp = NULL;
    // v is long value of string at p. arg 10 --> base 10.
    long v = strtol(p, &endp, 10);
    // no conversion performed
    if (endp == p) return 0;

    // set *out to parsed int value (v) from strtol.
    *out = (int)v;
    // success!
    return 1;
}

// parse int, but string. Refer to above function
static int parse_str_attr(const char *line, const char *key, char *out, size_t out_cap)
{
    if (!line || !key || !out || out_cap == 0) return 0;

    // same idea as int parser, but copy the string between quotes.
    const char *p = strstr(line, key);
    if (!p) return 0;

    p = strchr(p, '"');
    if (!p) return 0;
    p++;

    // the closing quote. could use this for int too, but we did strtol for validation and parsing because I am lazy
    const char *end = strchr(p, '"');
    // if no closing quote or end is somehow before p, err
    if (!end || end <= p) return 0;

    // length of string to copy
    size_t len = (size_t)(end - p);
    // max len is outcap - 1, need space for null terminator \0
    if (len >= out_cap) len = out_cap - 1;
    // copy from p to out, length bytes len
    memcpy(out, p, len);
    // add null terminaator
    out[len] = '\0';
    return 1;
}

// remove the traailing stuff aat the end of a string. newline, whitespace etc
static void trim_trailing(char *s)
{
    if (!s) return;
    // get length of string and cast to int for while loop laateraaaaaaaaaaaaa
    int n = (int)strlen(s);
    // iterate from end of string. While n > 0 and the latest character is a newline (\n), carriage return (\r)
    // or whitespace, set that character to null terminator and decrement n
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1]))) {
        s[n - 1] = '\0';
        n--;
    }
}

// take template with {{value}} and replace {{value}} with value.
// src = input string w {{value}}, dst = output string, dst_cap = max size of output
// bind = kv paairs
static void apply_bindings(const char *src, char *dst, size_t dst_cap, const ui_bindings_t *bind)
{
    if (!src || !dst || dst_cap == 0 || !bind) return;
    
    const char *r = src; // read pointer
    size_t w = 0; // write counter

    // while there aare still characters to read & < output max size,
    while (*r && w + 1 < dst_cap) {
        // if while iterating we found a {{, look for the closing }} and extraact key
        if (r[0] == '{' && r[1] == '{') {
            const char *token_start = r + 2; // ignore {{
            const char *token_end = strstr(token_start, "}}");

            // can't find closing }}.
            if (!token_end) {
                // while there are still chars to read and not maax output size,
                // set output[write pointer] to read pointer, then increment both (both are post increment)
                while (*r && w + 1 < dst_cap) dst[w++] = *r++;
                break;
            }

            // temp buffer to hold the key
            char key[UI_BIND_KEY_MAX];
            // length of actuaal key
            size_t key_len = (size_t)(token_end - token_start);
            // if key too long we just cut it off, -1 for \0 later.
            if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
            memcpy(key, token_start, key_len);
            key[key_len] = '\0';

            // get the value
            const char *value = ui_bindings_get(bind, key);
            // if present, copy value to output
            if (value) {
                // take eaach character from vaalue and copy to output until end of value or max output size (+ null terminator)
                // put i of vaalue into output at write pointer, then increment both (post increment)
                for (size_t i = 0; value[i] != '\0' && w + 1 < dst_cap; i++) {
                    dst[w++] = value[i];
                }
            } else {
                // can't find vaalue
                // *orig is set to read pointer, orig_end to +2 include }}. We just copy {{key}} to output
                const char *orig = r;
                const char *orig_end = token_end + 2;
                while (orig < orig_end && w + 1 < dst_cap) {
                    dst[w++] = *orig++;
                }
            }

            // advance read pointer after closing }},
            // then keep reaading the template file
            r = token_end + 2;
            continue;
        }

        // just copy chara from read pointer and paste to output if not {{}}
        dst[w++] = *r++;
    }

    // add null terminator to the end of the output
    dst[w] = '\0';
}

int ui_template_load_from_file(const char *path, ui_template_t *tpl)
{
    if (!path || !tpl) return 0;

    // read file if exists
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    ui_template_init(tpl);

    // for each line in file
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        trim_trailing(line); // trim trailing whitespace newline etc etc

        // check if template full (> max nodes)
        if (tpl->count >= UI_MAX_NODES) break;

        // if text node
        if (strstr(line, "<text") == line) {
            // add text node to template nodes add the back
            ui_node_t *n = &tpl->nodes[tpl->count];
            n->type = UI_NODE_TEXT;
            // find x and y with helper function above
            if (!parse_int_attr(line, "x=", &n->x)) continue;
            if (!parse_int_attr(line, "y=", &n->y)) continue;

            // get pointers for value between > and </text>.
            const char *start = strchr(line, '>');
            const char *end = strstr(line, "</text>");
            if (!start || !end || end <= start) continue;

            // move pointer to first char after >
            start++;
            // get len, if too long, cut off
            size_t len = (size_t)(end - start);
            if (len >= sizeof(n->text)) len = sizeof(n->text) - 1;
            // copy from start to text attr of n, add null terminator
            memcpy(n->text, start, len);
            n->text[len] = '\0';
            // add counter
            tpl->count++;
        } else if (strstr(line, "<box") == line) {
            // box node. same ting
            ui_node_t *n = &tpl->nodes[tpl->count];
            n->type = UI_NODE_BOX;
            if (!parse_int_attr(line, "x=", &n->x)) continue;
            if (!parse_int_attr(line, "y=", &n->y)) continue;
            if (!parse_int_attr(line, "w=", &n->w)) continue;
            if (!parse_int_attr(line, "h=", &n->h)) continue;
            tpl->count++;
        } else if (strstr(line, "<widget") == line) {
            // widget 
            ui_node_t *n = &tpl->nodes[tpl->count];
            n->type = UI_NODE_WIDGET;
            if (!parse_int_attr(line, "x=", &n->x)) continue;
            if (!parse_int_attr(line, "y=", &n->y)) continue;
            if (!parse_str_attr(line, "type=", n->widget_type, sizeof(n->widget_type))) continue;
            // id is optional for now.
            (void)parse_str_attr(line, "id=", n->widget_id, sizeof(n->widget_id));
            tpl->count++;
        }
    }

    // close the file, return 1
    fclose(fp);
    return 1;
}

//drawabox
static void draw_box(const ui_node_t *n)
{
    if (!n || n->w < 2 || n->h < 2) return;

    // shared accent color for frame bits.
    attron(COLOR_PAIR(9));

    // move to top left corner, +----------+ (width)
    move_cursor(n->y, n->x);
    addch('+');
    for (int i = 0; i < n->w - 2; i++) addch('-');
    addch('+');

    // then, draw the sides of the box
    for (int r = 1; r < n->h - 1; r++) {
        move_cursor(n->y + r, n->x);
        addch('|');
        move_cursor(n->y + r, n->x + n->w - 1);
        addch('|');
    }

    // draw the bottom
    move_cursor(n->y + n->h - 1, n->x);
    addch('+');
    for (int i = 0; i < n->w - 2; i++) addch('-');
    addch('+');

    // off color pair
    attroff(COLOR_PAIR(9));
}

// takes in the UI template and bindings table.
void ui_template_render(const ui_template_t *tpl, const ui_bindings_t *bind)
{
    if (!tpl || !bind) return;

    // we need ncurses
    ensure_ncurses();
    // wipe ncurses buffer
    erase();

    // iterate through each node in template and render
    for (int i = 0; i < tpl->count; i++) {
        // get current node
        const ui_node_t *n = &tpl->nodes[i];
        // text node
        if (n->type == UI_NODE_TEXT) {
            // temp text buffer to store text after applying bindings
            char text[UI_TEXT_MAX];
            // apply bindings to text node, store res in buffer
            apply_bindings(n->text, text, sizeof(text), bind);
            // move cursor to location
            move_cursor(n->y, n->x);
            // draw text, up to text length - 1
            addnstr(text, UI_TEXT_MAX - 1);
        } else if (n->type == UI_NODE_BOX) {
            // box?
            draw_box(n);
        }
    }

    // caller performs final refresh after all widgets are drawn.
}

int ui_template_find_widget(
    const ui_template_t *tpl,
    const char *widget_type,
    const char *widget_id,
    int *x_out,
    int *y_out)
{
    if (!tpl || !x_out || !y_out) return 0;

    // first match wins. deterministic lookup
    for (int i = 0; i < tpl->count; i++) {
        const ui_node_t *n = &tpl->nodes[i];
        // only consider widget nodes
        if (n->type != UI_NODE_WIDGET) continue;

        const char *type_value = n->widget_type;
        const char *id_value = n->widget_id;

        // if caller doesn't care about type or id, match.
        int type_ok = (widget_type == NULL) || (strcmp(type_value, widget_type) == 0);
        int id_ok = (widget_id == NULL) || (strcmp(id_value, widget_id) == 0);
        if (!type_ok || !id_ok) continue;

        *x_out = n->x;
        *y_out = n->y;
        return 1;
    }

    return 0;
}
