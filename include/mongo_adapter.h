#ifndef MONGO_ADAPTER_H
#define MONGO_ADAPTER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// mongodb config struct
typedef struct {
    char uri[256];
    char database[64];
} mongo_config_t;

// just a wrapper with state
typedef struct {
    mongo_config_t config;
    char status[128];
    int connected;
    void *native_client;
} mongo_client_t;

void mongo_config_init(mongo_config_t *cfg);
void mongo_config_load_from_env(mongo_config_t *cfg);

int mongo_client_connect(mongo_client_t *client, const mongo_config_t *cfg);
void mongo_client_disconnect(mongo_client_t *client);
const char *mongo_client_status(const mongo_client_t *client);
void mongo_client_init(mongo_client_t *client);
int mongo_client_list_documents(mongo_client_t *client, const char *collection, int page, int page_size, char *out, size_t out_cap);
int mongo_client_update_order_status(mongo_client_t *client, const char *order_id, const char *new_status, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif
