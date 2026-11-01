#define _POSIX_C_SOURCE 200809L
#include "fingolfin/package.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <json-c/json.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
#define WIRE_LIMIT (64u * 1024u * 1024u)
static int number(const char *s, uint64_t *out) {
    if (!s || !*s || (*s == '0' && s[1]))
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
static int add(struct json_object *o, const char *key, struct json_object *v) {
    if (!v)
        return 0;
    if (json_object_object_add(o, key, v)) {
        json_object_put(v);
        return 0;
    }
    return 1;
}
static int text(struct json_object *o, const char *key, const char *s) {
    return add(o, key, json_object_new_string(s));
}
static int integer(struct json_object *o, const char *key, uint64_t n) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%" PRIu64, n);
    return text(o, key, buf);
}
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
static struct json_object *request(const char *command, uint64_t session, uint64_t seq,
                                   uint64_t gen) {
    struct json_object *root = json_object_new_object(), *p = json_object_new_object();
    if (!root || !p)
        goto fail;
    const char *method = NULL;
    if (!strcmp(command, "status"))
        method = "session.status";
    else if (!strcmp(command, "list"))
        method = "program.list";
    else if (!strcmp(command, "close"))
        method = "session.close";
    else if (!strncmp(command, "unload ", 7)) {
        uint64_t id;
        if (!number(command + 7, &id) || !id || !integer(p, "program_id", id))
            goto fail;
        method = "program.unload";
    } else if (!strncmp(command, "load ", 5) && command[5]) {
        FILE *f = fopen(command + 5, "rb");
        if (!f)
            goto fail;
        char *data = malloc(FG_PACKAGE_MAX_BYTES + 1u);
        if (!data) {
            fclose(f);
            goto fail;
        }
        size_t n = fread(data, 1, FG_PACKAGE_MAX_BYTES + 1u, f);
        int bad = ferror(f) || n > FG_PACKAGE_MAX_BYTES;
        if (fclose(f))
            bad = 1;
        FgPackage *package = NULL;
        FgPackageLimits limits = {
            1, sizeof(limits), 256, FG_PACKAGE_MAX_BYTES, FG_PACKAGE_MAX_BYTES, NULL, NULL};
        if (!bad && fg_package_load((FgBytes){(unsigned char *)data, n}, &limits, &package, NULL,
                                    0) != FG_STATUS_OK)
            bad = 1;
        if (!bad)
            bad = !add(p, "package", json_object_new_string_len(data, (int)n)) ||
                  !text(p, "program_hash", fg_package_program_hash(package)) ||
                  !text(p, "model_hash", fg_package_model_hash(package));
        fg_package_destroy(package);
        free(data);
        if (bad)
            goto fail;
        method = "program.load";
    }
    if (!method || !text(root, "jsonrpc", "2.0") || !integer(root, "id", seq) ||
        !text(root, "method", method) || !add(p, "version", json_object_new_int(1)) ||
        !integer(p, "session_id", session) || !integer(p, "generation", gen))
        goto fail;
    if (!add(root, "params", p)) {
        p = NULL;
        goto fail;
    }
    return root;
fail:
    json_object_put(p);
    json_object_put(root);
    return NULL;
}
static int64_t now_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        return -1;
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int ready(int fd, short events, int64_t deadline) {
    for (;;) {
        int64_t now = now_ms();
        if (now < 0 || now >= deadline)
            return 0;
        struct pollfd p = {fd, events, 0};
        int result = poll(&p, 1, (int)(deadline - now));
        if (result < 0 && errno == EINTR)
            continue;
        return result > 0 && (p.revents & events);
    }
}
static int send_all(int fd, const char *s, size_t n, int64_t deadline) {
    while (n) {
        if (!ready(fd, POLLOUT, deadline))
            return 0;
        ssize_t count = write(fd, s, n);
        if (count < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (count <= 0)
            return 0;
        s += count;
        n -= (size_t)count;
    }
    return 1;
}
/* One response per request; do not accept unsolicited/pipelined frames. */
static int receive(int fd, char *buf, size_t *length, int64_t deadline) {
    *length = 0;
    while (*length <= WIRE_LIMIT) {
        if (!ready(fd, POLLIN, deadline))
            return 0;
        ssize_t n = read(fd, buf + *length, WIRE_LIMIT + 1u - *length);
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (n <= 0)
            return 0;
        char *lf = memchr(buf + *length, '\n', (size_t)n);
        *length += (size_t)n;
        if (lf) {
            if (lf != buf + *length - 1 || *length - 1 > WIRE_LIMIT)
                return 0;
            *lf = 0;
            (*length)--;
            return 1;
        }
        if (*length > WIRE_LIMIT)
            return 0;
    }
    return 0;
}
static size_t raw_keys(const char *s, size_t n) {
    size_t count = 0;
    int quoted = 0, escaped = 0;
    for (size_t i = 0; i < n; i++) {
        if (quoted) {
            if (escaped)
                escaped = 0;
            else if (s[i] == '\\')
                escaped = 1;
            else if (s[i] == '"')
                quoted = 0;
        } else if (s[i] == '"')
            quoted = 1;
        else if (s[i] == ':')
            count++;
    }
    return count;
}
static size_t dom_keys(struct json_object *o) {
    size_t count = 0;
    if (json_object_is_type(o, json_type_object)) {
        json_object_object_foreach(o, key, value) {
            (void)key;
            count += 1 + dom_keys(value);
        }
    } else if (json_object_is_type(o, json_type_array)) {
        for (size_t i = 0; i < json_object_array_length(o); i++)
            count += dom_keys(json_object_array_get_idx(o, i));
    }
    return count;
}
static int response(char *buf, size_t n, uint64_t session, uint64_t seq, uint64_t *gen,
                    int *remote_error) {
    struct json_tokener *tok = json_tokener_new_ex(16);
    if (!tok)
        return 0;
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    struct json_object *o = json_tokener_parse_ex(tok, buf, (int)n);
    int ok =
        json_tokener_get_error(tok) == json_tokener_success && json_tokener_get_parse_end(tok) == n;
    json_tokener_free(tok);
    uint64_t id, sid, last, generation, program;
    struct json_object *result = get(o, "result"), *error = get(o, "error");
    struct json_object *state = error ? get(error, "data") : result;
    const char *version = string(get(o, "jsonrpc"));
    ok = ok && json_object_is_type(o, json_type_object) && json_object_object_length(o) == 3 &&
         raw_keys(buf, n) == dom_keys(o) && version && !strcmp(version, "2.0") &&
         number(string(get(o, "id")), &id) && id == seq && (!!error != !!result) &&
         json_object_is_type(state, json_type_object) &&
         number(string(get(state, "session_id")), &sid) && sid == session &&
         number(string(get(state, "last_sequence")), &last) && last <= seq &&
         (error || last == seq) && number(string(get(state, "generation")), &generation) &&
         generation >= *gen && number(string(get(state, "program_id")), &program) &&
         json_object_is_type(get(state, "closed"), json_type_boolean) &&
         json_object_is_type(get(state, "program_count"), json_type_int) &&
         json_object_get_int64(get(state, "program_count")) >= 0 &&
         json_object_get_int64(get(state, "program_count")) <= 64;
    if (error)
        ok = ok && json_object_is_type(get(error, "code"), json_type_int) &&
             string(get(error, "message"));
    if (ok) {
        *gen = generation;
        *remote_error = error != NULL;
    }
    json_object_put(o);
    return ok;
}
int fgctl_session(int argc, char **argv) {
    uint64_t session;
    if (argc < 5 || strcmp(argv[3], "--") || !number(argv[2], &session) || !session) {
        fputs("usage: fgctl session SESSION_ID -- EXECUTABLE [ARGS...]\n", stderr);
        return 2;
    }
    int to[2], from[2];
    /* Keep pipe endpoints distinct from the inherited standard descriptors. */
    if (fcntl(STDIN_FILENO, F_GETFD) < 0 || fcntl(STDOUT_FILENO, F_GETFD) < 0 ||
        fcntl(STDERR_FILENO, F_GETFD) < 0)
        return 1;
    if (pipe(to))
        return 1;
    if (pipe(from)) {
        close(to[0]);
        close(to[1]);
        return 1;
    }
    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc) {
        close(to[0]);
        close(to[1]);
        close(from[0]);
        close(from[1]);
        return 1;
    }
    rc = posix_spawn_file_actions_adddup2(&actions, to[0], STDIN_FILENO);
    rc |= posix_spawn_file_actions_adddup2(&actions, from[1], STDOUT_FILENO);
    rc |= posix_spawn_file_actions_addclose(&actions, to[0]);
    rc |= posix_spawn_file_actions_addclose(&actions, to[1]);
    rc |= posix_spawn_file_actions_addclose(&actions, from[0]);
    rc |= posix_spawn_file_actions_addclose(&actions, from[1]);
    pid_t child = 0;
    if (!rc)
        rc = posix_spawnp(&child, argv[4], &actions, NULL, &argv[4], environ);
    posix_spawn_file_actions_destroy(&actions);
    close(to[0]);
    close(from[1]);
    if (rc) {
        close(to[1]);
        close(from[0]);
        fputs("fgctl: could not start transport\n", stderr);
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    int failed = 0, remote_error = 0;
    char *buf = malloc(WIRE_LIMIT + 1u);
    if (!buf || fcntl(to[1], F_SETFL, O_NONBLOCK) < 0 || fcntl(from[0], F_SETFL, O_NONBLOCK) < 0)
        failed = 1;
    uint64_t generation = 1, sequence = 0;
    char command[4096];
    while (!failed && fgets(command, sizeof(command), stdin)) {
        size_t length = strlen(command);
        if (!length || command[length - 1] != '\n' || sequence == UINT64_MAX) {
            failed = 1;
            break;
        }
        command[--length] = 0;
        if (length && command[length - 1] == '\r')
            command[--length] = 0;
        struct json_object *req = request(command, session, ++sequence, generation);
        if (!req) {
            failed = 1;
            break;
        }
        const char *encoded = json_object_to_json_string_ext(req, JSON_C_TO_STRING_PLAIN);
        int64_t now = now_ms(), deadline = now + 30000;
        int ok = now >= 0 && encoded && strlen(encoded) <= WIRE_LIMIT &&
                 send_all(to[1], encoded, strlen(encoded), deadline) &&
                 send_all(to[1], "\n", 1, deadline);
        json_object_put(req);
        size_t n;
        int error = 0;
        if (!ok || !receive(from[0], buf, &n, deadline) ||
            !response(buf, n, session, sequence, &generation, &error)) {
            failed = 1;
            break;
        }
        remote_error |= error;
        if (puts(buf) < 0 || fflush(stdout)) {
            failed = 1;
            break;
        }
    }
    if (ferror(stdin))
        failed = 1;
    free(buf);
    close(to[1]);
    close(from[0]);
    if (failed)
        kill(child, SIGKILL);
    int status = 0;
    int64_t deadline = now_ms() + 30000;
    for (;;) {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child)
            break;
        if (done < 0 && errno != EINTR) {
            failed = 1;
            break;
        }
        int64_t current = now_ms();
        if (current < 0 || current >= deadline) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            failed = 1;
            break;
        }
        struct timespec pause = {0, 10000000};
        nanosleep(&pause, NULL);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status))
        failed = 1;
    if (failed)
        fputs("fgctl: session command, transport or response failed\n", stderr);
    return failed || remote_error ? 1 : 0;
}
