#include <stdio.h>
#include <string.h>

#include "mongo_adapter.h"

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

// extract string from ObjectId() (from input) to output
static void normalize_object_id(const char *input, char *output, size_t output_cap)
{
    // checks
    if (!output || output_cap == 0) return;
    output[0] = '\0';
    if (!input) return;

    // get the pre and suffix 
    const char *prefix = "ObjectId('";
    const char *suffix = "')";
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t input_len = strlen(input);

    // check if : input string (with object()...) is longer than the pre and suffix,
    if (input_len > prefix_len + suffix_len &&
        // check if input starts with prefix and ends with suffix
        // notes : strncmp compares first n chars, strcmp compares until null terminator
        strncmp(input, prefix, prefix_len) == 0 &&
        strcmp(input + input_len - suffix_len, suffix) == 0) {
        // yes? extraact hex id 
        size_t hex_len = input_len - prefix_len - suffix_len;
        // shouldn't be the case, but just in case, check if hex len exceeds buffer cap
        if (hex_len >= output_cap) hex_len = output_cap - 1;
        // return the input offset by prefix_len (get rid of ObjectId('...)) and copy
        // exactly hex_len (length of hex) chars into output
        memcpy(output, input + prefix_len, hex_len);
        // add null terminator
        output[hex_len] = '\0';
        return;
    }

    snprintf(output, output_cap, "%s", input);
}

// declaration for function defined later. for compiler to verify the call
static void summarize_document(const char *collection, const char *doc, char *out, size_t out_cap);

// append "label=value" segment into output, with " | " between segments.
// returns 1 if segment was appended, else 0.
static int append_summary_part(char *out, size_t out_cap, int *is_first, const char *label, const char *value)
{
    if (!out || !is_first || !label || !value || out_cap == 0) return 0;

    // current length of output. If at capacity, return
    size_t used = strlen(out);
    if (used >= out_cap - 1) return 0;

    // written = num of chars written by snprintf (below) --> negative = error, >= out_cap - used = truncated...
    int written = 0;
    // write to out + used (used is current offset of end of string)
    if (*is_first) {
        written = snprintf(out + used, out_cap - used, "%s=%s", label, value);
    } else {
        written = snprintf(out + used, out_cap - used, " | %s=%s", label, value);
    }

    // error or truncated, return 0 
    if (written < 0 || (size_t)written >= out_cap - used) return 0;
    *is_first = 0;
    return 1;
}

// string of docs separated by ;, fill rows array
static void fill_document_rows(const char *collection, const char *src, char rows[][69], size_t row_count)
{
    if (!rows || row_count == 0) return;

    // clear all rows
    for (size_t i = 0; i < row_count; i++) {
        rows[i][0] = '\0';
    }

    // check if source NULL
    if (!src) return;

    const char *p = src;
    size_t row = 0;

    // while pointer to souce string is not null and there are still rows, to fill,
    while (*p && row < row_count) {
        // skip whitespace and semicolons
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ';') {
            p++; // move pointer forward
        }
       // if pointer is null or empty, break out of loop
        if (!*p) break;

        // find end of current doc
        const char *end = p;
        while (*end && *end != ';') {
            end++;
        }
        // at this point, {p, end} represents current doc. Start at p, stop before end

        // copy current doc into raw buffer
        char raw[1024]; // store current doc (with null terminator)
        char summary[69] = {0}; // hold short version
        size_t len = (size_t)(end - p); // get length of current doc and use it to copy into raw buffer
        if (len >= sizeof(raw)) len = sizeof(raw) - 1;
        memcpy(raw, p, len);
        raw[len] = '\0';

        // pass that raw doc into summarize_doc, put return value into summary buffer and
        // copy that into rows[row]
        summarize_document(collection, raw, summary, sizeof(summary));
        // dest, dest size, format, format is %.*s (string with max chars to copy)
        // 68 = max chars to copy, copy summary. leave 1 space for null terminator
        snprintf(rows[row], 69, "%.*s", 68, summary); 
        // snprintf automatically adds null terminator. Just leave a space for it.

        row++; // next row
        p = end; // move pointer to end of current doc
    }
}

// get hex id from ObjectId() string.
static int extract_object_id(const char *doc, char *out, size_t out_cap)
{
    // "$oid": "66b0f2f6a3e4c4f2d6f9a123"
    if (!doc || !out || out_cap == 0) return 0;

    // find $oid in doc, find colon after it, skip whitespace quotes braces and colons...
    const char *p = strstr(doc, "\"$oid\"");
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;

    while (*p == ' ' || *p == '\t' || *p == '{' || *p == '"' || *p == ':') p++;

    // find end quote, check if valid
    const char *end = strchr(p, '"');
    if (!end || end <= p) return 0;

    // p points to start of hex, end to end of hex. We get length of hex using end-p,
    // check if length exceeds output buffer, copy hex into output and add null terminator
    // i.e. in a string "$oid": "12345678", p points to 1, end points to '"' right after 8, end-p=8
    // so we copy 8 chars from p to output, then add null terminator.
    size_t len = (size_t)(end - p);
    if (len >= out_cap) len = out_cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

static int extract_json_string(const char *doc, const char *key, char *out, size_t out_cap)
{
    if (!doc || !key || !out || out_cap == 0) return 0;

    // needle is the formatted key (status --> "status")
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    // find key in doc, find colon after it, skip all whitespace or \t
    const char *p = strstr(doc, needle);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;

    while (*p == ' ' || *p == '\t') p++;
    // after skipping, if next char is quote, skip
    if (*p == '"') p++;

    // get end quote as delimiter, check if valid.
    const char *end = strchr(p, '"');
    if (!end || end <= p) return 0;

    // refer to above
    size_t len = (size_t)(end - p);
    if (len >= out_cap) len = out_cap - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

static int extract_json_number(const char *doc, const char *key, char *out, size_t out_cap)
{
    if (!doc || !key || !out || out_cap == 0) return 0;

    // same thing as string, refer above
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(doc, needle);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;

    while (*p == ' ' || *p == '\t') p++;

    // mongodb has different formats for numbers.
    // sometimes it's "total": { "$numberDouble": "12345.67"}
    if (*p == '{') {
        // check for valid formats
        const char *number = strstr(p, "\"$numberDouble\"");
        if (!number) number = strstr(p, "\"$numberInt\"");
        if (!number) number = strstr(p, "\"$numberLong\"");
        if (!number) number = strstr(p, "\"$numberDecimal\"");
        if (!number) return 0; // FAIL

        // then skip the colon and whitespace, then skip the quote, then find the end quote
        number = strchr(number, ':');
        if (!number) return 0;
        number++;

        while (*number == ' ' || *number == '\t') number++;
        if (*number == '"') number++;

        const char *end = strchr(number, '"');
        if (!end || end <= number) return 0;

        // same thing as above
        size_t len = (size_t)(end - number);
        if (len >= out_cap) len = out_cap - 1;
        memcpy(out, number, len);
        out[len] = '\0';
        return 1;
    }

    // not {? then just number. Skip quote if any (sometimes it's "total": 123, or "total": "123")
    if (*p == '"') p++;

    // tmp buffer to hold number string, copy chars until we hit ", ,, or } or end of string or exceed buffer (shouldn't happen)
    char tmp[32] = {0};
    size_t w = 0;
    // +1 to make space for null terminator
    while (*p && *p != '"' && *p != ',' && *p != '}' && w + 1 < sizeof(tmp)) {
        tmp[w++] = *p++;
    }
    // null terminator
    tmp[w] = '\0';

    // copy tmp into output buffer
    snprintf(out, out_cap, "%s", tmp);
    // 1 if tmp not empty
    return tmp[0] != '\0';
}

// summary : "id=123 | status=shipped | total=45.67"
static void summarize_document(const char *collection, const char *doc, char *out, size_t out_cap)
{
    // checks
    if (!out || out_cap == 0) return;
    out[0] = '\0';
    if (!doc) return;

    // values we may waant to extraaact from docaaaaaaaaaaaa
    char id[40] = {0};
    char status[32] = {0};
    char total[24] = {0};
    char name[40] = {0};
    char username[40] = {0};
    int first = 1;

    // extract the vaalues
    (void)extract_object_id(doc, id, sizeof(id));
    (void)extract_json_string(doc, "status", status, sizeof(status));
    (void)extract_json_number(doc, "total", total, sizeof(total));
    (void)extract_json_string(doc, "name", name, sizeof(name));
    (void)extract_json_string(doc, "username", username, sizeof(username));

    // vendors: keep the row shape consistent.
    if (collection && strcmp(collection, "vendors") == 0) {
        append_summary_part(out, out_cap, &first, "id", id[0] ? id : "(missing)");
        append_summary_part(out, out_cap, &first, "name", name[0] ? name : "(missing)");
        return;
    }

    // users: show the username field prominently for account rows.
    if (collection && strcmp(collection, "users") == 0) {
        append_summary_part(out, out_cap, &first, "id", id[0] ? id : "(missing)");
        append_summary_part(out, out_cap, &first, "username", username[0] ? username : "(missing)");
        return;
    }

    // orders: same idea, always show id + status + total.
    if (collection && strcmp(collection, "orders") == 0) {
        append_summary_part(out, out_cap, &first, "id", id[0] ? id : "(missing)");
        append_summary_part(out, out_cap, &first, "status", status[0] ? status : "(missing)");
        append_summary_part(out, out_cap, &first, "total", total[0] ? total : "(missing)");
        return;
    }

    // fallback for any other collection. Append if not empty
    if (id[0] != '\0') append_summary_part(out, out_cap, &first, "id", id);
    if (name[0] != '\0') append_summary_part(out, out_cap, &first, "name", name);
    if (username[0] != '\0') append_summary_part(out, out_cap, &first, "username", username);
    if (status[0] != '\0') append_summary_part(out, out_cap, &first, "status", status);
    if (total[0] != '\0') append_summary_part(out, out_cap, &first, "total", total);

    // if no parts aappended (first was never set to 0), then no details
    if (first) {
        snprintf(out, out_cap, "(no details)");
    }
}

// temp default template
static void load_default_template(ui_template_t *tpl)
{
    ui_template_init(tpl);
    tpl->count = 12;

    tpl->nodes[0].type = UI_NODE_TEXT;
    tpl->nodes[0].x = 2;
    tpl->nodes[0].y = 1;
    snprintf(tpl->nodes[0].text, sizeof(tpl->nodes[0].text), "Daizoubu Admin");

    tpl->nodes[1].type = UI_NODE_BOX;
    tpl->nodes[1].x = 1;
    tpl->nodes[1].y = 3;
    tpl->nodes[1].w = 70;
    tpl->nodes[1].h = 20;

    tpl->nodes[2].type = UI_NODE_TEXT;
    tpl->nodes[2].x = 3;
    tpl->nodes[2].y = 5;
    snprintf(tpl->nodes[2].text, sizeof(tpl->nodes[2].text), "status={{status}}");

    tpl->nodes[3].type = UI_NODE_TEXT;
    tpl->nodes[3].x = 3;
    tpl->nodes[3].y = 7;
    snprintf(tpl->nodes[3].text, sizeof(tpl->nodes[3].text), "collection={{collection}}");

    tpl->nodes[4].type = UI_NODE_TEXT;
    tpl->nodes[4].x = 3;
    tpl->nodes[4].y = 9;
    snprintf(tpl->nodes[4].text, sizeof(tpl->nodes[4].text), "");

    tpl->nodes[5].type = UI_NODE_TEXT;
    tpl->nodes[5].x = 3;
    tpl->nodes[5].y = 10;
    snprintf(tpl->nodes[5].text, sizeof(tpl->nodes[5].text), "{{documents_0}}");

    tpl->nodes[6].type = UI_NODE_TEXT;
    tpl->nodes[6].x = 3;
    tpl->nodes[6].y = 11;
    snprintf(tpl->nodes[6].text, sizeof(tpl->nodes[6].text), "{{documents_1}}");

    tpl->nodes[7].type = UI_NODE_TEXT;
    tpl->nodes[7].x = 3;
    tpl->nodes[7].y = 12;
    snprintf(tpl->nodes[7].text, sizeof(tpl->nodes[7].text), "{{documents_2}}");

    tpl->nodes[8].type = UI_NODE_TEXT;
    tpl->nodes[8].x = 3;
    tpl->nodes[8].y = 13;
    snprintf(tpl->nodes[8].text, sizeof(tpl->nodes[8].text), "{{documents_3}}");

    tpl->nodes[9].type = UI_NODE_TEXT;
    tpl->nodes[9].x = 3;
    tpl->nodes[9].y = 14;
    snprintf(tpl->nodes[9].text, sizeof(tpl->nodes[9].text), "{{documents_4}}");

    tpl->nodes[10].type = UI_NODE_TEXT;
    tpl->nodes[10].x = 3;
    tpl->nodes[10].y = 16;
    snprintf(tpl->nodes[10].text, sizeof(tpl->nodes[10].text), "message={{message}}");

    tpl->nodes[11].type = UI_NODE_TEXT;
    tpl->nodes[11].x = 3;
    tpl->nodes[11].y = 18;
    snprintf(tpl->nodes[11].text, sizeof(tpl->nodes[11].text), "press q to quit");
}

int main(void)
{
    // init
    ui_markup_config_t cfg;
    ui_template_t tpl;
    ui_bindings_t bind;
    mongo_client_t mongo_client;
    mongo_config_t mongo_cfg;
    char documents[8192] = {0};
    char document_rows[5][69] = {{0}};
    char message[256] = {0};
    char current_collection[64] = "orders";
    int current_page = 1;
    int help_mode = 0;

    const char *help_text =
        "commands: list <collection>, next, prev, page <n>, "
        "status <order_objectid> <status>, refund <order_objectid>, help, quit";

    mongo_client_init(&mongo_client);
    mongo_config_init(&mongo_cfg);
    mongo_config_load_from_env(&mongo_cfg);
    mongo_client_connect(&mongo_client, &mongo_cfg);

    // start with the default admin panel config (set cfg object to default admin panel vals)
    ui_markup_config_default_admin(&cfg);
    // init ncurses with above config
    if (!ui_markup_init_with_config(&cfg)) {
        fprintf(stderr, "failed to initialize ui markup\n");
        return 1;
    }

    // load template from file. If fail, use above function
    if (!ui_template_load_from_file("ui/admin.ui", &tpl)) {
        fprintf(stderr, "debug: ui/admin.ui not found or unreadable; using built-in fallback template\n");
        load_default_template(&tpl);
    } else {
        fprintf(stderr, "debug: ui/admin.ui loaded successfully\n");
    }

    // init binding object
    ui_bindings_init(&bind);

    // main loop
    while (1) {
        if (help_mode) {
            snprintf(document_rows[0], sizeof(document_rows[0]), "%.*s", (int)(sizeof(document_rows[0]) - 1), help_text);
            for (size_t i = 1; i < 5; i++) {
                document_rows[i][0] = '\0';
            }
        } else {
            if (!mongo_client_list_documents(&mongo_client, current_collection, current_page, 5, documents, sizeof(documents))) {
                snprintf(documents, sizeof(documents), "query unavailable");
            }
            fill_document_rows(current_collection, documents, document_rows, 5);
        }

        // update bindings
        (void)ui_bindings_set(&bind, "status", mongo_client_status(&mongo_client));
        (void)ui_bindings_set(&bind, "collection", current_collection);
        (void)ui_bindings_set(&bind, "documents_0", document_rows[0]);
        (void)ui_bindings_set(&bind, "documents_1", document_rows[1]);
        (void)ui_bindings_set(&bind, "documents_2", document_rows[2]);
        (void)ui_bindings_set(&bind, "documents_3", document_rows[3]);
        (void)ui_bindings_set(&bind, "documents_4", document_rows[4]);
        (void)ui_bindings_set(&bind, "message", message);

        ui_template_render(&tpl, &bind);
        refresh();

        // get user input
        move(21, 3);
        clrtoeol();
        printw("daizoubu cmd> ");
        refresh();

        echo();
        char input[256] = {0};
        getstr(input);
        noecho();

        // check commands...
        if (strcmp(input, "q") == 0 || strcmp(input, "quit") == 0) {
            break;
        }

        // help command, just display text
        if (strcmp(input, "help") == 0) {
            help_mode = 1;
            snprintf(message, sizeof(message), "use the orders _id ObjectId hex for status/refund");
            continue;
        }

        help_mode = 0;

        // status command. We compare the first 7 chars to see if it starts with "status " with a space
        if (strncmp(input, "status ", 7) == 0) {
            // placeholder array vals
            char id[64] = {0};
            char status_value[64] = {0};
            // sscanf : read from input string in "status %63s %63s" format,
            // store first string in id, second in status_value.
            // %63s means read up to 63 chars (leave space for null terminator /0)
            // sscanf returns num of successfully read items, hence the ==2
            if (sscanf(input, "status %63s %63s", id, status_value) == 2) {
                char object_id[64] = {0};
                normalize_object_id(id, object_id, sizeof(object_id));
                // if successfully read both id and status values, call mongo client update function
                // return 1 on success, if successful, change message to "updated <status_value>"
                if (mongo_client_update_order_status(&mongo_client, object_id, status_value, message, sizeof(message))) {
                    snprintf(message, sizeof(message), "updated %s", status_value);
                }
            } else {
                // if incorrectly formatted, show intended usage messaage.
                snprintf(message, sizeof(message), "usage: status <order_id> <status>");
            }
            continue;
        }

        // refund cmd, same logic as above
        if (strncmp(input, "refund ", 7) == 0) {
            char id[64] = {0};
            if (sscanf(input, "refund %63s", id) == 1) {
                // correct usage, call function.
                // not implemented yet, so just set message to "refund processed" for now.
                snprintf(message, sizeof(message), "refund processed");
                // if (mongo_client_refund_order(&mongo_client, id, message, sizeof(message))) {
                //     snprintf(message, sizeof(message), "refund processed");
                // }
            } else {
                snprintf(message, sizeof(message), "usage: refund <order_id>");
            }
            continue;
        }

        // list
        if (strncmp(input, "list ", 5) == 0) {
            char collection[64] = {0};
            // get collection name first from input, store in collection
            if (sscanf(input, "list %63s", collection) == 1) {
                // set current_collection to new collection name, reset page to 1
                snprintf(current_collection, sizeof(current_collection), "%s", collection);
                current_page = 1;
                snprintf(message, sizeof(message), "listing %s", current_collection);
            }
            continue;
        }

        // pagination function
        if (strcmp(input, "next") == 0) {
            current_page++;
            // change current page index and update message to show the current page
            // page itself is displayed in the next iteration with updated docs from the new page number
            snprintf(message, sizeof(message), "page %d", current_page);
            continue;
        }

        if (strcmp(input, "prev") == 0) {
            if (current_page > 1) current_page--;
            snprintf(message, sizeof(message), "page %d", current_page);
            continue;
        }

        // currently no validation for page num
        if (strncmp(input, "page ", 5) == 0) {
            int page = 1;
            if (sscanf(input, "page %d", &page) == 1) {
                current_page = page;
                snprintf(message, sizeof(message), "page %d", current_page);
            }
            continue;
        }

        snprintf(message, sizeof(message), "unknown command");
    }

    // cleanup
    mongo_client_disconnect(&mongo_client);
    ui_markup_shutdown();
    return 0;
}
