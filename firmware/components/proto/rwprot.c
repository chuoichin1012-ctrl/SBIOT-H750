#include "rwprot.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"

static cJSON *header(const char *id)
{
    cJSON *doc = cJSON_CreateObject();
    cJSON *p = cJSON_AddObjectToObject(doc, "rw_prot");
    cJSON_AddStringToObject(p, "Ver", "1.0.1");
    cJSON_AddStringToObject(p, "dir", "up");
    if (id) cJSON_AddStringToObject(p, "id", id);
    return doc;
}

static char *finish(cJSON *doc)
{
    char *s = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    return s;
}

/** A value given as a string or a number, as text. */
static bool as_text(const cJSON *v, char *out, size_t max)
{
    if (cJSON_IsString(v)) return snprintf(out, max, "%s", v->valuestring) < (int)max;
    if (cJSON_IsNumber(v)) return snprintf(out, max, "%.10g", v->valuedouble) < (int)max;
    if (cJSON_IsBool(v)) return snprintf(out, max, "%d", cJSON_IsTrue(v)) < (int)max;
    return false;
}

char *rwprot_handle(const char *req, size_t len, rwprot_read_fn rd, rwprot_write_fn wr, void *ctx)
{
    cJSON *in = cJSON_ParseWithLength(req, len);
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(in, "rw_prot");
    if (!cJSON_IsObject(p)) {
        cJSON_Delete(in);
        return NULL;  // not ours: let the caller pass it on
    }
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(p, "Ver");
    if (!ver) ver = cJSON_GetObjectItemCaseSensitive(p, "ver");
    const cJSON *dir = cJSON_GetObjectItemCaseSensitive(p, "dir");
    char id[64] = "";
    bool has_id = as_text(cJSON_GetObjectItemCaseSensitive(p, "id"), id, sizeof id);
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(p, "r_data"), *w = cJSON_GetObjectItemCaseSensitive(p, "w_data");
    bool ok_head = cJSON_IsString(ver) && cJSON_IsString(dir) && strcmp(dir->valuestring, "down") == 0 && has_id;
    if (!ok_head || (!cJSON_IsArray(r) && !cJSON_IsArray(w))) {
        cJSON *doc = header(has_id ? id : NULL);
        cJSON_AddStringToObject(cJSON_GetObjectItem(doc, "rw_prot"), "err", "1");
        cJSON_Delete(in);
        return finish(doc);
    }

    cJSON *doc = header(id);
    cJSON *out = cJSON_GetObjectItem(doc, "rw_prot");
    char value[48];
    if (cJSON_IsArray(r)) {
        cJSON *list = cJSON_AddArrayToObject(out, "r_data");
        int n = 0;
        const cJSON *e;
        cJSON_ArrayForEach(e, r)
        {
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(e, "name"));
            cJSON *x = cJSON_CreateObject();
            cJSON_AddStringToObject(x, "name", name ? name : "");
            bool ok = name && ++n <= RWPROT_MAX && rd && rd(ctx, name, value, sizeof value);
            cJSON_AddStringToObject(x, "value", ok ? value : "");
            cJSON_AddStringToObject(x, "err", ok ? "0" : "1");
            cJSON_AddItemToArray(list, x);
        }
    }
    if (cJSON_IsArray(w)) {
        cJSON *list = cJSON_AddArrayToObject(out, "w_data");
        int n = 0;
        const cJSON *e;
        cJSON_ArrayForEach(e, w)
        {
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(e, "name"));
            bool have = as_text(cJSON_GetObjectItemCaseSensitive(e, "value"), value, sizeof value);
            cJSON *x = cJSON_CreateObject();
            cJSON_AddStringToObject(x, "name", name ? name : "");
            cJSON_AddStringToObject(x, "value", have ? value : "");
            bool ok = name && have && ++n <= RWPROT_MAX && wr && wr(ctx, name, value);
            cJSON_AddStringToObject(x, "err", ok ? "0" : "1");
            cJSON_AddItemToArray(list, x);
        }
    }
    cJSON_Delete(in);
    return finish(doc);
}
