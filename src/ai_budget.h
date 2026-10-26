/* Conservative sizing without a tokenizer or provider SDK. */
static unsigned ai_estimate(const char *text) {
    unsigned ascii = 0, unicode = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p < 128)
            ascii++;
        else if ((*p & 0xc0) != 0x80)
            unicode++;
    }
    return (ascii + 2) / 3 + unicode * 2 + 32;
}
static unsigned ai_budget_output(size_t count, bool summary) {
    return 128 + (unsigned)count * (summary ? 128 : 256);
}
static uint64_t ai_budget_key(const char *url, const char *model) {
    uint64_t hash = UINT64_C(14695981039346656037);
    const char *parts[] = {url, model};
    for (unsigned k = 0; k < 2; k++) {
        for (const unsigned char *p = (const unsigned char *)parts[k]; *p; p++)
            hash = (hash ^ *p) * UINT64_C(1099511628211);
        hash = (hash ^ 255) * UINT64_C(1099511628211);
    }
    return hash;
}
static unsigned ai_budget_input(unsigned context) {
    return context * 3 / 8;
}
static bool ai_pack(Library *l, Buffer *out, J *repos, bool summary, size_t description_limit) {
    J *items = yyjson_mut_arr(l->doc), *repo;
    size_t i, n;
    yyjson_mut_arr_foreach(repos, i, n, repo) {
        J *item = yyjson_mut_obj(l->doc);
        jnumber(l, item, "id", jnum(repo, "id"));
        jtext(l, item, "n", jstr(repo, "full_name"));
        const char *description =
            summary && !repo_has_material(repo) ? jstr(repo, "readme_excerpt") : jstr(repo, "description");
        if (*description) {
            Buffer text = {0};
            append_data_string(&text, description,
                               summary && !repo_has_material(repo) ? 2400 : description_limit);
            yyjson_doc *d = text.failed ? NULL : yyjson_read(text.data, text.len, 0);
            if (!d) {
                buf_drop(&text);
                return false;
            }
            jset(l, item, "d", yyjson_val_mut_copy(l->doc, yyjson_doc_get_root(d)));
            yyjson_doc_free(d);
            buf_drop(&text);
        }
        if (*jstr(repo, "language"))
            jtext(l, item, "l", jstr(repo, "language"));
        const char *from[] = {"topics", "tags"}, *to[] = {"t", "g"};
        for (unsigned k = 0; k < (summary ? 1u : 2u); k++) {
            J *values = yyjson_mut_arr(l->doc), *v;
            size_t x, count;
            yyjson_mut_arr_foreach(jget(repo, from[k]), x, count, v) {
                if (x == 8)
                    break;
                Buffer text = {0};
                append_data_string(&text, yyjson_mut_get_str(v), 128);
                yyjson_doc *d = text.failed ? NULL : yyjson_read(text.data, text.len, 0);
                if (!d) {
                    buf_drop(&text);
                    return false;
                }
                yyjson_mut_arr_append(values, yyjson_val_mut_copy(l->doc, yyjson_doc_get_root(d)));
                yyjson_doc_free(d);
                buf_drop(&text);
            }
            if (yyjson_mut_arr_size(values))
                jset(l, item, to[k], values);
        }
        yyjson_mut_arr_append(items, item);
    }
    char *json = yyjson_mut_val_write(items, 0, NULL);
    bool ok = json != NULL;
    if (json)
        buf_add(out, json);
    free(json);
    return ok && !out->failed;
}
