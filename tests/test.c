#include "star.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <objbase.h>
#include <wincrypt.h>

static int failures, checks, requests, mode;
static LONG summary_fixture_calls;
static LONG classification_fixture_calls;
static LONG parallel_live, parallel_peak, parallel_started, private_seen;
static unsigned budget_calls, budget_max_items, budget_seen[5];
static unsigned parallel_limit;
static HANDLE parallel_barrier;
static uint64_t parallel_failed_id;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        InterlockedIncrement((LONG *)&checks);                                                               \
        if (!(x)) {                                                                                          \
            printf("FAIL line %d: %s\n", __LINE__, #x);                                                      \
            InterlockedIncrement((LONG *)&failures);                                                         \
        }                                                                                                    \
    } while (0)
enum {
    DISC_BALANCED = 10,
    DISC_SEARCH_FAIL,
    DISC_INCOMPLETE,
    DISC_EMPTY,
    DISC_CANCEL,
    DISC_BAD_FINAL,
    DISC_SHORT_QUERY,
    DISC_DUP_QUERY,
    DISC_BAD_QUERY,
    DISC_BAD_DIMENSION,
    DISC_NARROW_PLAN,
    DISC_PRIVATE_QUERY,
    DISC_RATE_LIMIT,
    DISC_COVERAGE,
    DISC_DUP_DIMENSION,
    DISC_ZERO_DIMENSION
};
static int discovery_searches, discovery_ai, pool_count, pool_routes[3];
static void reset_discovery_fixture(void) {
    discovery_searches = discovery_ai = pool_count = 0;
    memset(pool_routes, 0, sizeof(pool_routes));
}
static char *fixture_selections(const Request *req) {
    yyjson_doc *request = yyjson_read(req->body, strlen(req->body), 0);
    yyjson_val *messages = yyjson_obj_get(yyjson_doc_get_root(request), "messages");
    const char *content = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(messages, 1), "content"));
    yyjson_doc *packet = content ? yyjson_read(content, strlen(content), 0) : NULL;
    CHECK(packet != NULL);
    if (!packet) {
        yyjson_doc_free(request);
        return str_copy("{}");
    }
    yyjson_val *candidates = yyjson_obj_get(yyjson_doc_get_root(packet), "candidates"), *v;
    uint64_t ids[6] = {0};
    const char *lanes[6] = {0};
    yyjson_val *axes[6] = {0};
    yyjson_val *context = yyjson_obj_get(yyjson_doc_get_root(packet), "profile");
    CHECK(yyjson_arr_size(yyjson_obj_get(context, "dimensions")) == 6);
    CHECK(yyjson_is_arr(yyjson_obj_get(context, "not_interested")));
    CHECK(yyjson_get_len(yyjson_obj_get(context, "portrait")) > 0);
    int core = 0, adjacent = 0, selected = 0;
    size_t i, n;
    pool_count = (int)yyjson_arr_size(candidates);
    yyjson_arr_foreach(candidates, i, n, v) {
        uint64_t id = yyjson_get_uint(yyjson_obj_get(v, "id"));
        const char *lane = yyjson_get_str(yyjson_obj_get(v, "discovery_lane"));
        int route = id >= 10000 && id < 13000 ? (int)((id - 10000) / 1000) : -1;
        if (route >= 0)
            pool_routes[route]++;
        bool is_core = lane && !strcmp(lane, "core");
        if ((is_core && core == 4) || (!is_core && adjacent == 2))
            continue;
        ids[selected] = id;
        axes[selected] = yyjson_obj_get(v, "profile_dimensions");
        lanes[selected++] = lane;
        if (is_core)
            core++;
        else
            adjacent++;
    }
    Buffer answer = {0};
    buf_add(&answer, "{\"recommendations\":[");
    for (int k = selected - 1; k >= 0; k--) {
        if (k != selected - 1)
            buf_add(&answer, ",");
        buf_add(&answer, "{\"id\":");
        buf_num(&answer, ids[k]);
        buf_add(&answer,
                ",\"hook\":\"试一试 Windows "
                "小工具\",\"try_idea\":\"查看项目介绍，选择其中一种用途进行尝试。\",\"discovery_lane\":");
        buf_json_string(&answer, lanes[k]);
        char *dimensions = yyjson_val_write(axes[k], 0, NULL);
        buf_add(&answer, ",\"profile_dimensions\":");
        buf_add(&answer, dimensions ? dimensions : "[]");
        free(dimensions);
        buf_add(
            &answer,
            ",\"summary\":\"面向 Windows "
            "的实用工具。\",\"reason\":\"关联动手构建、效率偏好和信息秩序，具体功能以项目描述为依据。\"}");
    }
    buf_add(&answer, "]}");
    yyjson_doc_free(packet);
    yyjson_doc_free(request);
    return answer.data;
}
static const char *repo_api =
    "{\"id\":1001,\"full_name\":\"yftx888/Pulse\",\"description\":\"native "
    "desktop\",\"language\":\"C++\",\"stargazers_count\":99,\"topics\":[\"windows\"],\"private\":false,"
    "\"archived\":false,\"owner\":{\"id\":123,\"avatar_url\":\"https://avatars.githubusercontent.com/u/"
    "123?v=4\"},\"pushed_at\":\"2026-10-23T00:00:00Z\"}";
static const char *profile_json =
    "{\"summary\":\"原生工具\",\"interests\":[{\"name\":\"Windows\",\"reason\":\"Pulse "
    "描述和笔记\",\"confidence\":\"medium\",\"evidence\":[1001]}],\"preferences\":[]}";
static char *fixture_analysis(const Request *req, bool summaries) {
    yyjson_doc *d = yyjson_read(req->body, strlen(req->body), 0);
    yyjson_val *messages = yyjson_obj_get(yyjson_doc_get_root(d), "messages");
    const char *input = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(messages, 1), "content"));
    CHECK(input && !strstr(input, "LEGACY_GUIDANCE_SHOULD_NOT_BE_SENT"));
    CHECK(input && !strstr(input, "PRIVATE_OLD_FIELD_MUST_NOT_BE_SENT"));
    if (input && strstr(input, "PRIVATE_METADATA_MARKER"))
        InterlockedExchange(&private_seen, 1);
    yyjson_doc *packet = yyjson_read(input, strlen(input), 0);
    CHECK(packet != NULL);
    Buffer b = {0};
    buf_add(&b, summaries ? "{\"items\":[" : "{\"classifications\":[");
    size_t i, n;
    yyjson_val *r;
    yyjson_arr_foreach(yyjson_doc_get_root(packet), i, n, r) {
        if (i)
            buf_add(&b, ",");
        uint64_t id = yyjson_get_uint(yyjson_obj_get(r, "id"));
        const char *description = yyjson_get_str(yyjson_obj_get(r, "d"));
        bool material = (description && *description) || yyjson_arr_size(yyjson_obj_get(r, "t"));
        buf_add(&b, "{\"id\":");
        buf_num(&b, id);
        if (summaries) {
            buf_add(&b, material ? ",\"s\":\"用于本机桌面与开发工作的工具。\"}" : ",\"s\":null}");
        } else
            buf_add(&b, material ? ",\"sufficient\":true,\"axes\":[{\"dimension\":1,\"reason\":"
                                   "\"固定资料中的开发工具用途。\"}]}"
                                 : ",\"sufficient\":false,\"axes\":[]}");
    }
    buf_add(&b, "]}");
    yyjson_doc_free(packet);
    yyjson_doc_free(d);
    return b.data;
}
static bool fixture(const Request *req, Response *res, HANDLE cancel) {
    (void)cancel;
    InterlockedIncrement((LONG *)&requests);
    if (req->body && strstr(req->body, "任务：收藏兴趣分类"))
        InterlockedIncrement(&classification_fixture_calls);
    if (req->github) {
        CHECK(req->token && *req->token);
        CHECK(!strstr(req->url, "/users/"));
    }
    memset(res, 0, sizeof(*res));
    res->status = 200;
    if (mode >= 60 && mode <= 65 && req->body && strstr(req->body, "任务：中文项目概括")) {
        budget_calls++;
        yyjson_doc *d = yyjson_read(req->body, strlen(req->body), 0);
        yyjson_val *r = yyjson_doc_get_root(d);
        if (strstr(req->url, "api.deepseek.com"))
            CHECK(!strcmp(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(r, "thinking"), "type")), "disabled"));
        yyjson_val *messages = yyjson_obj_get(r, "messages");
        const char *text = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(messages, 1), "content"));
        yyjson_doc *input = yyjson_read(text, strlen(text), 0);
        size_t count = yyjson_arr_size(yyjson_doc_get_root(input));
        if (count > budget_max_items)
            budget_max_items = (unsigned)count;
        CHECK(count > 0 && count <= 20);
        CHECK(!strstr(text, "description") && !strstr(text, "tags") && !strstr(text, "avatar_url"));
        yyjson_val *cap = yyjson_obj_get(r, "max_tokens");
        if (!cap)
            cap = yyjson_obj_get(r, "max_completion_tokens");
        CHECK(yyjson_get_uint(cap) > 0 && yyjson_get_uint(cap) <= 3072);
        size_t i, n;
        yyjson_val *item;
        yyjson_arr_foreach(yyjson_doc_get_root(input), i, n, item) {
            uint64_t id = yyjson_get_uint(yyjson_obj_get(item, "id"));
            if (id >= 1001 && id <= 1005)
                budget_seen[id - 1001]++;
        }
        bool context = mode == 62 && count > 2;
        bool parameter = mode == 63 && yyjson_obj_get(r, "max_tokens");
        yyjson_doc_free(input);
        yyjson_doc_free(d);
        if (context || parameter || mode == 64) {
            res->status = mode == 64 ? 429 : 400;
            res->body = str_copy(context ? "{\"error\":{\"code\":\"context_length_exceeded\"}}"
                                         : "{\"error\":{\"message\":\"unsupported max_tokens\"}}");
            error_set(res->error, sizeof(res->error), "fixture request rejected");
            return false;
        }
        char *normal = fixture_analysis(req, true);
        const char *answer = mode == 61 && budget_calls == 1
                                 ? "{\"items\":[{\"id\":1001,\"s\":\"原生桌面工具。\"},{\"id\":"
                             : mode == 65 ? "{\"items\":[]}"
                                          : normal;
        Buffer b = {0};
        buf_add(&b, "{\"usage\":{\"prompt_tokens\":300,\"completion_tokens\":100},\"choices\":[{\"message\":{"
                    "\"content\":");
        buf_json_string(&b, answer);
        buf_add(&b, "}}]}");
        mem_free(normal);
        res->body = b.data;
        return true;
    }
    if (mode >= 40 && mode <= 44 && req->body &&
        (strstr(req->body, "任务：中文项目概括") || strstr(req->body, "任务：收藏兴趣分类"))) {
        LONG ordinal = InterlockedIncrement(&parallel_started), live = InterlockedIncrement(&parallel_live),
             peak;
        do {
            peak = parallel_peak;
        } while (live > peak && InterlockedCompareExchange(&parallel_peak, live, peak) != peak);
        if (live == (LONG)parallel_limit)
            SetEvent(parallel_barrier);
        CHECK(WaitForSingleObject(parallel_barrier, 5000) == WAIT_OBJECT_0);
        if (ordinal == 1) {
            yyjson_doc *d = yyjson_read(req->body, strlen(req->body), 0);
            yyjson_val *messages = yyjson_obj_get(yyjson_doc_get_root(d), "messages");
            const char *input = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(messages, 1), "content"));
            yyjson_doc *packet = yyjson_read(input, strlen(input), 0);
            parallel_failed_id =
                yyjson_get_uint(yyjson_obj_get(yyjson_arr_get_first(yyjson_doc_get_root(packet)), "id"));
            yyjson_doc_free(packet);
            yyjson_doc_free(d);
        }
        if (mode == 42 && ordinal == 1)
            SetEvent(cancel);
        Sleep(mode == 44 ? (ordinal == 1   ? 10
                            : ordinal == 2 ? 40
                                           : 60)
              : (mode == 41 || mode == 43)
                  ? (ordinal == 1 ? 10 : 100)
                  : (DWORD)(20 + (parallel_limit - (ordinal % parallel_limit)) * 10));
        bool fail = (mode == 41 && ordinal == 1) || (mode == 44 && ordinal == 2) ||
                    WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0;
        if (fail) {
            res->status = 429;
            error_set(res->error, sizeof(res->error), "fixture rate limited or canceled");
        } else {
            char *answer = fixture_analysis(req, strstr(req->body, "任务：中文项目概括") != NULL);
            Buffer b = {0};
            buf_add(&b, "{\"choices\":[{\"message\":{\"content\":");
            buf_json_string(&b, mode == 43 && ordinal == 1 ? "{\"summaries\":[]}" : answer);
            buf_add(&b, "}}]}");
            mem_free(answer);
            res->body = b.data;
        }
        InterlockedDecrement(&parallel_live);
        return !fail;
    }
    if (mode == 30 || (mode == 31 && req->github && !wcscmp(req->method, L"PUT"))) {
        res->status = mode == 30 ? 401 : 403;
        error_set(res->error, sizeof(res->error), "fixture denied");
        return false;
    }
    if (mode == 9) {
        res->status = 429;
        error_set(res->error, sizeof(res->error), "fixture rate limited");
        return false;
    }
    if (!strcmp(req->url, "https://api.github.com/user")) {
        res->body = str_copy(mode == 8 ? "{\"login\":\"other\"}" : "{\"login\":\"preview\"}");
        return true;
    }
    if (strstr(req->url, "/releases/latest")) {
        res->body = str_copy(mode == 71   ? "{\"tag_name\":\"v0.0.6\",\"prerelease\":true}"
                             : mode == 72 ? "{\"tag_name\":\"https://evil.example\"}"
                                          : "{\"tag_name\":\"v0.0.6\",\"draft\":false}");
        return true;
    }
    if (!strcmp(req->url, "https://api.github.com/repos/liuminxin45/star-library")) {
        res->body =
            str_copy("{\"id\":1430840985,\"full_name\":\"liuminxin45/star-library\",\"description\":\"GitHub "
                     "Stars manager\",\"topics\":[],\"stargazers_count\":100}");
        return true;
    }
    if (strstr(req->url, "/starred?")) {
        Buffer b = {0};
        buf_add(&b, "[{\"starred_at\":\"2026-10-23T00:00:00Z\",\"repo\":");
        buf_add(&b, repo_api);
        buf_add(&b, "}]");
        res->body = b.data;
        return true;
    }
    if (strstr(req->url, "/repos/")) {
        if (mode == 4 && strstr(req->url, "ibireme/yyjson")) {
            res->status = 404;
            error_set(res->error, sizeof(res->error), "repository no longer visible");
            return false;
        }
        const char *names[] = {"yftx888/Pulse", "ibireme/yyjson", "microsoft/terminal", "git/git",
                               "microsoft/PowerToys"};
        for (int k = 0; k < 5; k++) {
            if (strstr(req->url, names[k])) {
                Buffer b = {0};
                buf_add(&b, "{\"id\":");
                buf_num(&b, 1001 + k);
                buf_add(&b, "}");
                res->body = b.data;
                return true;
            }
        }
    }
    if (strstr(req->url, "/user/starred/")) {
        if (!wcscmp(req->method, L"GET")) {
            res->status = 404;
            error_set(res->error, sizeof(res->error), "not starred");
            return false;
        }
        CHECK(!wcscmp(req->method, L"PUT") || !wcscmp(req->method, L"DELETE"));
        res->status = 204;
        res->body = str_copy("");
        return true;
    }
    if (strstr(req->url, "search/repositories")) {
        CHECK(strstr(req->url, "stars%3A%3E%3D100") && strstr(req->url, "sort=stars&order=desc"));
        discovery_searches++;
        int route = strstr(req->url, "desktop") ? 1 : strstr(req->url, "creative") ? 2 : 0;
        if (mode == DISC_SEARCH_FAIL && route == 2) {
            res->status = 429;
            error_set(res->error, sizeof(res->error), "fixture discovery rate limit");
            return false;
        }
        Buffer b = {0};
        buf_add(&b, mode == DISC_INCOMPLETE && route == 1 ? "{\"incomplete_results\":true,\"items\":["
                                                          : "{\"incomplete_results\":false,\"items\":[");
        if (mode >= DISC_BALANCED && mode != DISC_EMPTY) {
            buf_add(&b,
                    "{\"id\":1001,\"full_name\":\"yftx888/Pulse\",\"description\":\"already starred\"},"
                    "{\"id\":9998,\"full_name\":\"example/"
                    "archived\",\"description\":\"archived\",\"archived\":true},"
                    "{\"id\":9997,\"full_name\":\"example/no-evidence\",\"description\":\"\",\"topics\":[]}");
            buf_add(&b, ",{\"id\":9996,\"full_name\":\"example/below-threshold\",\"description\":\"desktop "
                        "tool\",\"stargazers_count\":99}");
            if (route)
                buf_add(&b, ",{\"id\":10000,\"full_name\":\"example/duplicate\",\"description\":\"Windows "
                            "utility\"}");
            for (int k = 0; k < 35; k++) {
                uint64_t id = 10000 + route * 1000 + k;
                buf_add(&b, ",{\"id\":");
                buf_num(&b, id);
                buf_add(&b, ",\"full_name\":\"example/tool-");
                buf_num(&b, id);
                buf_add(&b, "\",\"description\":\"Windows "
                            "utility\",\"topics\":[\"desktop\"],\"stargazers_count\":100}");
            }
        } else if (mode != DISC_EMPTY) {
            buf_add(&b, "{\"id\":9991,\"full_name\":\"example/native-tool\",\"description\":\"Windows "
                        "utility\",\"topics\":[],\"stargazers_count\":100,\"language\":\"C\"}");
        }
        buf_add(&b, "]}");
        res->body = b.data;
        return true;
    }
    if (!strcmp(req->url, "https://model.example/v1/chat/completions")) {
        yyjson_doc *d = yyjson_read(req->body, strlen(req->body), 0);
        CHECK(d != NULL);
        if (d)
            yyjson_doc_free(d);
        const char *answer =
            strstr(req->body, "1001")
                ? profile_json
                : "{\"summary\":\"本批项目证据不足，不作强结论。\",\"interests\":[],\"preferences\":[]}";
        char *owned = NULL;
        if (strstr(req->body, "queries")) {
            CHECK(!strstr(req->body, "classifications") && !strstr(req->body, "source_stamp"));
            discovery_ai++;
            CHECK(!strstr(req->body, "\"tools\":") && !strstr(req->body, "web_search"));
            yyjson_doc *rd = yyjson_read(req->body, strlen(req->body), 0);
            yyjson_val *messages = yyjson_obj_get(yyjson_doc_get_root(rd), "messages");
            const char *input = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(messages, 1), "content"));
            yyjson_doc *context = yyjson_read(input, strlen(input), 0);
            yyjson_val *dimensions = yyjson_obj_get(yyjson_doc_get_root(context), "dimensions"), *axis;
            unsigned choices[6] = {0}, choice_count = 0;
            size_t di, dn;
            yyjson_arr_foreach(dimensions, di, dn, axis) if (yyjson_get_uint(yyjson_obj_get(axis, "count")))
                choices[choice_count++] = (unsigned)di;
            CHECK(yyjson_arr_size(dimensions) == 6 && choice_count > 0);
            Buffer plan = {0};
            buf_add(&plan, "{\"queries\":[");
            for (int k = 0; k < (mode == DISC_SHORT_QUERY ? 1 : 3); k++) {
                if (k)
                    buf_add(&plan, ",");
                buf_add(&plan, "{\"query\":");
                const char *query[] = {"native windows archived:false", "desktop data archived:false",
                                       "creative tools archived:false"};
                buf_json_string(&plan, mode == DISC_DUP_QUERY       ? "same"
                                       : mode == DISC_BAD_QUERY     ? "REPO:evil/test"
                                       : mode == DISC_PRIVATE_QUERY ? "secret-project"
                                                                    : query[k]);
                buf_add(&plan, ",\"intent\":");
                const char *intent[] = {"尝试可改造的小工具", "组织日常重复操作", "探索知识组织的新用途"};
                buf_json_string(&plan, intent[k]);
                buf_add(&plan, ",\"dimensions\":[");
                unsigned choice =
                    choices[(mode == DISC_COVERAGE && k == 2 ? 0 : k) % (choice_count ? choice_count : 1)];
                buf_num(&plan, mode == DISC_BAD_DIMENSION    ? 99
                               : mode == DISC_ZERO_DIMENSION ? 3
                               : mode == DISC_NARROW_PLAN    ? 1
                                                             : choice);
                if (mode == DISC_DUP_DIMENSION)
                    buf_add(&plan, ",1");
                buf_add(&plan, "]}");
            }
            buf_add(&plan, "]}");
            owned = plan.data;
            answer = owned;
            yyjson_doc_free(context);
            yyjson_doc_free(rd);
        } else if (strstr(req->body, "recommendations")) {
            CHECK(!strstr(req->body, "classifications") && !strstr(req->body, "source_stamp"));
            discovery_ai++;
            if (mode == DISC_RATE_LIMIT) {
                res->status = 429;
                error_set(res->error, sizeof(res->error), "fixture model rate limit");
                return false;
            }
            owned = fixture_selections(req);
            answer = owned;
            if (mode == 7)
                answer = "{\"recommendations\":[{\"id\":5555,\"reason\":\"invented\"}]}";
            if (mode == DISC_BAD_FINAL)
                answer = "not json";
            if (mode == DISC_CANCEL)
                SetEvent(cancel);
        } else if (strstr(req->body, "任务：中文项目概括")) {
            if (mode == 38 && InterlockedIncrement(&summary_fixture_calls) > 1) {
                res->status = 429;
                error_set(res->error, sizeof(res->error), "fixture limited");
                return false;
            }
            owned = fixture_analysis(req, true);
            answer = owned;
            if (mode == 32)
                answer = "{\"summaries\":[]}";
            if (mode == 45)
                answer =
                    "{\"summaries\":[{\"id\":1001,\"summary\":\"原生桌面工具。\",\"status\":\"ready\"}]}";
            if (mode == 37)
                SetEvent(cancel);
        } else if (strstr(req->body, "任务：收藏兴趣分类")) {
            owned = fixture_analysis(req, false);
            answer = owned;
            if (mode == 33)
                answer = "{\"classifications\":[{\"id\":99999,\"sufficient\":true,\"axes\":[]}]}";
            if (mode == 46)
                answer =
                    "{\"classifications\":[{\"id\":1001,\"sufficient\":true,\"axes\":[]},"
                    "{\"id\":1002,\"sufficient\":true,\"axes\":[{\"dimension\":1,\"reason\":\"开发库\"}]},"
                    "{\"id\":1003,\"sufficient\":true,\"axes\":[{\"dimension\":99,\"reason\":\"错误维度\"}]},"
                    "{\"id\":1004,\"sufficient\":true,\"axes\":[]},"
                    "{\"id\":1004,\"sufficient\":true,\"axes\":[]},"
                    "{\"id\":99999,\"sufficient\":true,\"axes\":[]}]}";
        } else if (strstr(req->body, "任务：叙事式兴趣画像")) {
            answer =
                "{\"summary\":\"关注原生工具与数据整理。\",\"interests\":[{\"name\":\"Windows\",\"reason\":"
                "\"桌面工具的描述。\",\"confidence\":\"medium\",\"evidence\":[1001]}],\"preferences\":[],"
                "\"narrative\":[{\"text\":"
                "\"你似乎在意日常与自己之间的距离：当重复的事情变得顺手，注意力便有机会回到真正想做的事上。这"
                "些收藏让人想到，你寻找的也许不只是方便，还有一种能够亲自安排节奏的自主感。\",\"evidence\":["
                "1001]},"
                "{\"text\":"
                "\"另一条线索，是愿意走近事物的结构，把零散的东西整理成可以理解和改动的秩序。秩序为尝试提供落"
                "脚处，却也可能让陌生的可能暂时留在门外。当手边的一切已经足够顺畅，你会把腾出的时间交给更深的"
                "理解，还是一个还说不出用途的新念头？\",\"evidence\":[1002]}]}";
            if (mode == 34)
                answer = "{\"summary\":\"无依据\",\"interests\":[],\"preferences\":[],\"narrative\":[{"
                         "\"text\":\"断言\",\"evidence\":[999999]}]}";
        } else if (strstr(req->body, "测试模型连接"))
            answer = "{\"ok\":true}";
        if (mode == 6)
            answer = "not json";
        Buffer b = {0};
        buf_add(&b, "{\"choices\":[{\"message\":{\"content\":");
        buf_json_string(&b, answer);
        buf_add(&b, "}}]}");
        res->body = b.data;
        mem_free(owned);
        return true;
    }
    error_set(res->error, sizeof(res->error), "unexpected fixture request");
    return false;
}
static void init_job(Job *j, Library *l, int kind) {
    memset(j, 0, sizeof(*j));
    CHECK(library_clone(&j->library, l));
    j->kind = kind;
    j->github_key = str_copy("fixture-token");
    j->ai_key = str_copy("fixture-key");
    j->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    j->base_revision = l->revision;
}
static void drop_job(Job *j) {
    library_free(&j->library);
    secure_free(j->github_key);
    secure_free(j->ai_key);
    CloseHandle(j->cancel);
}
static char *selection_payload(Library *l, J *candidates, int fault) {
    J *out = yyjson_mut_obj(l->doc), *answers = yyjson_mut_arr(l->doc);
    jset(l, out, "recommendations", answers);
    size_t total = yyjson_mut_arr_size(candidates);
    for (size_t i = 0; i < total; i++) {
        J *r = yyjson_mut_arr_get(candidates, i), *v = yyjson_mut_obj(l->doc);
        jnumber(l, v, "id", jnum(r, "id"));
        jtext(l, v, "hook", jstr(r, "hook"));
        jtext(l, v, "try_idea", jstr(r, "try_idea"));
        jtext(l, v, "discovery_lane", jstr(r, "discovery_lane"));
        jtext(l, v, "reason", "与画像中的 Windows 兴趣相关。");
        if (jget(r, "profile_dimensions")) {
            jset(l, v, "profile_dimensions", jclone(l, jget(r, "profile_dimensions")));
            jtext(l, v, "summary", "提供具体用途的中文项目概括。");
            jtext(l, v, "reason", "关联动手构建、效率偏好和信息秩序。");
        }
        yyjson_mut_arr_append(answers, v);
    }
    J *first = yyjson_mut_arr_get(answers, 0), *second = yyjson_mut_arr_get(answers, 1);
    if (fault == 1)
        jnumber(l, first, "id", 99999999);
    if (fault == 2)
        jnumber(l, second, "id", jnum(first, "id"));
    if (fault == 3)
        jset(l, second, "hook", yyjson_mut_null(l->doc));
    if (fault == 4)
        jset(l, second, "try_idea", yyjson_mut_uint(l->doc, 1));
    if (fault == 5)
        jtext(l, second, "discovery_lane", "unknown");
    if (fault == 6)
        jtext(l, second, "reason", "与不存在的画像兴趣相关。");
    if (fault == 7)
        jtext(l, second, "discovery_lane",
              !strcmp(jstr(second, "discovery_lane"), "core") ? "adjacent" : "core");
    if (fault == 8)
        yyjson_mut_arr_append(answers, jclone(l, first));
    if (fault == 9) {
        char overlong[300];
        memset(overlong, 'x', sizeof(overlong) - 1);
        overlong[299] = 0;
        jtext(l, second, "hook", overlong);
    }
    if (fault == 10)
        jtext(l, second, "try_idea", " \t\r\n");
    if (fault == 11)
        jtext(l, second, "hook", "line one\nline two");
    if (fault == 12)
        jtext(l, second, "summary", "English only");
    if (fault == 13)
        jset(l, second, "summary", yyjson_mut_uint(l->doc, 123));
    if (fault == 14)
        jtext(l, second, "summary", "中文\n换行");
    if (fault == 15)
        jset(l, second, "profile_dimensions", yyjson_mut_arr(l->doc));
    if (fault == 16) {
        J *axes = yyjson_mut_arr(l->doc);
        yyjson_mut_arr_append(axes, yyjson_mut_uint(l->doc, 3));
        jset(l, second, "profile_dimensions", axes);
    }
    if (fault == 17)
        yyjson_mut_obj_remove_str(second, "summary");
    return yyjson_mut_val_write(out, 0, NULL);
}
static void check_unchanged(Library *l, const char *before) {
    size_t n;
    char *after = library_json(l, &n);
    CHECK(after && !strcmp(before, after));
    mem_free(after);
}
static void test_discovery(Library *base) {
    Library l = {0}, curated = {0};
    char error[1024] = {0};
    size_t len;
    CHECK(library_clone(&l, base));
    J *p = jget(root(&l), "profile"), *classes = yyjson_mut_arr(l.doc);
    for (unsigned k = 0; k < 5; k++) {
        J *record = yyjson_mut_obj(l.doc), *axes = yyjson_mut_arr(l.doc), *axis = yyjson_mut_obj(l.doc);
        jnumber(&l, record, "id", 1001 + k);
        jset(&l, record, "sufficient", yyjson_mut_true(l.doc));
        jnumber(&l, axis, "dimension", k == 1 ? 2 : k == 2 ? 5 : 1);
        jtext(&l, axis, "reason", "固定资料中的用途依据。");
        yyjson_mut_arr_append(axes, axis);
        jset(&l, record, "axes", axes);
        yyjson_mut_arr_append(classes, record);
    }
    jset(&l, p, "classifications", classes);
    jtext(&l, p, "source_stamp", "0000000000000000");
    jnumber(&l, p, "source_version", 2);
    const char *parts = "[{\"text\":\"你似乎喜欢亲手改变日常。\",\"evidence\":[1001]},{\"text\":"
                        "\"也愿意为新的可能留些位置。\",\"evidence\":[1002]}]";
    yyjson_doc *pd = yyjson_read(parts, strlen(parts), 0);
    jset(&l, p, "narrative", yyjson_val_mut_copy(l.doc, yyjson_doc_get_root(pd)));
    yyjson_doc_free(pd);
    jtext(&l, p, "narrative_style", PROFILE_STYLE);
    char *reflection = profile_reflection(p);
    CHECK(reflection && !strcmp(reflection, "你似乎喜欢亲手改变日常。也愿意为新的可能留些位置。"));
    mem_free(reflection);
    J *old = jclone(&l, find_repo(&l, 1002));
    jnumber(&l, old, "id", 8001);
    jtext(&l, old, "full_name", "example/previous-batch");
    jtext(&l, old, "recommendation", "Legacy discovery, without the new fields.");
    yyjson_mut_arr_append(jget(root(&l), "discoveries"), old);
    CHECK(library_validate(&l, error));
    char *legacy_json = library_json(&l, &len);
    Library legacy = {0};
    CHECK(library_parse(&legacy, legacy_json, len, error));
    CHECK(find_candidate(&legacy, 8001) && !*jstr(find_candidate(&legacy, 8001), "hook"));
    mem_free(legacy_json);
    library_free(&legacy);
    Job job;
    mode = DISC_BALANCED;
    reset_discovery_fixture();
    init_job(&job, &l, JOB_DISCOVER);
    CHECK(run_job(&job));
    CHECK(discovery_searches == 3 && discovery_ai == 2);
    CHECK(pool_count == 30 && pool_routes[0] == 10 && pool_routes[1] == 10 && pool_routes[2] == 10);
    J *arr = jget(root(&job.library), "discoveries");
    CHECK(yyjson_mut_arr_size(arr) == 6);
    const char *expected[] = {"core", "core", "adjacent", "core", "core", "adjacent"};
    for (int k = 0; k < 6; k++)
        CHECK(!strcmp(jstr(yyjson_mut_arr_get(arr, k), "discovery_lane"), expected[k]));
    CHECK(!find_candidate(&job.library, 1001) && !find_candidate(&job.library, 9998) &&
          !find_candidate(&job.library, 9997) && !find_candidate(&job.library, 9996));
    CHECK(!strcmp(jstr(find_repo(&job.library, 1001), "note"), jstr(find_repo(base, 1001), "note")));
    CHECK(library_validate(&job.library, error));
    CHECK(library_clone(&curated, &job.library));
    drop_job(&job);
    char *curated_json = library_json(&curated, &len);
    Library restored = {0};
    CHECK(library_parse(&restored, curated_json, len, error));
    CHECK(*jstr(yyjson_mut_arr_get(jget(root(&restored), "discoveries"), 0), "try_idea"));
    mem_free(curated_json);
    library_free(&restored);

    for (int auth = 0; auth < 2; auth++) {
        mode = auth ? 8 : 30;
        reset_discovery_fixture();
        init_job(&job, &curated, JOB_DISCOVER);
        char *unchanged = library_json(&job.library, &len);
        CHECK(!run_job(&job) && discovery_searches == 0 && discovery_ai == 0);
        check_unchanged(&job.library, unchanged);
        mem_free(unchanged);
        drop_job(&job);
    }
    mode = DISC_BALANCED;
    Library excluded = {0};
    CHECK(library_clone(&excluded, &l));
    J *owned = jclone(&excluded, find_repo(&excluded, 1001));
    jnumber(&excluded, owned, "id", 10000);
    yyjson_mut_arr_append(jget(root(&excluded), "repos"), owned);
    J *previous = jclone(&excluded, old);
    jnumber(&excluded, previous, "id", 12000);
    yyjson_mut_arr_append(jget(root(&excluded), "discoveries"), previous);
    for (int k = 0; k < 2; k++) {
        J *feedback = yyjson_mut_obj(excluded.doc);
        jnumber(&excluded, feedback, "id", 11000 + k);
        jtext(&excluded, feedback, "state", k ? "later" : "ignored");
        jset(&excluded, feedback, "repo", jclone(&excluded, old));
        yyjson_mut_arr_append(jget(root(&excluded), "feedback"), feedback);
    }
    reset_discovery_fixture();
    init_job(&job, &excluded, JOB_DISCOVER);
    CHECK(run_job(&job));
    CHECK(!find_candidate(&job.library, 10000) && !find_candidate(&job.library, 11000) &&
          find_candidate(&job.library, 11001) && !find_candidate(&job.library, 12000));
    CHECK(discovery_searches == 3 && pool_count == 30);
    drop_job(&job);
    library_free(&excluded);

    const int failure_modes[] = {7,
                                 DISC_SEARCH_FAIL,
                                 DISC_INCOMPLETE,
                                 DISC_CANCEL,
                                 DISC_BAD_FINAL,
                                 DISC_SHORT_QUERY,
                                 DISC_DUP_QUERY,
                                 DISC_BAD_QUERY,
                                 DISC_BAD_DIMENSION,
                                 DISC_NARROW_PLAN,
                                 DISC_RATE_LIMIT,
                                 DISC_COVERAGE,
                                 DISC_DUP_DIMENSION,
                                 DISC_ZERO_DIMENSION};
    for (size_t k = 0; k < sizeof(failure_modes) / sizeof(failure_modes[0]); k++) {
        mode = failure_modes[k];
        reset_discovery_fixture();
        init_job(&job, &curated, JOB_DISCOVER);
        char *before = library_json(&job.library, &len);
        CHECK(!run_job(&job));
        check_unchanged(&job.library, before);
        CHECK(job.library.revision == curated.revision);
        if (mode >= DISC_SHORT_QUERY && mode != DISC_RATE_LIMIT)
            CHECK(discovery_searches == 0 && discovery_ai == 1);
        mem_free(before);
        drop_job(&job);
    }
    mode = DISC_PRIVATE_QUERY;
    reset_discovery_fixture();
    init_job(&job, &curated, JOB_DISCOVER);
    J *private_repo = find_repo(&job.library, 1001);
    jboolean(&job.library, private_repo, "private", true);
    jtext(&job.library, private_repo, "full_name", "private-owner/secret-project");
    char *private_before = library_json(&job.library, &len);
    CHECK(!run_job(&job) && discovery_searches == 0 && discovery_ai == 1);
    check_unchanged(&job.library, private_before);
    mem_free(private_before);
    drop_job(&job);
    mode = DISC_BALANCED;
    init_job(&job, &curated, JOB_DISCOVER);
    p = jget(root(&job.library), "profile");
    jset(&job.library, p, "interests", yyjson_mut_arr(job.library.doc));
    jset(&job.library, p, "preferences", yyjson_mut_arr(job.library.doc));
    CHECK(run_job(&job)); /* Six dimensions work without a technical-interest keyword. */
    drop_job(&job);
    init_job(&job, &curated, JOB_DISCOVER);
    p = jget(root(&job.library), "profile");
    jset(&job.library, p, "classifications", yyjson_mut_arr(job.library.doc));
    int prior_requests = requests;
    char *before = library_json(&job.library, &len);
    CHECK(!run_job(&job) && requests == prior_requests);
    check_unchanged(&job.library, before);
    mem_free(before);
    drop_job(&job);

    mode = DISC_EMPTY;
    reset_discovery_fixture();
    init_job(&job, &curated, JOB_DISCOVER);
    CHECK(run_job(&job));
    CHECK(yyjson_mut_arr_size(jget(root(&job.library), "discoveries")) == 0);
    CHECK(discovery_searches == 3 && discovery_ai == 1);
    drop_job(&job);
    mode = 0;
    reset_discovery_fixture();
    init_job(&job, &l, JOB_DISCOVER);
    CHECK(run_job(&job));
    CHECK(yyjson_mut_arr_size(jget(root(&job.library), "discoveries")) == 1);
    CHECK(discovery_searches == 3 && discovery_ai == 2);
    drop_job(&job);

    J *candidates = jget(root(&curated), "discoveries");
    for (int fault = 1; fault <= 17; fault++) {
        char *answer = selection_payload(&curated, candidates, fault);
        before = library_json(&curated, &len);
        CHECK(!discovery_accept(&curated, candidates, answer, error));
        check_unchanged(&curated, before);
        mem_free(before);
        mem_free(answer);
    }
    /* Five core results are rejected even if all IDs and fields are otherwise valid. */
    J *over_pool = yyjson_mut_arr(curated.doc);
    for (int k = 0; k < 6; k++)
        if (k != 5)
            yyjson_mut_arr_append(over_pool, jclone(&curated, yyjson_mut_arr_get(candidates, k)));
    J *extra = jclone(&curated, yyjson_mut_arr_get(candidates, 0));
    jnumber(&curated, extra, "id", 13000);
    yyjson_mut_arr_append(over_pool, extra);
    char *answer = selection_payload(&curated, over_pool, 0);
    before = library_json(&curated, &len);
    CHECK(!discovery_accept(&curated, over_pool, answer, error));
    check_unchanged(&curated, before);
    mem_free(answer);
    mem_free(before);
    CHECK(discovery_accept(&curated, candidates, "{\"recommendations\":[]}", error));
    CHECK(!yyjson_mut_arr_size(jget(root(&curated), "discoveries")));
    library_free(&curated);
    library_free(&l);
    mode = 0;
}
static J *test_value(Library *l, const char *json) {
    yyjson_doc *d = yyjson_read(json, strlen(json), 0);
    J *v = d ? yyjson_val_mut_copy(l->doc, yyjson_doc_get_root(d)) : NULL;
    if (d)
        yyjson_doc_free(d);
    return v;
}
static void test_token_summary_profile(Library *base) {
    Library l = {0}, completed = {0};
    char e[1024] = {0};
    Job j;
    CHECK(library_clone(&l, base));
    mode = 0;
    init_job(&j, &l, JOB_GH_CONNECT);
    CHECK(run_job(&j));
    CHECK(!strcmp(jstr(root(&j.library), "account"), "preview"));
    CHECK(!strcmp(jstr(jget(root(&j.library), "settings"), "username"), "preview"));
    drop_job(&j);
    for (int fault = 0; fault < 3; fault++) {
        mode = fault == 0 ? 30 : fault == 1 ? 8 : 9;
        init_job(&j, &l, JOB_GH_CONNECT);
        size_t length;
        char *before = library_json(&j.library, &length);
        CHECK(!run_job(&j));
        check_unchanged(&j.library, before);
        free(before);
        drop_job(&j);
    }
    mode = 0;
    const int gh_jobs[] = {JOB_SYNC, JOB_DISCOVER, JOB_STAR, JOB_GH_CONNECT};
    for (int k = 0; k < 4; k++) {
        init_job(&j, &l, gh_jobs[k]);
        j.repo_id = 1001;
        secure_free(j.github_key);
        j.github_key = NULL;
        int count = requests;
        CHECK(!run_job(&j));
        CHECK(requests == count);
        drop_job(&j);
    }
    mode = 31;
    init_job(&j, &l, JOB_STAR);
    j.repo_id = 1001;
    j.desired_star = true;
    CHECK(!run_job(&j));
    CHECK(strstr(j.error, "Starring") != NULL);
    CHECK(j.library.revision == l.revision);
    drop_job(&j);
    mode = 0;
    J *settings = jget(root(&l), "settings");
    jtext(&l, settings, "guidance", "LEGACY_GUIDANCE_SHOULD_NOT_BE_SENT");
    for (int k = 0; k < 20; k++) {
        J *r = jclone(&l, find_repo(&l, 1001));
        jnumber(&l, r, "id", 3000 + k);
        yyjson_mut_arr_append(jget(root(&l), "repos"), r);
    }
    CHECK(!summary_current(find_repo(&l, 1001)));
    mode = 38;
    summary_fixture_calls = 0;
    init_job(&j, &l, JOB_SUMMARY);
    secure_free(j.github_key);
    j.github_key = NULL;
    int prior = requests;
    CHECK(!run_job(&j));
    CHECK(requests - prior == 2);
    unsigned ready = 0;
    size_t i, n;
    J *r;
    yyjson_mut_arr_foreach(jget(root(&j.library), "repos"), i, n, r) if (summary_current(r)) ready++;
    CHECK(ready == 20);
    CHECK(!strcmp(jstr(find_repo(&j.library, 1001), "note"), jstr(find_repo(&l, 1001), "note")));
    CHECK(library_clone(&completed, &j.library));
    drop_job(&j);
    for (int fault = 0; fault < 2; fault++) {
        mode = fault ? 37 : 32;
        init_job(&j, &completed, JOB_SUMMARY);
        size_t length;
        char *before = library_json(&j.library, &length);
        CHECK(!run_job(&j));
        check_unchanged(&j.library, before);
        free(before);
        drop_job(&j);
    }
    mode = 0;
    init_job(&j, &completed, JOB_SUMMARY);
    CHECK(run_job(&j));
    ready = 0;
    yyjson_mut_arr_foreach(jget(root(&j.library), "repos"), i, n, r) if (summary_current(r)) ready++;
    CHECK(ready == 25);
    CHECK(summary_current(find_repo(&j.library, 3019)));
    r = find_repo(&j.library, 1001);
    char stamp[17];
    repo_fingerprint(r, stamp);
    jnumber(&j.library, r, "version", jnum(r, "version") + 1);
    jnumber(&j.library, r, "stars", 1);
    jtext(&j.library, r, "pushed_at", "changed");
    library_touch(&j.library);
    CHECK(summary_current(r));
    jtext(&j.library, r, "description", "Changed relevant source");
    CHECK(!summary_current(r));
    drop_job(&j);
    J *one = yyjson_mut_arr(l.doc);
    yyjson_mut_arr_append(one, jclone(&l, find_repo(&l, 1001)));
    const char *bad_summaries[] = {
        "{\"summaries\":[{\"id\":99999,\"summary\":\"用途\",\"status\":\"ready\"}]}",
        "{\"summaries\":[{\"id\":1001,\"summary\":\"English only\",\"status\":\"ready\"}]}",
        "{\"summaries\":[{\"id\":1001,\"summary\":\"中文\\n换行\",\"status\":\"ready\"}]}",
        "{\"summaries\":[{\"id\":1001,\"summary\":\"猜测\",\"status\":\"insufficient\"}]}",
        "{\"summaries\":[{\"id\":1001,\"summary\":123,\"status\":\"ready\"}]}",
        "{\"summaries\":[{\"id\":1001,\"summary\":\"用途\",\"status\":\"ready\"},{\"id\":1001,\"summary\":"
        "\"用途\",\"status\":\"ready\"}]}"};
    size_t length;
    char *before = library_json(&l, &length);
    for (int k = 0; k < 6; k++) {
        CHECK(!summary_accept(&l, one, bad_summaries[k], e));
        check_unchanged(&l, before);
    }
    free(before);
    J *noinfo = find_repo(&l, 1001);
    jtext(&l, noinfo, "description", "");
    one = yyjson_mut_arr(l.doc);
    yyjson_mut_arr_append(one, jclone(&l, noinfo));
    CHECK(!summary_accept(
        &l, one, "{\"summaries\":[{\"id\":1001,\"summary\":\"没有资料却发明用途\",\"status\":\"ready\"}]}",
        e));
    CHECK(summary_accept(
        &l, one, "{\"summaries\":[{\"id\":1001,\"summary\":\"资料不足\",\"status\":\"insufficient\"}]}", e));
    CHECK(summary_current(noinfo));
    jtext(&l, noinfo, "description", "恢复桌面工具资料");
    init_job(&j, &l, JOB_SUMMARY);
    j.repo_id = 1001;
    CHECK(run_job(&j));
    CHECK(summary_current(find_repo(&j.library, 1001)));
    drop_job(&j);
    init_job(&j, &l, JOB_PROFILE);
    secure_free(j.github_key);
    j.github_key = NULL;
    CHECK(run_job(&j));
    CHECK(profile_current(&j.library));
    J *p = jget(root(&j.library), "profile");
    unsigned counts[6];
    Job discovery;
    init_job(&discovery, &j.library, JOB_DISCOVER);
    CHECK(run_job(&discovery));
    drop_job(&discovery);
    CHECK(profile_counts(p, counts) == 25 && counts[1] == 25 && counts[0] == 0);
    jtext(&j.library, jget(root(&j.library), "settings"), "ai_model", "unrelated-setting");
    jboolean(&j.library, jget(root(&j.library), "settings"), "light", true);
    jtext(&j.library, root(&j.library), "discovered_at", "unrelated");
    library_touch(&j.library);
    jtext(&j.library, find_repo(&j.library, 1001), "ai_summary", "新概括");
    CHECK(profile_current(&j.library));
    J *records = jget(p, "classifications"), *first = yyjson_mut_arr_get_first(records);
    J *axes = jget(first, "axes");
    yyjson_mut_arr_append(axes, test_value(&j.library, "{\"dimension\":2,\"reason\":\"重叠维度依据\"}"));
    CHECK(profile_classifications_valid(&j.library, records, NULL, e));
    CHECK(profile_counts(p, counts) == 25 && counts[1] == 25 && counts[2] == 1);
    yyjson_mut_arr_append(axes, jclone(&j.library, yyjson_mut_arr_get_first(axes)));
    CHECK(!profile_classifications_valid(&j.library, records, NULL, e));
    yyjson_mut_arr_remove_last(axes);
    jnumber(&j.library, first, "id", 999999);
    CHECK(!profile_classifications_valid(&j.library, records, NULL, e));
    jnumber(&j.library, first, "id", 1002);
    CHECK(!profile_classifications_valid(&j.library, records, NULL, e));
    jnumber(&j.library, first, "id", 1001);
    jtext(&j.library, find_repo(&j.library, 1001), "description", "changed");
    CHECK(!profile_current(&j.library));
    CHECK(library_validate(&j.library, e));
    J *paragraph = yyjson_mut_arr_get_first(jget(p, "narrative"));
    jset(&j.library, paragraph, "evidence", test_value(&j.library, "[99999]"));
    CHECK(!profile_validate_value(&j.library, p, e));
    drop_job(&j);
    const char *bad_records[] = {
        "[{\"id\":1001,\"sufficient\":true,\"axes\":[{\"dimension\":6,\"reason\":\"无效\"}]}]",
        "[{\"id\":1001,\"sufficient\":true,\"axes\":[{\"dimension\":0,\"reason\":\"依据\"},{\"dimension\":1,"
        "\"reason\":\"依据\"},{\"dimension\":2,\"reason\":\"依据\"},{\"dimension\":3,\"reason\":\"依据\"}]}]",
        "[{\"id\":1001,\"sufficient\":false,\"axes\":[{\"dimension\":1,\"reason\":\"无资料\"}]}]",
        "[{\"id\":1001,\"sufficient\":true,\"axes\":[{\"dimension\":1,\"reason\":\"\"}]}]"};
    for (int k = 0; k < 4; k++)
        CHECK(!profile_classifications_valid(&l, test_value(&l, bad_records[k]), NULL, e));
    J *counter = test_value(&l, "{\"classifications\":[{\"id\":1001,\"sufficient\":false,\"axes\":[]},{"
                                "\"id\":1002,\"sufficient\":true,\"axes\":[]}]}");
    CHECK(profile_counts(counter, counts) == 1 && counts[1] == 0);
    counter = test_value(&l, "{\"classifications\":[]}");
    CHECK(profile_counts(counter, counts) == 0);
    for (int k = 0; k < 2; k++) {
        mode = k ? 34 : 33;
        init_job(&j, &l, JOB_PROFILE);
        before = library_json(&j.library, &length);
        CHECK(!run_job(&j));
        if (!k)
            check_unchanged(&j.library, before);
        else
            CHECK(profile_classification_cached(&j.library, find_repo(&j.library, 1001)) != NULL);
        free(before);
        drop_job(&j);
    }
    mode = 0;
    yyjson_mut_arr_foreach(jget(root(&l), "repos"), i, n, r) jtext(&l, r, "description", "");
    init_job(&j, &l, JOB_PROFILE);
    before = library_json(&j.library, &length);
    CHECK(!run_job(&j));
    CHECK(j.completed == 25 && profile_classification_cached(&j.library, find_repo(&j.library, 1001)));
    free(before);
    drop_job(&j);
    library_free(&completed);
    library_free(&l);
}
static LRESULT CALLBACK delayed_commit(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_BATCH) {
        if (!((Job *)lp)->completed)
            Sleep(180);
        return TRUE;
    }
    if (message == WM_PROGRESS) {
        mem_free((void *)lp);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
static void test_parallel_and_avatars(Library *base) {
    Library l = {0};
    CHECK(library_clone(&l, base));
    for (int k = 0; k < 195; k++) {
        J *r = jclone(&l, find_repo(&l, 1001));
        jnumber(&l, r, "id", 8000 + k);
        if (k == 0) {
            jboolean(&l, r, "private", true);
            jtext(&l, r, "description", "PRIVATE_METADATA_MARKER: developer tool");
            jtext(&l, r, "note", "PRIVATE_OLD_FIELD_MUST_NOT_BE_SENT");
        }
        yyjson_mut_arr_append(jget(root(&l), "repos"), r);
    }
    for (unsigned setting = 0; setting < 3; setting++) {
        unsigned limit = setting == 0 ? 1 : setting == 1 ? 5 : 10;
        jnumber(&l, jget(root(&l), "settings"), "ai_concurrency", limit);
        CHECK(ai_concurrency(&l) == limit);
        for (int kind = 0; kind < 2; kind++) {
            parallel_live = parallel_peak = parallel_started = private_seen = 0;
            parallel_limit = limit;
            parallel_barrier = CreateEventW(NULL, TRUE, FALSE, NULL);
            mode = 40;
            Job j;
            init_job(&j, &l, kind ? JOB_PROFILE : JOB_SUMMARY);
            CHECK(run_job(&j));
            CHECK(parallel_peak == (LONG)parallel_limit && parallel_live == 0);
            CHECK(private_seen == 1);
            CHECK(j.completed == 200);
            CHECK(kind ? parallel_started >= 10 : parallel_started == 10);
            if (!kind) {
                CHECK(summary_current(find_repo(&j.library, 8000)));
                CHECK(j.batches == 10);
            } else {
                unsigned counts[6];
                CHECK(profile_counts(jget(root(&j.library), "profile"), counts) == 200);
            }
            drop_job(&j);
            CloseHandle(parallel_barrier);
        }
    }
    jnumber(&l, jget(root(&l), "settings"), "ai_concurrency", 5);
    for (int fault = 41; fault <= 43; fault++) {
        parallel_live = parallel_peak = parallel_started = 0;
        parallel_limit = 5;
        parallel_barrier = CreateEventW(NULL, TRUE, FALSE, NULL);
        mode = fault;
        Job j;
        init_job(&j, &l, JOB_SUMMARY);
        CHECK(run_job(&j) == (fault == 43));
        CHECK((fault == 43 ? parallel_started > 10 : parallel_started == 5) && parallel_live == 0);
        CHECK(j.completed == (fault == 42 ? 0u : fault == 43 ? 200u : 80u));
        CHECK(summary_current(find_repo(&j.library, parallel_failed_id)) == (fault == 43));
        drop_job(&j);
        CloseHandle(parallel_barrier);
    }
    mode = 44;
    parallel_live = parallel_peak = parallel_started = 0;
    parallel_limit = 5;
    parallel_barrier = CreateEventW(NULL, TRUE, FALSE, NULL);
    WNDCLASSW klass = {0};
    klass.lpfnWndProc = delayed_commit;
    klass.hInstance = GetModuleHandleW(NULL);
    klass.lpszClassName = L"StarTestsDelayedCommit";
    CHECK(RegisterClassW(&klass) != 0);
    HWND fake = CreateWindowExW(0, klass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                klass.hInstance, NULL);
    CHECK(fake != NULL);
    Job delayed;
    init_job(&delayed, &l, JOB_SUMMARY);
    delayed.notify = fake;
    CHECK(!run_job(&delayed));
    CHECK(parallel_started == 5 && delayed.completed == 80);
    MSG message;
    while (PeekMessageW(&message, fake, WM_PROGRESS, WM_PROGRESS, PM_REMOVE))
        DispatchMessageW(&message);
    drop_job(&delayed);
    DestroyWindow(fake);
    UnregisterClassW(klass.lpszClassName, klass.hInstance);
    CloseHandle(parallel_barrier);
    mode = 0;
    Job j;
    init_job(&j, &l, JOB_SUMMARY);
    j.discovery_summary = true;
    j.repo_id = 90001;
    J *candidate = jclone(&j.library, find_repo(&j.library, 1001));
    jnumber(&j.library, candidate, "id", 90001);
    jtext(&j.library, candidate, "hook", "legacy hook");
    yyjson_mut_arr_append(jget(root(&j.library), "discoveries"), candidate);
    CHECK(run_job(&j));
    CHECK(summary_current(find_candidate(&j.library, 90001)));
    CHECK(!strcmp(jstr(find_candidate(&j.library, 90001), "hook"), "legacy hook"));
    drop_job(&j);
    CHECK(avatar_url_valid("https://avatars.githubusercontent.com/u/123?s=64"));
    CHECK(!avatar_url_valid("https://avatars.githubusercontent.com.evil/u/1"));
    CHECK(!avatar_url_valid("http://avatars.githubusercontent.com/u/1"));
    CHECK(!avatar_url_valid("https://evil.example/u/1"));
    CHECK(!avatar_url_valid("https://avatars.githubusercontent.com/u/1\r\n"));
    char fallback[1024];
    J *legacy_avatar = yyjson_mut_obj(l.doc);
    jtext(&l, legacy_avatar, "full_name", "Owner-123/legacy-repo");
    CHECK(!strcmp(repo_avatar_url(legacy_avatar, fallback),
                  "https://avatars.githubusercontent.com/Owner-123?s=64"));
    CHECK(avatar_url_valid(fallback));
    jtext(&l, legacy_avatar, "avatar_url", "https://avatars.githubusercontent.com/u/123?s=64");
    CHECK(!strcmp(repo_avatar_url(legacy_avatar, fallback),
                  "https://avatars.githubusercontent.com/u/123?s=64"));
    jtext(&l, legacy_avatar, "avatar_url", "https://evil.example/portrait");
    CHECK(!strcmp(repo_avatar_url(legacy_avatar, fallback),
                  "https://avatars.githubusercontent.com/Owner-123?s=64"));
    const char *invalid_owners[] = {"owner?token=x/repo", "../repo", "/repo", "owner/", "owner\\evil/repo"};
    for (unsigned i = 0; i < sizeof(invalid_owners) / sizeof(*invalid_owners); i++) {
        jtext(&l, legacy_avatar, "full_name", invalid_owners[i]);
        CHECK(!*repo_avatar_url(legacy_avatar, fallback));
    }
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    unsigned char bmp[58] = {0};
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[2] = 58;
    bmp[10] = 54;
    bmp[14] = 40;
    bmp[18] = 1;
    bmp[22] = 1;
    bmp[26] = 1;
    bmp[28] = 24;
    bmp[34] = 4;
    bmp[54] = 80;
    bmp[55] = 120;
    bmp[56] = 200;
    unsigned char *pixels = avatar_decode(bmp, sizeof(bmp));
    CHECK(pixels != NULL);
    if (pixels)
        CHECK(pixels[(32 * 64 + 32) * 4 + 3] == 255);
    mem_free(pixels);
    CHECK(!avatar_decode("broken", 6));
    CHECK(!avatar_decode(bmp, 256u * 1024u + 1));
    bmp[18] = 0;
    bmp[19] = 8;
    CHECK(!avatar_decode(bmp, sizeof(bmp)));
    CoUninitialize();
    library_free(&l);
}
static LONG avatar_requests, avatar_live, avatar_peak;
static HANDLE avatar_barrier;
static bool avatar_fixture(const Request *req, Response *res, HANDLE cancel) {
    (void)cancel;
    CHECK(!req->github && !req->token && req->max_bytes == 256u * 1024u);
    LONG count = InterlockedIncrement(&avatar_requests), live = InterlockedIncrement(&avatar_live), peak;
    do {
        peak = avatar_peak;
    } while (live > peak && InterlockedCompareExchange(&avatar_peak, live, peak) != peak);
    if (count >= 2)
        SetEvent(avatar_barrier);
    CHECK(WaitForSingleObject(avatar_barrier, 5000) == WAIT_OBJECT_0);
    Sleep(20);
    unsigned char bmp[58] = {0};
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[2] = 58;
    bmp[10] = 54;
    bmp[14] = 40;
    bmp[18] = 1;
    bmp[22] = 1;
    bmp[26] = 1;
    bmp[28] = 24;
    bmp[34] = 4;
    bmp[54] = 80;
    bmp[55] = 120;
    bmp[56] = 200;
    memset(res, 0, sizeof(*res));
    res->body = mem_alloc(sizeof(bmp));
    memcpy(res->body, bmp, sizeof(bmp));
    res->size = sizeof(bmp);
    res->status = 200;
    InterlockedDecrement(&avatar_live);
    return true;
}
static bool await_avatar(uint64_t id, const char *url, bool connected) {
    ULONGLONG deadline = GetTickCount64() + 5000;
    do {
        if (avatar_pixels(id, url, connected))
            return true;
        Sleep(5);
    } while (GetTickCount64() < deadline);
    return false;
}
static void test_avatar_cache(void) {
    wchar_t dir[512];
    swprintf_s(dir, 512, L"artifacts\\avatar-test-%lu", GetCurrentProcessId());
    Storage storage = {0};
    char error[1024] = {0};
    CHECK(storage_init(&storage, dir, error));
    network_transport = avatar_fixture;
    avatar_requests = avatar_live = avatar_peak = 0;
    avatar_barrier = CreateEventW(NULL, TRUE, FALSE, NULL);
    avatars_init(&storage, NULL);
    const char *one = "https://avatars.githubusercontent.com/u/1?s=64",
               *two = "https://avatars.githubusercontent.com/u/2?s=64",
               *three = "https://avatars.githubusercontent.com/u/3?s=64";
    CHECK(avatar_pixels(1, one, true) == NULL);
    avatar_pixels(1, one, true);
    avatar_pixels(2, two, true);
    avatar_pixels(3, three, true);
    CHECK(await_avatar(1, one, true) && await_avatar(2, two, true) && await_avatar(3, three, true));
    CHECK(avatar_requests == 3 && avatar_peak == 2);
    avatars_free();
    avatar_requests = 0;
    avatars_init(&storage, NULL);
    CHECK(await_avatar(1, one, false));
    CHECK(avatar_requests == 0);
    avatars_free();
    /* A fetch triggers eviction in this isolated cache, never in the user's directory. */
    char *payload = mem_alloc(1024u * 1024u);
    memset(payload, 0, 1024u * 1024u);
    wchar_t path[32768];
    for (int k = 0; k < 21; k++) {
        swprintf_s(path, 32768, L"%s\\avatars\\stale-%02d.img", storage.dir, k);
        CHECK(file_write(path, payload, 1024u * 1024u));
        HANDLE f = CreateFileW(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            FILETIME old = {(DWORD)(k + 1), 0};
            SetFileTime(f, NULL, NULL, &old);
            CloseHandle(f);
        }
    }
    mem_free(payload);
    avatars_init(&storage, NULL);
    SetEvent(avatar_barrier);
    CHECK(await_avatar(4, "https://avatars.githubusercontent.com/u/4?s=64", true));
    avatars_free();
    swprintf_s(path, 32768, L"%s\\avatars\\*.img", storage.dir);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(path, &data);
    uint64_t total = 0;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            total += ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    CHECK(total <= 20u * 1024u * 1024u);
    CloseHandle(avatar_barrier);
    network_transport = fixture;
}
static Storage item_commit_storage;
static unsigned item_commits, item_fail_at;
static bool item_cancel_after_save;
static LRESULT CALLBACK item_commit_fixture(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_BATCH) {
        Job *j = (Job *)lp;
        if (item_commits == item_fail_at) {
            error_set(j->error, sizeof(j->error), "fixture project save failed");
            return FALSE;
        }
        if (!storage_save(&item_commit_storage, &j->library, j->error))
            return FALSE;
        item_commits++;
        if (item_cancel_after_save)
            SetEvent(j->cancel);
        return TRUE;
    }
    if (message == WM_PROGRESS) {
        mem_free((void *)lp);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
static void test_profile_resume(void) {
    Library l = {0}, restored = {0};
    library_new(&l);
    library_demo(&l);
    jtext(&l, jget(root(&l), "settings"), "ai_url", "https://model.example");
    jtext(&l, jget(root(&l), "settings"), "ai_model", "fixture");
    network_transport = fixture;
    mode = 46;
    Job j;
    init_job(&j, &l, JOB_PROFILE);
    CHECK(run_job(&j));
    CHECK(j.completed == 2 && j.retry_count == 3);
    J *p = jget(root(&j.library), "profile");
    unsigned counts[6];
    CHECK(profile_counts(p, counts) == 2 && counts[1] == 1 && jnum(p, "classification_pending") == 3);
    CHECK(profile_classification_cached(&j.library, find_repo(&j.library, 1001)) != NULL);
    CHECK(profile_classification_cached(&j.library, find_repo(&j.library, 1003)) == NULL);
    size_t size;
    char *json = library_json(&j.library, &size), e[1024] = {0};
    CHECK(library_parse(&restored, json, size, e));
    free(json);
    drop_job(&j);
    mode = 0;
    classification_fixture_calls = 0;
    init_job(&j, &restored, JOB_PROFILE);
    CHECK(run_job(&j) && j.completed == 5 && j.retry_count == 0);
    CHECK(classification_fixture_calls == 1);
    uint64_t unchanged_version = jnum(find_repo(&restored, 1001), "version");
    CHECK(jnum(find_repo(&j.library, 1001), "version") == unchanged_version);
    CHECK(profile_counts(jget(root(&j.library), "profile"), counts) == 5);
    library_free(&restored);
    library_clone(&restored, &j.library);
    drop_job(&j);
    classification_fixture_calls = 0;
    init_job(&j, &restored, JOB_PROFILE);
    int prior = requests;
    CHECK(run_job(&j) && classification_fixture_calls == 0 && requests == prior + 1);
    p = jget(root(&j.library), "profile");
    CHECK(!strcmp(jstr(p, "narrative_style"), PROFILE_STYLE));
    char *reflection = profile_reflection(p);
    CHECK(reflection && !strchr(reflection, '\n') && !strstr(reflection, "Pulse"));
    mem_free(reflection);
    const char *bad_prose[] = {"你喜欢 yftx888/Pulse。", "你喜欢 Pulse。", "项目编号是1001。", "分段\n描述"};
    char *original = library_json(&j.library, &size);
    for (unsigned k = 0; k < 4; k++) {
        J *invalid = jclone(&j.library, p);
        jtext(&j.library, yyjson_mut_arr_get_first(jget(invalid, "narrative")), "text", bad_prose[k]);
        char *input = yyjson_mut_val_write(invalid, 0, NULL);
        CHECK(!profile_accept(&j.library, input, e));
        check_unchanged(&j.library, original);
        free(input);
    }
    mem_free(original);
    J *r = find_repo(&j.library, 1001);
    jtext(&j.library, r, "ai_summary", "unrelated");
    jnumber(&j.library, r, "stars", 999);
    CHECK(profile_classification_cached(&j.library, r) != NULL);
    yyjson_mut_arr_append(jget(r, "tags"), yyjson_mut_strcpy(j.library.doc, "changed evidence"));
    CHECK(profile_classification_cached(&j.library, r) == NULL);
    drop_job(&j);
    library_free(&restored);
    mode = 34;
    init_job(&j, &l, JOB_PROFILE);
    CHECK(!run_job(&j) && j.completed == 5);
    CHECK(profile_classification_cached(&j.library, find_repo(&j.library, 1001)) != NULL);
    mode = 0;
    classification_fixture_calls = 0;
    Job retry;
    init_job(&retry, &j.library, JOB_PROFILE);
    prior = requests;
    CHECK(run_job(&retry) && classification_fixture_calls == 0 && requests == prior + 1);
    drop_job(&retry);
    drop_job(&j);
    J *batch = yyjson_mut_arr(l.doc);
    yyjson_mut_arr_append(batch, jclone(&l, find_repo(&l, 1001)));
    J *valid = profile_classification_results(
        &l, batch, "{\"classifications\":[{\"id\":1001,\"sufficient\":true,\"axes\":[]},{\"id\":1002");
    CHECK(yyjson_mut_arr_size(valid) == 1);
    WNDCLASSW klass = {0};
    klass.lpfnWndProc = item_commit_fixture;
    klass.hInstance = GetModuleHandleW(NULL);
    klass.lpszClassName = L"StarTestsProfileCommit";
    CHECK(RegisterClassW(&klass) != 0);
    HWND fake = CreateWindowExW(0, klass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                klass.hInstance, NULL);
    CHECK(fake != NULL);
    for (unsigned variant = 0; variant < 3; variant++) {
        wchar_t dir[512];
        swprintf_s(dir, 512, L"artifacts\\profile-commit-%lu-%u", GetCurrentProcessId(), variant);
        memset(&item_commit_storage, 0, sizeof(item_commit_storage));
        CHECK(storage_init(&item_commit_storage, dir, e));
        item_commits = 0;
        item_fail_at = variant ? 99 : 2;
        item_cancel_after_save = variant == 1;
        mode = variant == 2 ? 46 : 0;
        init_job(&j, &l, JOB_PROFILE);
        j.notify = fake;
        bool ok = run_job(&j);
        CHECK(ok == (variant == 2));
        CHECK(j.completed == (variant == 1 ? 1u : 2u) && item_commits == j.completed);
        CHECK(storage_load(&item_commit_storage, &restored, e));
        unsigned cached = 0;
        size_t i, n;
        yyjson_mut_arr_foreach(jget(root(&restored), "repos"), i, n,
                               r) if (profile_classification_cached(&restored, r)) cached++;
        CHECK(cached == item_commits);
        mode = 0;
        init_job(&retry, &restored, JOB_PROFILE);
        CHECK(run_job(&retry) && retry.completed == 5 && retry.retry_count == 0);
        drop_job(&retry);
        drop_job(&j);
        library_free(&restored);
        MSG message;
        while (PeekMessageW(&message, fake, WM_PROGRESS, WM_PROGRESS, PM_REMOVE))
            DispatchMessageW(&message);
    }
    DestroyWindow(fake);
    UnregisterClassW(klass.lpszClassName, klass.hInstance);
    for (unsigned k = 0; k < 90; k++) {
        r = jclone(&l, find_repo(&l, 1001));
        jnumber(&l, r, "id", 40000 + k);
        yyjson_mut_arr_append(jget(root(&l), "repos"), r);
    }
    mode = 46;
    classification_fixture_calls = 0;
    init_job(&j, &l, JOB_PROFILE);
    CHECK(run_job(&j) && classification_fixture_calls > 3 && j.completed == 2 && j.retry_count == 93);
    drop_job(&j);
    size_t i, n;
    yyjson_mut_arr_foreach(jget(root(&l), "repos"), i, n, r) jtext(&l, r, "description", "");
    mode = 0;
    prior = requests;
    init_job(&j, &l, JOB_PROFILE);
    CHECK(!run_job(&j) && j.completed == 95 && j.insufficient == 95 && requests == prior);
    drop_job(&j);
    library_free(&l);
}
static void test_token_budgets(void) {
    Library l = {0};
    network_transport = fixture;
    library_new(&l);
    library_demo(&l);
    CHECK(version_newer("v0.0.6", "0.0.5") && version_newer("0.10.0", "0.9.9"));
    CHECK(!version_newer("0.0.5", "0.0.5") && !version_newer("0.0.4", "0.0.5"));
    CHECK(!version_newer("9.0.0-rc1", "0.0.5") && !version_newer("https://evil.example", "0.0.5"));
    char latest[32] = {0};
    mode = 0;
    CHECK(!update_latest("", NULL, latest));
    CHECK(update_latest("fixture-token", NULL, latest) && !strcmp(latest, "v0.0.6"));
    mode = 71;
    CHECK(!update_latest("fixture-token", NULL, latest));
    mode = 72;
    CHECK(!update_latest("fixture-token", NULL, latest));
    mode = 0;
    Job support;
    init_job(&support, &l, JOB_SUPPORT);
    CHECK(run_job(&support) && jbool(find_repo(&support.library, 1430840985), "active"));
    CHECK(!find_candidate(&support.library, 1430840985));
    drop_job(&support);
    J *candidate = find_repo(&l, 1001);
    jnumber(&l, candidate, "stars", 500);
    J *topic = yyjson_mut_arr(l.doc);
    yyjson_mut_arr_append(topic, yyjson_mut_strcpy(l.doc, "desktop"));
    jset(&l, candidate, "topics", topic);
    uint64_t weight = discovery_weight(&l, candidate);
    J *feedback = yyjson_mut_obj(l.doc);
    jnumber(&l, feedback, "id", 888888);
    jtext(&l, feedback, "state", "ignored");
    jset(&l, feedback, "repo", jclone(&l, candidate));
    yyjson_mut_arr_append(jget(root(&l), "feedback"), feedback);
    CHECK(discovery_weight(&l, candidate) < weight);
    uint64_t lower = discovery_weight(&l, candidate);
    jnumber(&l, candidate, "stars", 1000);
    CHECK(discovery_weight(&l, candidate) > lower);
    jtext(&l, feedback, "state", "later");
    CHECK(discovery_weight(&l, candidate) == 1000);
    jtext(&l, jget(root(&l), "settings"), "ai_url", "https://model.example");
    jtext(&l, jget(root(&l), "settings"), "ai_model", "fixture");
    jnumber(&l, jget(root(&l), "settings"), "ai_concurrency", 1);
    for (mode = 60; mode <= 65; mode++) {
        budget_calls = budget_max_items = 0;
        memset(budget_seen, 0, sizeof(budget_seen));
        Job j;
        init_job(&j, &l, JOB_SUMMARY);
        CHECK(run_job(&j) == (mode != 64 && mode != 65));
        CHECK(j.completed == (mode == 64 || mode == 65 ? 0u : 5u));
        if (mode == 60)
            CHECK(budget_calls == 1 && budget_max_items == 5 && j.usage_known);
        if (mode == 61)
            CHECK(budget_calls > 1 && budget_seen[0] == 1);
        if (mode == 62) {
            CHECK(budget_calls > 1 &&
                  jnum(jget(jget(root(&j.library), "settings"), "ai_budget"), "context") == 4096);
        }
        if (mode == 63) {
            CHECK(budget_calls == 2);
            CHECK(jnum(jget(jget(root(&j.library), "settings"), "ai_budget"), "parameter") == 1);
        }
        if (mode == 64)
            CHECK(budget_calls == 1);
        if (mode == 65)
            for (unsigned k = 0; k < 5; k++)
                CHECK(budget_seen[k] == 3);
        drop_job(&j);
    }
    mode = 60;
    J *repo;
    size_t i, n;
    Buffer long_text = {0};
    for (unsigned k = 0; k < 800; k++)
        buf_add(&long_text, "开发工具");
    yyjson_mut_arr_foreach(jget(root(&l), "repos"), i, n, repo)
        jtext(&l, repo, "description", long_text.data);
    buf_drop(&long_text);
    budget_calls = budget_max_items = 0;
    Job j;
    init_job(&j, &l, JOB_SUMMARY);
    CHECK(run_job(&j) && j.completed == 5 && budget_calls > 1 && budget_max_items < 5);
    drop_job(&j);
    jtext(&l, jget(root(&l), "settings"), "ai_url", "https://api.deepseek.com");
    init_job(&j, &l, JOB_SUMMARY);
    CHECK(run_job(&j));
    drop_job(&j);
    library_free(&l);
    mode = 0;
}
static void test_endpoints_and_partial_results(void) {
    const char *inputs[] = {"https://model.example",
                            " https://model.example/ \r\n",
                            "https://model.example/v1",
                            "https://model.example/v1/",
                            "https://model.example/v1/chat",
                            "https://model.example/v1/chat/completions/",
                            "https://model.example/chat/completions",
                            "https://model.example/gateway/v1",
                            "http://localhost:8080",
                            "https://model.example/v2",
                            "https://model.example/openai?api-version=2026-01-01",
                            "",
                            "  "};
    const char *outputs[] = {"https://model.example/v1/chat/completions",
                             "https://model.example/v1/chat/completions",
                             "https://model.example/v1/chat/completions",
                             "https://model.example/v1/chat/completions",
                             "https://model.example/v1/chat/completions",
                             "https://model.example/v1/chat/completions",
                             "https://model.example/chat/completions",
                             "https://model.example/gateway/v1/chat/completions",
                             "http://localhost:8080/v1/chat/completions",
                             "https://model.example/v2/chat/completions",
                             "https://model.example/openai/v1/chat/completions?api-version=2026-01-01",
                             "",
                             ""};
    char e[1024] = {0};
    for (size_t k = 0; k < sizeof(inputs) / sizeof(*inputs); k++) {
        char *url = ai_endpoint(inputs[k], e);
        CHECK(url && !strcmp(url, outputs[k]));
        char *again = url ? ai_endpoint(url, e) : NULL;
        CHECK(again && !strcmp(again, outputs[k]));
        mem_free(url);
        mem_free(again);
    }
    const char *bad[] = {"http://model.example",      "https://name:password@model.example",
                         "ftp://example.com",         "https://model.example/#fragment",
                         "https://model.example/a b", "bad-url"};
    for (unsigned k = 0; k < sizeof(bad) / sizeof(*bad); k++) {
        char *url = ai_endpoint(bad[k], e);
        CHECK(url == NULL);
        mem_free(url);
    }
    Library l = {0};
    library_new(&l);
    library_demo(&l);
    J *batch = yyjson_mut_arr(l.doc);
    for (unsigned k = 0; k < 3; k++)
        yyjson_mut_arr_append(batch, jclone(&l, find_repo(&l, 1001 + k)));
    J *accepted =
        summary_results(&l, batch,
                        "{\"summaries\":[{\"id\":1001,\"summary\":\"原生桌面工具。\",\"status\":\"ready\"},"
                        "{\"id\":1002,\"summary\":\"English only\",\"status\":\"ready\"},"
                        "{\"id\":9999,\"summary\":\"虚构仓库。\",\"status\":\"ready\"}]}");
    CHECK(yyjson_mut_arr_size(accepted) == 1);
    CHECK(summary_apply(&l, yyjson_mut_arr_get_first(batch), yyjson_mut_arr_get_first(accepted)));
    CHECK(summary_current(find_repo(&l, 1001)) && !summary_current(find_repo(&l, 1002)));
    accepted = summary_results(&l, batch,
                               "{\"summaries\":[{\"id\":1001,\"summary\":\"重复一。\",\"status\":\"ready\"},"
                               "{\"id\":1001,\"summary\":\"重复二。\",\"status\":\"ready\"},"
                               "{\"id\":1002,\"summary\":\"读写 JSON 的开发库。\",\"status\":\"ready\"}]}");
    CHECK(yyjson_mut_arr_size(accepted) == 1 && jnum(yyjson_mut_arr_get_first(accepted), "id") == 1002);
    accepted = summary_results(
        &l, batch,
        " { \"summaries\" : [ {\"id\":1002,\"summary\":\"读写 JSON 的开发库。\",\"status\":\"ready\"},"
        "{\"id\":1003,\"summary\":\"unfinished");
    CHECK(yyjson_mut_arr_size(accepted) == 1 && jnum(yyjson_mut_arr_get_first(accepted), "id") == 1002);
    jtext(&l, find_repo(&l, 1002), "description", "source changed");
    CHECK(!summary_apply(&l, yyjson_mut_arr_get(batch, 1), yyjson_mut_arr_get_first(accepted)));
    J *settings = jget(root(&l), "settings");
    jtext(&l, settings, "ai_url", "https://model.example");
    jtext(&l, settings, "ai_model", "fixture");
    network_transport = fixture;
    mode = 0;
    Job j;
    init_job(&j, &l, JOB_AI_TEST);
    CHECK(run_job(&j));
    drop_job(&j);
    J *empty = find_repo(&l, 1003);
    jtext(&l, empty, "description", " \t ");
    jset(&l, empty, "topics", yyjson_mut_arr(l.doc));
    init_job(&j, &l, JOB_SUMMARY);
    j.repo_id = 1003;
    int before = requests;
    CHECK(run_job(&j) && j.completed == 1 && j.insufficient == 1 && j.retry_count == 0);
    CHECK(requests == before && summary_current(find_repo(&j.library, 1003)));
    drop_job(&j);
    mode = 45;
    jtext(&l, find_repo(&l, 1001), "summary_status", "");
    init_job(&j, &l, JOB_SUMMARY);
    CHECK(!run_job(&j) && j.completed == 2 && j.retry_count == 3 && j.insufficient == 1);
    CHECK(summary_current(find_repo(&j.library, 1003)) && !summary_current(find_repo(&j.library, 1002)));
    mode = 0;
    Job retry;
    init_job(&retry, &j.library, JOB_SUMMARY);
    CHECK(run_job(&retry) && retry.completed == 3);
    CHECK(summary_current(find_repo(&retry.library, 1002)));
    drop_job(&retry);
    drop_job(&j);
    WNDCLASSW klass = {0};
    klass.lpfnWndProc = item_commit_fixture;
    klass.hInstance = GetModuleHandleW(NULL);
    klass.lpszClassName = L"StarTestsPerProjectCommit";
    CHECK(RegisterClassW(&klass) != 0);
    HWND fake = CreateWindowExW(0, klass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                klass.hInstance, NULL);
    CHECK(fake != NULL);
    for (unsigned variant = 0; variant < 2; variant++) {
        size_t i, n;
        J *r;
        yyjson_mut_arr_foreach(jget(root(&l), "repos"), i, n, r) {
            jtext(&l, r, "summary_status", "");
            jtext(&l, r, "description", "Developer utility metadata");
        }
        wchar_t dir[512];
        swprintf_s(dir, 512, L"artifacts\\summary-commit-%lu-%u", GetCurrentProcessId(), variant);
        memset(&item_commit_storage, 0, sizeof(item_commit_storage));
        CHECK(storage_init(&item_commit_storage, dir, e));
        item_commits = 0;
        item_fail_at = variant ? 99 : 2;
        item_cancel_after_save = variant != 0;
        init_job(&j, &l, JOB_SUMMARY);
        j.notify = fake;
        CHECK(!run_job(&j));
        CHECK(j.completed == (variant ? 1u : 2u) && item_commits == j.completed);
        CHECK(j.retry_count == 5 - j.completed);
        Library saved = {0};
        CHECK(storage_load(&item_commit_storage, &saved, e));
        unsigned ready = 0;
        yyjson_mut_arr_foreach(jget(root(&saved), "repos"), i, n, r) {
            if (summary_current(r))
                ready++;
        }
        CHECK(ready == item_commits);
        uint64_t saved_version = jnum(find_repo(&saved, 1001), "version");
        init_job(&retry, &saved, JOB_SUMMARY);
        CHECK(run_job(&retry) && retry.completed == 5 - ready);
        CHECK(jnum(find_repo(&retry.library, 1001), "version") == saved_version);
        drop_job(&retry);
        drop_job(&j);
        library_free(&saved);
        MSG message;
        while (PeekMessageW(&message, fake, WM_PROGRESS, WM_PROGRESS, PM_REMOVE))
            DispatchMessageW(&message);
    }
    DestroyWindow(fake);
    UnregisterClassW(klass.lpszClassName, klass.hInstance);
    library_free(&l);
}
static void test_storage_migration(void) {
    Storage *stores = mem_alloc(5 * sizeof(Storage));
    Library full = {0}, empty = {0}, restored = {0};
    wchar_t local[512], dir[1024], file[32768], source[32768];
    char error[1024] = {0};
    CHECK(stores != NULL);
    if (!stores)
        return;
    swprintf_s(local, 512, L"artifacts\\migration-%lu\\old-local", GetCurrentProcessId());
    swprintf_s(dir, 1024, L"%s\\StarLibrary", local);
    CHECK(storage_init(&stores[0], dir, error));
    CHECK(library_new(&empty));
    jtext(&empty, root(&empty), "account", "preview");
    jnumber(&empty, root(&empty), "revision", 1000);
    CHECK(storage_save(&stores[0], &empty, error));
    swprintf_s(dir, 1024, L"%s\\Packages\\Example.Host\\LocalCache\\Local\\StarLibrary", local);
    CHECK(storage_init(&stores[1], dir, error));
    CHECK(library_new(&full));
    library_demo(&full);
    CHECK(profile_accept(&full, profile_json, error));
    jtext(&full, find_repo(&full, 1001), "ai_summary", "迁移保留中文概括");
    CHECK(storage_save(&stores[1], &full, error));
    storage_path(&stores[1], L"credentials.bin", source);
    CHECK(file_write(source, "opaque-encrypted-fixture", 24));
    swprintf_s(dir, 1024, L"artifacts\\migration-%lu\\stable", GetCurrentProcessId());
    CHECK(storage_init(&stores[2], dir, error));
    CHECK(storage_migrate_from(&stores[2], local, &restored, error) && stores[2].recovered);
    CHECK(find_repo(&restored, 1001) && yyjson_mut_is_obj(jget(root(&restored), "profile")));
    CHECK(!strcmp(jstr(find_repo(&restored, 1001), "ai_summary"), "迁移保留中文概括"));
    storage_path(&stores[2], L"credentials.bin", file);
    size_t bytes;
    char *data = file_read(file, &bytes);
    CHECK(data && bytes == 24 && !memcmp(data, "opaque-encrypted-fixture", 24));
    mem_free(data);
    CHECK(GetFileAttributesW(source) != INVALID_FILE_ATTRIBUTES);
    CHECK(!storage_migrate_from(&stores[2], local, &restored, error));
    CHECK(storage_load(&stores[2], &restored, error) && find_repo(&restored, 1001));
    storage_path(&stores[0], L"library.json", file);
    CHECK(file_write(file, "{broken", 7));
    swprintf_s(dir, 1024, L"artifacts\\migration-%lu\\skip-broken", GetCurrentProcessId());
    CHECK(storage_init(&stores[3], dir, error));
    CHECK(storage_migrate_from(&stores[3], local, &restored, error) && find_repo(&restored, 1001));
    jtext(&full, root(&full), "account", "other-account");
    swprintf_s(dir, 1024, L"%s\\Packages\\Another.Host\\LocalCache\\Local\\StarLibrary", local);
    CHECK(storage_init(&stores[4], dir, error) && storage_save(&stores[4], &full, error));
    swprintf_s(dir, 1024, L"artifacts\\migration-%lu\\conflict", GetCurrentProcessId());
    CHECK(storage_init(&stores[3], dir, error));
    CHECK(!storage_migrate_from(&stores[3], local, &restored, error));
    CHECK(!strcmp(jstr(root(&restored), "account"), "preview"));
    storage_path(&stores[3], L"library.json", file);
    CHECK(GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES);
    library_free(&restored);
    library_free(&empty);
    library_free(&full);
    mem_free(stores);
}
static void test_storage_restart(void) {
    Library written = {0}, loaded = {0};
    Storage storage = {0};
    wchar_t dir[512], path[32768], backup[32768];
    char error[1024] = {0};
    swprintf_s(dir, 512, L"artifacts\\restart-%lu", GetCurrentProcessId());
    CHECK(storage_init(&storage, dir, error));
    CHECK(library_new(&written));
    library_demo(&written);
    CHECK(profile_accept(&written, profile_json, error));
    jtext(&written, find_repo(&written, 1001), "ai_summary", "重启后仍然保留的中文概括。");
    jtext(&written, jget(root(&written), "settings"), "ai_url", "https://example.com/v1/chat/completions");
    jtext(&written, jget(root(&written), "settings"), "ai_model", "saved-model");
    CHECK(storage_save(&storage, &written, error));
    CHECK(storage_save(&storage, &written, error));
    CHECK(storage_load(&storage, &loaded, error) && !storage.recovered);
    CHECK(!strcmp(jstr(find_repo(&loaded, 1001), "ai_summary"), "重启后仍然保留的中文概括。"));
    CHECK(!strcmp(jstr(jget(root(&loaded), "settings"), "ai_model"), "saved-model"));
    CHECK(yyjson_mut_is_obj(jget(root(&loaded), "profile")));
    storage_path(&storage, L"library.json", path);
    storage_path(&storage, L"library.json.bak", backup);
    CHECK(file_write(path, "{broken", 7));
    CHECK(storage_load(&storage, &loaded, error) && storage.recovered && !*error);
    CHECK(find_repo(&loaded, 1001) && yyjson_mut_is_obj(jget(root(&loaded), "profile")));
    wchar_t pattern[32768];
    storage_path(&storage, L"library.json.unreadable-*.bak", pattern);
    WIN32_FIND_DATAW info;
    HANDLE archive = FindFirstFileW(pattern, &info);
    CHECK(archive != INVALID_HANDLE_VALUE);
    if (archive != INVALID_HANDLE_VALUE) {
        wchar_t archived[32768];
        storage_path(&storage, info.cFileName, archived);
        size_t n;
        char *text = file_read(archived, &n);
        CHECK(text && n == 7 && !memcmp(text, "{broken", 7));
        mem_free(text);
        FindClose(archive);
    }
    CHECK(DeleteFileW(path));
    CHECK(storage_load(&storage, &loaded, error) && storage.recovered);
    CHECK(find_repo(&loaded, 1001));
    CHECK(file_write(path, "", 0));
    CHECK(storage_load(&storage, &loaded, error) && storage.recovered && find_repo(&loaded, 1001));
    HANDLE lock = CreateFileW(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(lock != INVALID_HANDLE_VALUE);
    CHECK(!storage_load(&storage, &loaded, error) && find_repo(&loaded, 1001));
    if (lock != INVALID_HANDLE_VALUE)
        CloseHandle(lock);
    CHECK(file_write(path, "{broken", 7) && file_write(backup, "{bad", 4));
    CHECK(!storage_load(&storage, &loaded, error) && find_repo(&loaded, 1001));
    size_t n;
    char *text = file_read(path, &n);
    CHECK(text && n == 7 && !memcmp(text, "{broken", 7));
    mem_free(text);
    CHECK(DeleteFileW(path));
    CHECK(!storage_load(&storage, &loaded, error) && find_repo(&loaded, 1001));
    library_free(&loaded);
    library_free(&written);
}
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--live")) {
        const char *token = getenv("GITHUB_TOKEN");
        if (!token || !*token) {
            puts("Live checks skipped: explicitly supply GITHUB_TOKEN to run.");
            return 0;
        }
        const char *urls[] = {"https://api.github.com/user",
                              "https://api.github.com/user/starred?per_page=1&page=1",
                              "https://api.github.com/search/repositories?q=win32&per_page=1"};
        bool all_ok = true;
        for (int k = 0; k < 3; k++) {
            Request r = {L"GET", urls[k], token, NULL, true};
            Response res = {0};
            bool ok = http_request(&r, &res, NULL);
            printf("Authenticated GitHub endpoint %d: HTTP %lu, %s\n", k + 1, res.status,
                   ok ? "PASS" : res.error);
            all_ok = all_ok && ok;
            response_free(&res);
        }
        return all_ok ? 0 : 1;
    }
    test_token_budgets();
    test_endpoints_and_partial_results();
    test_profile_resume();
    test_storage_restart();
    test_storage_migration();
    Library l = {0}, copy = {0};
    char error[1024] = {0};
    CHECK(library_new(&l));
    CHECK(library_validate(&l, error));
    library_demo(&l);
    CHECK(library_validate(&l, error));
    CHECK(find_repo(&l, 1001) != NULL);
    CHECK(repo_annotate(&l, 1001, "本地工具, Windows", "含中文、换行\n和引号 \" 以及 \\。", error));
    CHECK(jnum(find_repo(&l, 1001), "version") == 1);
    CHECK(yyjson_mut_arr_size(jget(find_repo(&l, 1001), "tags")) == 2);
    jnumber(&l, root(&l), "precision_probe", UINT64_C(18446744073709551614));
    jset(&l, root(&l), "fraction_probe", yyjson_mut_real(l.doc, 1.25));
    size_t len;
    char *json = library_json(&l, &len);
    CHECK(json && library_parse(&copy, json, len, error));
    CHECK(!strcmp(jstr(find_repo(&copy, 1001), "note"), jstr(find_repo(&l, 1001), "note")));
    CHECK(jnum(root(&copy), "precision_probe") == UINT64_C(18446744073709551614));
    CHECK(yyjson_mut_get_real(jget(root(&copy), "fraction_probe")) == 1.25);
    free(json);
    CHECK(!library_parse(&copy, "{broken}", 8, error));
    CHECK(find_repo(&copy, 1001) != NULL);
    uint64_t before = l.revision;
    CHECK(profile_accept(&l, profile_json, error));
    CHECK(jnum(jget(root(&l), "profile"), "based_on_revision") == l.revision);
    CHECK(l.revision == before + 1);
    before = l.revision;
    CHECK(!profile_accept(&l,
                          "{\"summary\":\"fake\",\"interests\":[{\"name\":\"fake\",\"reason\":\"fake\","
                          "\"confidence\":\"high\",\"evidence\":[999999]}],\"preferences\":[]}",
                          error));
    CHECK(l.revision == before);
    CHECK(!profile_accept(&l,
                          "{\"summary\":\"fake\",\"interests\":[{\"name\":\"x\",\"reason\":\"x\","
                          "\"evidence\":[]}],\"preferences\":[]}",
                          error));
    Buffer escaped = {0};
    buf_json_string(&escaped, "a\n\"\\中文");
    yyjson_doc *d = yyjson_read(escaped.data, escaped.len, 0);
    CHECK(d != NULL);
    CHECK(!strcmp(yyjson_get_str(yyjson_doc_get_root(d)), "a\n\"\\中文"));
    yyjson_doc_free(d);
    buf_drop(&escaped);
    J *settings = jget(root(&l), "settings");
    jtext(&l, settings, "username", "preview");
    jtext(&l, settings, "ai_url", "https://model.example/v1/chat/completions");
    jtext(&l, settings, "ai_model", "fixture-model");
    jnumber(&l, settings, "ai_concurrency", 1);
    network_transport = fixture;
    Job job;
    mode = 0;
    init_job(&job, &l, JOB_SYNC);
    CHECK(run_job(&job));
    CHECK(!strcmp(jstr(find_repo(&job.library, 1001), "note"), jstr(find_repo(&l, 1001), "note")));
    CHECK(yyjson_mut_arr_size(jget(find_repo(&job.library, 1001), "tags")) == 2);
    CHECK(!jbool(find_repo(&job.library, 1002), "active"));
    CHECK(!strcmp(jstr(root(&job.library), "account"), "preview"));
    CHECK(jnum(find_repo(&job.library, 1001), "owner_id") == 123);
    CHECK(strstr(jstr(find_repo(&job.library, 1001), "avatar_url"), "&s=64") != NULL);
    drop_job(&job);
    mode = 4;
    init_job(&job, &l, JOB_SYNC);
    CHECK(run_job(&job));
    CHECK(jbool(find_repo(&job.library, 1002), "active"));
    CHECK(!strcmp(jstr(find_repo(&job.library, 1002), "visibility_status"), "unverified"));
    drop_job(&job);
    mode = 0;
    init_job(&job, &l, JOB_SYNC);
    secure_free(job.github_key);
    job.github_key = NULL;
    int no_token_requests = requests;
    CHECK(!run_job(&job));
    CHECK(jbool(find_repo(&job.library, 1002), "active") && requests == no_token_requests);
    drop_job(&job);
    mode = 8;
    init_job(&job, &l, JOB_SYNC);
    CHECK(!run_job(&job));
    CHECK(job.library.revision == l.revision);
    drop_job(&job);
    mode = 9;
    init_job(&job, &l, JOB_SYNC);
    CHECK(!run_job(&job));
    CHECK(job.library.revision == l.revision);
    drop_job(&job);
    mode = 0;
    init_job(&job, &l, JOB_SYNC);
    SetEvent(job.cancel);
    int request_count = requests;
    CHECK(!run_job(&job));
    CHECK(requests == request_count);
    drop_job(&job);
    mode = 0;
    init_job(&job, &l, JOB_PROFILE);
    CHECK(run_job(&job));
    CHECK(yyjson_mut_is_obj(jget(root(&job.library), "profile")));
    CHECK(library_validate(&job.library, error));
    drop_job(&job);
    Library large = {0};
    CHECK(library_clone(&large, &l));
    for (int k = 0; k < 121; k++) {
        J *r = jclone(&large, find_repo(&large, 1001));
        jnumber(&large, r, "id", 2000 + k);
        yyjson_mut_arr_append(jget(root(&large), "repos"), r);
    }
    init_job(&job, &large, JOB_PROFILE);
    int prior_requests = requests;
    CHECK(run_job(&job));
    CHECK(requests - prior_requests > 5 && requests - prior_requests < 126);
    CHECK(library_validate(&job.library, error));
    drop_job(&job);
    library_free(&large);
    mode = 6;
    init_job(&job, &l, JOB_PROFILE);
    CHECK(!run_job(&job));
    CHECK(job.library.revision == l.revision);
    drop_job(&job);
    mode = 0;
    init_job(&job, &l, JOB_PROFILE);
    CHECK(run_job(&job));
    jset(&l, root(&l), "profile", jclone(&l, jget(root(&job.library), "profile")));
    drop_job(&job);
    init_job(&job, &l, JOB_DISCOVER);
    CHECK(run_job(&job));
    CHECK(find_candidate(&job.library, 9991) != NULL);
    CHECK(summary_current(find_candidate(&job.library, 9991)));
    CHECK(strstr(jstr(find_candidate(&job.library, 9991), "recommendation"), "动手构建") != NULL);
    CHECK(library_validate(&job.library, error));
    drop_job(&job);
    mode = 7;
    init_job(&job, &l, JOB_DISCOVER);
    CHECK(!run_job(&job));
    CHECK(job.library.revision == l.revision);
    drop_job(&job);
    mode = 0;
    init_job(&job, &l, JOB_STAR);
    job.repo_id = 1001;
    job.desired_star = false;
    CHECK(run_job(&job));
    CHECK(!jbool(find_repo(&job.library, 1001), "active"));
    CHECK(*jstr(find_repo(&job.library, 1001), "note"));
    drop_job(&job);
    init_job(&job, &l, JOB_STAR);
    job.repo_id = 1001;
    job.desired_star = true;
    J *renamed = jclone(&job.library, find_repo(&job.library, 1001));
    jtext(&job.library, renamed, "full_name", "example/renamed-pulse");
    yyjson_mut_arr_append(jget(root(&job.library), "discoveries"), renamed);
    CHECK(run_job(&job));
    CHECK(!strcmp(jstr(find_repo(&job.library, 1001), "full_name"), "example/renamed-pulse"));
    CHECK(!strcmp(jstr(find_repo(&job.library, 1001), "note"), jstr(find_repo(&l, 1001), "note")));
    drop_job(&job);
    init_job(&job, &l, JOB_AI_TEST);
    CHECK(run_job(&job));
    drop_job(&job);
    test_discovery(&l);
    test_token_summary_profile(&l);
    test_parallel_and_avatars(&l);
    test_avatar_cache();
    Storage st = {0};
    wchar_t dir[512];
    swprintf_s(dir, 512, L"artifacts\\test-%lu", GetCurrentProcessId());
    CHECK(storage_init(&st, dir, error));
    CHECK(storage_save(&st, &l, error));
    CHECK(storage_save(&st, &l, error));
    Library saved = {0};
    CHECK(storage_load(&st, &saved, error));
    CHECK(saved.revision == l.revision);
    library_free(&saved);
    st.github_key = str_copy("secret-test-token");
    st.ai_key = str_copy("secret-test-model-key");
    CHECK(storage_keys_save(&st, error));
    Storage second = {0};
    CHECK(storage_init(&second, dir, error));
    storage_keys_load(&second);
    CHECK(second.github_key && !strcmp(second.github_key, "secret-test-token"));
    CHECK(second.ai_key && !strcmp(second.ai_key, "secret-test-model-key"));
    CHECK(storage_commit(&st, &l, &l, "", st.ai_key, error));
    CHECK(!*st.github_key && !strcmp(st.ai_key, "secret-test-model-key"));
    CHECK(storage_commit(&st, &l, &l, "replacement-token", st.ai_key, error));
    wchar_t locked_path[32768];
    storage_path(&st, L"library.json", locked_path);
    HANDLE commit_lock =
        CreateFileW(locked_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(commit_lock != INVALID_HANDLE_VALUE);
    CHECK(!storage_commit(&st, &l, &l, "must-not-persist", "must-not-persist-ai", error));
    CHECK(!strcmp(st.github_key, "replacement-token") && !strcmp(st.ai_key, "secret-test-model-key"));
    if (commit_lock != INVALID_HANDLE_VALUE)
        CloseHandle(commit_lock);
    Storage rollback = {0};
    CHECK(storage_init(&rollback, dir, error));
    storage_keys_load(&rollback);
    CHECK(rollback.github_key && !strcmp(rollback.github_key, "replacement-token"));
    CHECK(rollback.ai_key && !strcmp(rollback.ai_key, "secret-test-model-key"));
    secure_free(rollback.github_key);
    secure_free(rollback.ai_key);
    storage_path(&st, L"credentials.bin", locked_path);
    commit_lock = CreateFileW(locked_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(commit_lock != INVALID_HANDLE_VALUE);
    CHECK(!storage_commit(&st, &l, &l, "denied-token", st.ai_key, error));
    CHECK(!strcmp(st.github_key, "replacement-token"));
    if (commit_lock != INVALID_HANDLE_VALUE)
        CloseHandle(commit_lock);
    secure_free(st.github_key);
    secure_free(st.ai_key);
    secure_free(second.github_key);
    secure_free(second.ai_key);
    wchar_t data_path[32768];
    storage_path(&st, L"library.json", data_path);
    json = file_read(data_path, &len);
    CHECK(json && !strstr(json, "secret-test-token") && !strstr(json, "secret-test-model-key"));
    mem_free(json);
    wchar_t backup[32768];
    storage_path(&st, L"library.json.bak", backup);
    CHECK(GetFileAttributesW(backup) != INVALID_FILE_ATTRIBUTES);
    HANDLE locked = CreateFileW(data_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(!storage_save(&st, &l, error));
    if (locked != INVALID_HANDLE_VALUE)
        CloseHandle(locked);
    Library after_failed_save = {0};
    CHECK(storage_load(&st, &after_failed_save, error));
    CHECK(after_failed_save.revision == l.revision);
    library_free(&after_failed_save);
    library_free(&copy);
    library_free(&l);
    printf("%d checks, %d failures; deterministic network fixtures; no real Star writes.\n", checks,
           failures);
    return failures ? 1 : 0;
}
