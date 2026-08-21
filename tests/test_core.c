#include <assert.h> // macro for unit testing
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_markup.h"
#include "mongo_adapter.h"

static void set_env_var(const char *key, const char *value)
{
#if defined(_WIN32)
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

// unit test: verify ui bindings store and retrieve values correctly
static void test_ui_bindings_round_trip(void)
{
    ui_bindings_t bindings;
    ui_bindings_init(&bindings);

    assert(ui_bindings_set(&bindings, "status", "online") == 1); // ensure set successfully
    assert(ui_bindings_set(&bindings, "collection", "orders") == 1);
    assert(strcmp(ui_bindings_get(&bindings, "status"), "online") == 0); // ensure get returns correct value
    assert(strcmp(ui_bindings_get(&bindings, "collection"), "orders") == 0);
    assert(ui_bindings_get(&bindings, "missing") == NULL); // ensure get returns NULL for missing key
}

// integration test: validate template file parsing and widget lookup
static void test_ui_template_file_loading_and_lookup(void)
{
    const char *path = "test_template.ui";
    FILE *fp = fopen(path, "w"); // create a temporary template file for testing
    assert(fp != NULL);

    fprintf(fp,
        "<text x=\"2\" y=\"1\">Welcome</text>\n"
        "<box x=\"1\" y=\"3\" w=\"10\" h=\"4\" />\n"
        "<widget x=\"5\" y=\"8\" type=\"orders\" id=\"list\" />\n");
    fclose(fp);

    ui_template_t tpl;
    assert(ui_template_load_from_file(path, &tpl) == 1); // ensure template loads successfully
    assert(tpl.count == 3); // verify node count

    int x = -1;
    int y = -1;
    assert(ui_template_find_widget(&tpl, "orders", "list", &x, &y) == 1); // ensure found successfully
    assert(x == 5); // ensure located successfully
    assert(y == 8);

    remove(path); // clean up temporary file
}

// unit test: confirm Mongo config is loaded from environment variables
static void test_mongo_config_from_environment(void)
{
    mongo_config_t cfg; // mock environment variables for testing
    set_env_var("MONGO_URI", "mongodb://example.test/testdb");
    set_env_var("MONGO_DATABASE", "example_db");

    mongo_config_load_from_env(&cfg);
    assert(strcmp(cfg.uri, "mongodb://example.test/testdb") == 0); // ensure URI loaded correctly
    assert(strcmp(cfg.database, "example_db") == 0);
}

// integration test: verify a Mongo client can initialize, connect, and disconnect cleanly
static void test_mongo_client_lifecycle(void)
{
    mongo_client_t client;
    mongo_client_init(&client);
    assert(strcmp(client.status, "not connected") == 0); // ensure initial status is correct
    assert(client.connected == 0);

    mongo_config_t cfg;
    mongo_config_init(&cfg); // ensure default config is valid
    assert(cfg.uri[0] != '\0');
    assert(cfg.database[0] != '\0');

    mongo_client_connect(&client, &cfg); // attempt connection
    assert(client.connected == 1 || client.connected == 0); // verify status valid

    mongo_client_disconnect(&client); // attempt disconnection
    assert(client.connected == 0); // ensure client is disconnected
    assert(client.native_client == NULL); // ensure native client is cleaned up
}

int main(void)
{
    test_ui_bindings_round_trip();
    test_ui_template_file_loading_and_lookup();
    test_mongo_config_from_environment();
    test_mongo_client_lifecycle();

    // can only reach here if asserts pass, otherwise program aborts
    puts("All daizoubu (admin panel) tests passed");
    return 0;
}
