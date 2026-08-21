#ifndef UI_MARKUP_H
#define UI_MARKUP_H // if UI_MARKUP_H is undefined, define it.
// basically ensures no duplicate inclusion of this header file.

// max number of nodes per template, max text length per node (node = element)
#define UI_MAX_NODES 64
#define UI_TEXT_MAX 1024
// max lengths for widget type and id strings
#define UI_WIDGET_TYPE_MAX 32
#define UI_WIDGET_ID_MAX 32
#define UI_BIND_MAX 64 // up to 64 kv pairs for the entire ui_bindings_t object. All nodes use the same binding table
#define UI_BIND_KEY_MAX 32 // key max len
#define UI_BIND_VALUE_MAX 1024 // value max len

// enumerate node types for each element
typedef enum {
    UI_NODE_TEXT = 0,
    UI_NODE_BOX = 1,
    UI_NODE_WIDGET = 2,
} ui_node_type_t; // basically integers with names

// each node has type (text / box / board), position (x & y), size (width & height) & text content (text nodes).
typedef struct {
    ui_node_type_t type;
    int x;
    int y;
    int w;
    int h;
    char text[UI_TEXT_MAX];
    // generic widget metadata. e.g. type="grid" id="orders"
    char widget_type[UI_WIDGET_TYPE_MAX];
    char widget_id[UI_WIDGET_ID_MAX];
} ui_node_t; // i.e. for each element in template, we have a node_struct with all the info
// ^ extensibility, better dataa storage (e.g: storing list of nodes in aarraay)
// my a button on my keyboaaard is broken or something 

// template is like for each html file
typedef struct {
    ui_node_t nodes[UI_MAX_NODES]; 
    int count; // for allocating space 
} ui_template_t;

typedef struct {
    char key[UI_BIND_KEY_MAX];
    char value[UI_BIND_VALUE_MAX];
} ui_binding_kv_t; // for each key-vaalaue pair, we have a struct with key aand value

// dynamic binding table used by ui_template_render.
// basically a list of ui_binding_kv_t structs, with aa count of how many kv pairs in list
typedef struct {
    ui_binding_kv_t items[UI_BIND_MAX];
    int count;
} ui_bindings_t;

// runtime ncurses bootstrap policy.
// lets other projects reuse the same markup renderer without hardcoding tty mode.
typedef struct {
    int no_echo; // turn typed chars into nothing on screen.
    int cbreak_mode; // raw-ish input without waiting for enter.
    int cursor_visible; // 1 = show cursor, 0 = hide it.
    int keypad_enabled; // 1 = ncurses handles arrow keys etc.
    int nodelay_enabled; // 1 = getch returns immediately when no input is ready.
    int init_colors; // 1 = set up the color pairs used by renderer.
    int use_default_colors; // 1 = let ncurses use terminal default bg.
} ui_markup_config_t;

// FUNCTIONS

// init html file
void ui_template_init(ui_template_t *tpl);

// init binding table to empty.
void ui_bindings_init(ui_bindings_t *bind);

// insert/update a binding value by key. returns 1 on success, 0 on failure.
int ui_bindings_set(ui_bindings_t *bind, const char *key, const char *value);

// get binding value for key. returns NULL if key is missing.
const char *ui_bindings_get(const ui_bindings_t *bind, const char *key);

// load file from path into template struct at tpl. 1 = success, 0 = fail
int ui_template_load_from_file(const char *path, ui_template_t *tpl);

// fill config with defaults used by current tetrisu renderer.
// this keeps current behavior as the default path.
void ui_markup_config_default_game(ui_markup_config_t *cfg);

// fill config with defaults useful for a typing-friendly admin panel.
// noecho stays off so the panel can accept visible text input.
void ui_markup_config_default_admin(ui_markup_config_t *cfg);

// initialize renderer with caller-provided config.
// returns 1 on success, 0 on failure.
// strict behavior: cannot reconfigure while ncurses is already active.
int ui_markup_init_with_config(const ui_markup_config_t *cfg);

// render all primitives (text + box).
// board/grid is rendered by caller as its own widget path.
void ui_template_render(const ui_template_t *tpl, const ui_bindings_t *bind);

// generic lookup: pass NULL for type/id to skip that filter.
int ui_template_find_widget(
    const ui_template_t *tpl,
    const char *widget_type,
    const char *widget_id,
    int *x_out,
    int *y_out);

// explicit renderer-side cleanup for ncurses mode.
// call this before input shutdown so final tty mode restore order is stable.
void ui_markup_shutdown(void);

#endif
