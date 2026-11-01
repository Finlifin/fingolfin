#include "fingolfin/session.h"
#include <inttypes.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE (64u * 1024u * 1024u)
static struct json_object *get(struct json_object *o, const char *key) {
    struct json_object *v = NULL;
    json_object_object_get_ex(o, key, &v);
    return v;
}
static const char *string(struct json_object *o) {
    if (!json_object_is_type(o, json_type_string))
        return NULL;
    const char *s = json_object_get_string(o);
    return s && strlen(s) == (size_t)json_object_get_string_len(o) ? s : NULL;
}
static int equal(struct json_object *o, const char *s) {
    const char *v = string(o);
    return v && !strcmp(v, s);
}
static int decimal(const char *s, uint64_t *out) {
    if (!s || !*s || (s[0] == '0' && s[1]))
        return 0;
    uint64_t n = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || n > (UINT64_MAX - (unsigned)(*s - '0')) / 10)
            return 0;
        n = n * 10 + (unsigned)(*s - '0');
    }
    *out = n;
    return 1;
}
/* Count raw keys outside strings: comparing to the exact expected DOM field
 * count rejects duplicate keys (including escaped spellings) before mutation.
 */
static size_t key_count(const char *line, size_t n) {
    size_t keys = 0;
    int quoted = 0, escaped = 0;
    for (size_t i = 0; i < n; i++) {
        char c = line[i];
        if (quoted) {
            if (escaped)
                escaped = 0;
            else if (c == '\\')
                escaped = 1;
            else if (c == '"')
                quoted = 0;
        } else if (c == '"')
            quoted = 1;
        else if (c == ':')
            keys++;
    }
    return keys;
}
static int decode(struct json_object *root, size_t keys, FgSessionRequest *r) {
    if (!json_object_is_type(root, json_type_object) || json_object_object_length(root) != 4 ||
        !equal(get(root, "jsonrpc"), "2.0") || !decimal(string(get(root, "id")), &r->sequence) ||
        !r->sequence)
        return 0;
    struct json_object *p = get(root, "params");
    if (!json_object_is_type(p, json_type_object))
        return 0;
    struct json_object *v = get(p, "version");
    if (!json_object_is_type(v, json_type_int) || json_object_get_int64(v) != 1 ||
        !decimal(string(get(p, "session_id")), &r->session_id) ||
        !decimal(string(get(p, "generation")), &r->expected_generation))
        return 0;
    size_t count = 3;
    if (equal(get(root, "method"), "session.status"))
        r->method = FG_SESSION_STATUS;
    else if (equal(get(root, "method"), "program.list"))
        r->method = FG_SESSION_LIST;
    else if (equal(get(root, "method"), "session.close"))
        r->method = FG_SESSION_CLOSE;
    else if (equal(get(root, "method"), "program.unload")) {
        r->method = FG_SESSION_UNLOAD;
        count++;
        if (!decimal(string(get(p, "program_id")), &r->program_id))
            return 0;
    } else if (equal(get(root, "method"), "program.load")) {
        r->method = FG_SESSION_LOAD;
        count += 3;
        const char *package = string(get(p, "package"));
        r->expected_program_hash = string(get(p, "program_hash"));
        r->expected_model_hash = string(get(p, "model_hash"));
        if (!package || !r->expected_program_hash || !r->expected_model_hash)
            return 0;
        r->package = (FgBytes){(const unsigned char *)package, strlen(package)};
        if (r->package.size > FG_PACKAGE_MAX_BYTES)
            return 0;
        if (!*r->expected_program_hash)
            r->expected_program_hash = NULL;
        if (!*r->expected_model_hash)
            r->expected_model_hash = NULL;
    } else
        return 0;
    return (size_t)json_object_object_length(p) == count && keys == 4 + count;
}
static int invalid(void) {
    return puts("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,"
                "\"message\":\"invalid request\"}}") >= 0 &&
           !fflush(stdout);
}
static int quoted(const char *text) {
    struct json_object *value = json_object_new_string(text);
    if (!value)
        return 0;
    const char *encoded = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
    int ok = encoded && fputs(encoded, stdout) >= 0;
    json_object_put(value);
    return ok;
}
static int entries(const FgPackage *package) {
    if (fputs(",\"entries\":[", stdout) < 0)
        return 0;
    for (size_t i = 0; i < fg_package_count(package); i++) {
        const FgPackageEntry *entry = fg_package_entry(package, i);
        if (printf("%s{\"function\":", i ? "," : "") < 0 || !quoted(entry->function) ||
            fputs(",\"hook\":", stdout) < 0 || !quoted(entry->hook ? entry->hook : "") ||
            printf(",\"frame_bytes\":\"%" PRIu64 "\",\"helper_mask\":%" PRIu32 "}",
                   entry->schema.frame_bytes, entry->helper_mask) < 0)
            return 0;
    }
    return putchar(']') != EOF;
}
static int programs(FgSession *session) {
    if (fputs(",\"programs\":[", stdout) < 0)
        return 0;
    uint64_t after = 0, id;
    FgProgram *program = NULL;
    FgStatus status;
    while ((status = fg_session_next(session, after, &id, &program)) == FG_STATUS_OK) {
        const FgPackage *package = fg_program_package(program);
        /* Source hashes are validated lowercase SHA256 hex by the package loader.
         */
        int written = printf("%s{\"program_id\":\"%" PRIu64
                             "\",\"program_hash\":\"%s\",\"model_hash\":\"%s\",\"entry_count\":%zu",
                             after ? "," : "", id, fg_package_program_hash(package),
                             fg_package_model_hash(package), fg_package_count(package));
        int ok = written >= 0 && entries(package) && putchar('}') != EOF;
        fg_program_release(program);
        if (!ok)
            return 0;
        after = id;
    }
    return status == FG_STATUS_NOT_FOUND && putchar(']') != EOF;
}
static int respond(FgSession *session, const char *line, size_t length) {
    struct json_tokener *tok = json_tokener_new_ex(8);
    if (!tok)
        return 0;
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    struct json_object *root = json_tokener_parse_ex(tok, line, (int)length);
    size_t end = json_tokener_get_parse_end(tok);
    while (end < length && (line[end] == ' ' || line[end] == '\t' || line[end] == '\r'))
        end++;
    FgSessionRequest request = {.version = 1, .struct_bytes = sizeof(request)};
    int valid = json_tokener_get_error(tok) == json_tokener_success && end == length &&
                decode(root, key_count(line, length), &request);
    json_tokener_free(tok);
    if (!valid) {
        json_object_put(root);
        return invalid();
    }
    FgSessionReply reply;
    FgStatus status = fg_session_apply(session, &request, &reply, NULL, 0);
    int written = printf("{\"jsonrpc\":\"2.0\",\"id\":\"%" PRIu64 "\",", request.sequence);
    if (status == FG_STATUS_OK)
        written |= printf("\"result\":{");
    else
        written |= printf("\"error\":{\"code\":-32000,\"message\":\"%s\",\"data\":{",
                          fg_status_string(status));
    written |= printf("\"session_id\":\"%" PRIu64 "\",\"last_sequence\":\"%" PRIu64
                      "\",\"generation\":\"%" PRIu64 "\",\"program_id\":\"%" PRIu64
                      "\",\"program_count\":%zu,\"closed\":%s",
                      reply.session_id, reply.last_sequence, reply.generation, reply.program_id,
                      reply.program_count, reply.closed ? "true" : "false");
    if (status == FG_STATUS_OK && request.method == FG_SESSION_LIST && !programs(session))
        written = -1;
    written |= printf("}}%s\n", status == FG_STATUS_OK ? "" : "}");
    json_object_put(root);
    return written >= 0 && !fflush(stdout);
}
int main(int argc, char **argv) {
    uint64_t identity;
    if (argc != 3 || strcmp(argv[1], "--session-id") || !decimal(argv[2], &identity) || !identity) {
        fputs("usage: fg-agent --session-id NONZERO_U64\n", stderr);
        return 2;
    }
    FgSession *session = NULL;
    if (fg_session_create(identity, FG_SESSION_MAX_PROGRAMS, &session) != FG_STATUS_OK)
        return 1;
    char *line = malloc(MAX_LINE + 1u);
    if (!line) {
        fg_session_destroy(session);
        return 1;
    }
    size_t length = 0;
    int overflow = 0, failed = 0, c;
    while ((c = getchar()) != EOF) {
        if (c == '\n') {
            line[length] = 0;
            if (!(overflow ? invalid() : respond(session, line, length))) {
                failed = 1;
                break;
            }
            length = 0;
            overflow = 0;
        } else if (length < MAX_LINE)
            line[length++] = (char)c;
        else
            overflow = 1;
    }
    if (length || overflow || ferror(stdin))
        failed = 1; /* Never execute an unterminated frame on disconnect. */
    free(line);
    fg_session_destroy(session);
    return failed;
}
