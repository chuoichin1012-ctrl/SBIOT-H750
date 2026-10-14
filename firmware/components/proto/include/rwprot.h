// rw_prot: the JSON query/control protocol of the reference unit (USR-M100
// manual §9.5.1, §10.6.4), so servers written for it work unchanged.
// Pure C (+cJSON), host-tested.
//
//   request  {"rw_prot":{"Ver":"1.0.1","dir":"down","id":"12345",
//             "r_data":[{"name":"t"}],"w_data":[{"name":"c","value":"15"}]}}
//   reply    {"rw_prot":{"Ver":"1.0.1","dir":"up","id":"12345",
//             "r_data":[{"name":"t","value":"21.5","err":"0"}],
//             "w_data":[{"name":"c","value":"15","err":"0"}]}}
//   bad header (Ver/dir/id) or no valid r_data/w_data:
//            {"rw_prot":{"Ver":"1.0.1","dir":"up","id":"..","err":"1"}}
// "Ver" or "ver" is accepted; values may be strings or numbers; at most 5
// reads and 5 writes per request (further entries get err "1").
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define RWPROT_MAX 5

/** Current value of `name` as text; false when unknown or unread (err "1"). */
typedef bool (*rwprot_read_fn)(void *ctx, const char *name, char *value, size_t max);
/** Writes `value` to `name`; false when refused or failed (err "1"). NULL = writes disabled. */
typedef bool (*rwprot_write_fn)(void *ctx, const char *name, const char *value);

/** The reply (malloc'd), or NULL when `req` is not an rw_prot message at all. */
char *rwprot_handle(const char *req, size_t len, rwprot_read_fn rd, rwprot_write_fn wr, void *ctx);
