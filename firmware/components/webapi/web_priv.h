// Shared inside components/webapi.
#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "mongoose.h"

#define WEB_MAX_CONNS 8    // web connections at once; one more is closed at accept
#define WEB_SEND_HIGH 4096 // a streamed answer adds its next piece only below this (spec §9.4)

// The running values, read at boot (spec §9.1): /api/device reports these, not what is saved.
typedef struct {
    char hostname[17];
    char token[64];  // base64("user:password")
    int web_port, ws_port, ws_target;
} web_boot_t;
extern web_boot_t g_web;

void api_json(struct mg_connection *c, int status, cJSON *doc);  // sends and deletes doc
void api_error(struct mg_connection *c, int status, const char *message);
void api_ok(struct mg_connection *c);
/** A non-streamed /api request, already authorized, body complete (at most WEB_BODY_MAX). */
void api_handle(struct mg_connection *c, struct mg_http_message *hm);

int console_start(int port, int target);
/** The `status` line of the console's LOG mode. */
void console_status_line(char *out, size_t cap);
