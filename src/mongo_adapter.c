#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mongo_adapter.h"

#ifdef _WIN32
#include <bson/bson.h>
#include <mongoc/mongoc.h>
#else
#include <bson/bson.h>
#include <mongoc/mongoc.h>
#endif

static const char *default_uri = "mongodb+srv://lucassngweijun_db_user:w1bDiSjhc4hNsNXG@daizoubot.vlz42qu.mongodb.net/";
static const char *default_database = "daizoubu";

// init mongo config with default values
void mongo_config_init(mongo_config_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->uri, sizeof(cfg->uri), "%s", default_uri);
    snprintf(cfg->database, sizeof(cfg->database), "%s", default_database);
}

// init mongo config with values from environment variables
void mongo_config_load_from_env(mongo_config_t *cfg)
{
    if (!cfg) return;

    mongo_config_init(cfg);

    // getenv loads from environment variables
    const char *uri = getenv("MONGO_URI");
    const char *db = getenv("MONGO_DATABASE");

    // check if env variables exist and not empty
    if (uri && uri[0] != '\0') {
        snprintf(cfg->uri, sizeof(cfg->uri), "%s", uri);
    }

    if (db && db[0] != '\0') {
        snprintf(cfg->database, sizeof(cfg->database), "%s", db);
    }
}

// init mongo client with default values
void mongo_client_init(mongo_client_t *client)
{
    if (!client) return;
    memset(client, 0, sizeof(*client));
    mongo_config_init(&client->config);
    snprintf(client->status, sizeof(client->status), "not connected");
}

// connect to mongo with given cfg
int mongo_client_connect(mongo_client_t *client, const mongo_config_t *cfg)
{
    // check if client is valid (above)
    if (!client) return 0;

    // if there's a config provided, use. 
    if (cfg) {
        client->config = *cfg;
    } else {
        // no config? load from 
        mongo_config_load_from_env(&client->config);
    }

    // reset connection state before attempt
    client->connected = 0;
    client->native_client = NULL;
    snprintf(client->status, sizeof(client->status), "connecting");

    // initialize the MongoDB C driver
    mongoc_init();

    // create a new client instance, attempt to connect to MongoDB with the URI in config
    mongoc_client_t *native = mongoc_client_new(client->config.uri);
    if (!native) {
        // oops
        snprintf(client->status, sizeof(client->status), "connect failed");
        return 0;
    }

    // stores pointer to native client in client struct and update connected status.
    client->native_client = native;
    client->connected = 1;
    snprintf(client->status, sizeof(client->status), "connected");
    return 1;
}

void mongo_client_disconnect(mongo_client_t *client)
{
    if (!client) return;
    // if native client exists (assigned in mongo_client_connect), destroy it
    if (client->native_client) {
        mongoc_client_destroy((mongoc_client_t *)client->native_client);
    }
    // update connected status and turn off native client pointer
    client->connected = 0;
    client->native_client = NULL;
    snprintf(client->status, sizeof(client->status), "disconnected");
}

// retrieve from mongo
int mongo_client_list_documents(mongo_client_t *client, const char *collection, int page, int page_size, char *out, size_t out_cap)
{
    // checks
    if (!client || !client->native_client || !collection || !out || out_cap == 0) return 0;
    if (!client->connected) {
        snprintf(out, out_cap, "not connected");
        return 0;
    }

    // handle invalid page stuff
    if (page < 1) page = 1;
    if (page_size < 1) page_size = 5;

    // naative client pointer from our struct
    mongoc_client_t *native = (mongoc_client_t *)client->native_client;
    // get collection
    mongoc_collection_t *coll = mongoc_client_get_collection(native, client->config.database, collection);
    if (!coll) {
        snprintf(out, out_cap, "collection unavailable");
        return 0;
    }

    // bson_t --> BSON = binary JSON. not really sure exactly what it is
    bson_t *filter = bson_new();
    // options for limit and skip for pagination. skip = start from index (page-1)*pagesize, limit is size
    bson_t *opts = BCON_NEW("limit", BCON_INT32(page_size), "skip", BCON_INT32((page - 1) * page_size));
    // find documents wwith filter (empty) and options.
    // returns a cursor to iterate over results
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(coll, filter, opts, NULL);
    // destroy filter and opts after use
    bson_destroy(filter);
    bson_destroy(opts);

    size_t len = 0;
    int first = 1;
    const bson_t *doc = NULL;

    // iterate over cursor
    while (mongoc_cursor_next(cursor, &doc)) {
        // convert bson doc to JSON.
        char *json = bson_as_canonical_extended_json(doc, NULL);
        if (!json) continue; // if conversion fails, skip

        // check if adding json would exceed buffer capacity
        size_t json_len = strlen(json);
        if (len + json_len + 2 >= out_cap) {
            bson_free(json);
            break;
        }

        // if not first doc, add separator
        if (!first) {
            out[len++] = ';';
            out[len++] = ' ';
        }

        // copy json into output pointer + offset len 
        memcpy(out + len, json, json_len);
        // update offset pointer len
        len += json_len;
        // add terminator
        out[len] = '\0';
        // mark that we have <= 1 doc
        first = 0;
        // destroy json string after use
        bson_free(json);
    }

    // no doc found. If there was, first would've been set to 0 after
    if (first) {
        // write no docs found to output buffer
        snprintf(out, out_cap, "no documents found");
    }

    // destroy after use, return success
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(coll);
    return 1;
}

// update order status in mongo
int mongo_client_update_order_status(mongo_client_t *client, const char *order_id, const char *new_status, char *out, size_t out_cap)
{
    // checks
    if (!client || !client->native_client || !order_id || !new_status || !out || out_cap == 0) return 0;
    if (!client->connected) {
        snprintf(out, out_cap, "not connected");
        return 0;
    }

    // ZUTOMAYO
    // same as above
    mongoc_client_t *native = (mongoc_client_t *)client->native_client;
    mongoc_collection_t *coll = mongoc_client_get_collection(native, client->config.database, "orders");
    if (!coll) {
        snprintf(out, out_cap, "orders collection unavailable");
        return 0;
    }

    // buffers 
    char filter_json[256];
    char update_json[256];
    // set filter to filter by order id, and update to store new status
    snprintf(filter_json, sizeof(filter_json), "{\"_id\":{\"$oid\":\"%s\"}}", order_id);
    snprintf(update_json, sizeof(update_json), "{\"$set\":{\"status\":\"%s\"}}", new_status);

    // in case 
    bson_error_t error;
    // convert to bson objects, and if fail, write to error buffer 
    bson_t *filter = bson_new_from_json((const uint8_t *)filter_json, -1, &error);
    bson_t *update = bson_new_from_json((const uint8_t *)update_json, -1, &error);
    // something went wrong
    if (!filter || !update) {
        snprintf(out, out_cap, "invalid update payload");
        bson_destroy(filter);
        bson_destroy(update);
        mongoc_collection_destroy(coll);
        return 0;
    }

    // perform update
    bool ok = mongoc_collection_update_one(coll, filter, update, NULL, NULL, &error);
    // if successful, write to output buffer, else write error to output buffer
    // destroy and clean up
    bson_destroy(filter);
    bson_destroy(update);
    mongoc_collection_destroy(coll);

    // unsuccessful
    if (!ok) {
        snprintf(out, out_cap, "update failed: %s", error.message);
        return 0;
    }

    snprintf(out, out_cap, "status updated to %s", new_status);
    return 1;
}

const char *mongo_client_status(const mongo_client_t *client)
{
    return client ? client->status : "unavailable";
}

