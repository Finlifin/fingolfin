#include "fingolfin/objects.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
struct Worker {
    FgObjects *r;
    uint64_t launch;
    FgStatus status;
    FgObjectKey key;
};
static void *compete(void *opaque) {
    struct Worker *w = opaque;
    FgObjectIdentity id = {.device = 2, .local = 7, .kind = 3, .core_x = 1, .core_y = 2};
    w->status = fg_objects_register(w->r, w->launch, &id, &w->key);
    return NULL;
}
static void *churn(void *opaque) {
    struct Worker *w = opaque;
    FgObjectIdentity id = {.device = w->launch, .kind = 4};
    for (size_t i = 0; i < 2000; i++) {
        FgObjectKey key;
        FgObjectIdentity copy;
        assert(fg_objects_register(w->r, w->launch, &id, &key) == FG_STATUS_OK);
        assert(fg_objects_resolve(w->r, &key, &copy) == FG_STATUS_OK && copy.device == id.device);
        assert(fg_objects_retire(w->r, &key) == FG_STATUS_OK);
        assert(fg_objects_resolve(w->r, &key, &copy) == FG_STATUS_NOT_FOUND);
    }
    return NULL;
}
static int same_key(FgObjectKey a, FgObjectKey b) {
    return a.run == b.run && a.launch == b.launch && a.slot == b.slot &&
           a.generation == b.generation;
}
static void trees(void) {
    FgObjects *r;
    uint64_t first, second;
    assert(fg_objects_create(20, 2, 8, &r) == FG_STATUS_OK);
    assert(fg_objects_begin(r, &first) == FG_STATUS_OK);
    assert(fg_objects_begin(r, &second) == FG_STATUS_OK);
    FgObjectIdentity id = {.kind = 1};
    FgObjectKey root, child, leaf, other, out = {99, 99, 99, 99}, sentinel = out;
    assert(fg_objects_register(r, first, &id, &root) == FG_STATUS_OK);
    assert(fg_objects_register(r, second, &id, &other) == FG_STATUS_OK);
    assert(fg_objects_parent(r, &root, &out) == FG_STATUS_NOT_FOUND && same_key(out, sentinel));
    id.kind = 2;
    assert(fg_objects_register_child(r, first, &id, NULL, &out) == FG_STATUS_INVALID &&
           same_key(out, sentinel));
    assert(fg_objects_register_child(r, first, &id, &other, &out) == FG_STATUS_INVALID &&
           same_key(out, sentinel));
    FgObjectKey foreign = root;
    foreign.run++;
    assert(fg_objects_register_child(r, first, &id, &foreign, &out) == FG_STATUS_INVALID &&
           same_key(out, sentinel));
    assert(fg_objects_register_child(r, first, &id, &root, &child) == FG_STATUS_OK);
    assert(fg_objects_parent(r, &child, &out) == FG_STATUS_OK && same_key(out, root));
    id.kind = 3;
    assert(fg_objects_register_child(r, first, &id, &child, &leaf) == FG_STATUS_OK);
    assert(fg_objects_parent(r, &leaf, &out) == FG_STATUS_OK && same_key(out, child));
    out = sentinel;
    assert(fg_objects_register_child(r, first, &id, &root, &out) == FG_STATUS_CONFLICT &&
           same_key(out, sentinel));
    assert(fg_objects_retire(r, &root) == FG_STATUS_BUSY);
    assert(fg_objects_retire(r, &child) == FG_STATUS_BUSY);
    assert(fg_objects_retire(r, &leaf) == FG_STATUS_OK);
    assert(fg_objects_retire(r, &child) == FG_STATUS_OK);
    assert(fg_objects_retire(r, &root) == FG_STATUS_OK);
    FgObjectKey replacement;
    assert(fg_objects_register(r, first, &id, &replacement) == FG_STATUS_OK);
    assert(replacement.slot == root.slot && replacement.generation > root.generation);
    id.kind = 4;
    assert(fg_objects_register_child(r, first, &id, &root, &out) == FG_STATUS_NOT_FOUND &&
           same_key(out, sentinel));
    assert(fg_objects_parent(r, &leaf, &out) == FG_STATUS_NOT_FOUND && same_key(out, sentinel));
    assert(fg_objects_register_child(r, first, &id, &replacement, &child) == FG_STATUS_OK);
    id.kind = 5;
    assert(fg_objects_register_child(r, first, &id, &child, &leaf) == FG_STATUS_OK);
    assert(fg_objects_end(r, first) == FG_STATUS_OK);
    assert(fg_objects_parent(r, &leaf, &out) == FG_STATUS_NOT_FOUND && same_key(out, sentinel));
    assert(fg_objects_resolve(r, &replacement, &id) == FG_STATUS_NOT_FOUND);
    assert(fg_objects_resolve(r, &other, &id) == FG_STATUS_OK);
    assert(fg_objects_end(r, second) == FG_STATUS_OK);
    assert(fg_objects_destroy(r) == FG_STATUS_OK);
}
int main(void) {
    trees();
    FgObjects *r = NULL;
    assert(fg_objects_create(0, 1, 1, &r) == FG_STATUS_INVALID && !r);
    assert(fg_objects_create(1, SIZE_MAX, 1, &r) == FG_STATUS_OVERFLOW && !r);
    assert(fg_objects_create(9, 2, 2, &r) == FG_STATUS_OK);
    uint64_t first, second, unchanged = 77;
    assert(fg_objects_begin(r, &first) == FG_STATUS_OK);
    assert(fg_objects_begin(r, &second) == FG_STATUS_OK && first != second);
    assert(fg_objects_begin(r, &unchanged) == FG_STATUS_CAPACITY && unchanged == 77);
    assert(fg_objects_destroy(r) == FG_STATUS_BUSY);
    FgObjectIdentity id = {.device = 1, .kind = 1}, copy = {.device = 99};
    FgObjectKey a, b, key = {7, 7, 7, 7};
    assert(fg_objects_register(r, first, &id, &a) == FG_STATUS_OK);
    assert(fg_objects_register(r, first, &id, &key) == FG_STATUS_CONFLICT && key.run == 7);
    assert(fg_objects_register(r, second, &id, &b) == FG_STATUS_OK);
    id.device = 2;
    assert(fg_objects_register(r, first, &id, &key) == FG_STATUS_CAPACITY && key.run == 7);
    assert(fg_objects_resolve(r, &a, &copy) == FG_STATUS_OK && copy.device == 1);
    FgObjectKey wrong = a;
    wrong.run++;
    copy.device = 99;
    assert(fg_objects_resolve(r, &wrong, &copy) == FG_STATUS_NOT_FOUND && copy.device == 99);
    assert(fg_objects_retire(r, &a) == FG_STATUS_OK);
    assert(fg_objects_register(r, first, &id, &key) == FG_STATUS_OK);
    assert(key.slot == a.slot && key.generation > a.generation);
    assert(fg_objects_retire(r, &a) == FG_STATUS_NOT_FOUND);
    assert(fg_objects_resolve(r, &key, &copy) == FG_STATUS_OK && copy.device == 2);
    assert(fg_objects_end(r, first) == FG_STATUS_OK);
    assert(fg_objects_resolve(r, &key, &copy) == FG_STATUS_NOT_FOUND);
    assert(fg_objects_register(r, first, &id, &key) == FG_STATUS_NOT_FOUND);
    assert(fg_objects_resolve(r, &b, &copy) == FG_STATUS_OK);
    uint64_t third;
    assert(fg_objects_begin(r, &third) == FG_STATUS_OK && third > second);
    assert(fg_objects_register(r, third, &id, &key) == FG_STATUS_OK &&
           key.generation > a.generation);
    assert(fg_objects_end(r, second) == FG_STATUS_OK);
    assert(fg_objects_end(r, third) == FG_STATUS_OK);
    assert(fg_objects_end(r, third) == FG_STATUS_NOT_FOUND);
    assert(fg_objects_destroy(r) == FG_STATUS_OK);

    assert(fg_objects_create(10, 2, 4, &r) == FG_STATUS_OK);
    assert(fg_objects_begin(r, &first) == FG_STATUS_OK);
    struct Worker w[2] = {{.r = r, .launch = first}, {.r = r, .launch = first}};
    pthread_t threads[2];
    for (size_t i = 0; i < 2; i++)
        assert(!pthread_create(&threads[i], NULL, compete, &w[i]));
    for (size_t i = 0; i < 2; i++)
        assert(!pthread_join(threads[i], NULL));
    assert((w[0].status == FG_STATUS_OK && w[1].status == FG_STATUS_CONFLICT) ||
           (w[1].status == FG_STATUS_OK && w[0].status == FG_STATUS_CONFLICT));
    id = (FgObjectIdentity){.device = 2, .local = 7, .kind = 3, .core_x = 1, .core_y = 2};
    assert(fg_objects_find(r, first, &id, &key) == FG_STATUS_OK);
    assert(fg_objects_retire(r, &key) == FG_STATUS_OK);
    assert(fg_objects_find(r, first, &id, &key) == FG_STATUS_NOT_FOUND);
    assert(fg_objects_begin(r, &second) == FG_STATUS_OK);
    w[1].launch = second;
    for (size_t i = 0; i < 2; i++)
        assert(!pthread_create(&threads[i], NULL, churn, &w[i]));
    for (size_t i = 0; i < 2; i++)
        assert(!pthread_join(threads[i], NULL));
    assert(fg_objects_end(r, first) == FG_STATUS_OK);
    assert(fg_objects_end(r, second) == FG_STATUS_OK);
    assert(fg_objects_destroy(r) == FG_STATUS_OK);
    return 0;
}
