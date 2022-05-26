#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "rbtree/rbtree.h"


/*
 * Compile with:
 * gcc -Wall daidai.c rbtree/rbtree.c `pkg-config fuse3 --cflags --libs` -o daidai
 */

#define debug


/*
 * Data structure: rbtree
 * reference: https://www.kernel.org/doc/Documentation/rbtree.txt
 */
struct daidai_node {
    int type;
    char *path;
    void *content;
    size_t capacity;
    struct rb_node rb_node;
};

#define Directory 0
#define File 1

static struct rb_root rb_root = RB_ROOT;

static int free_node(struct daidai_node *data) {
    if (data->path != NULL)
        free(data->path);
    if (data->content != NULL)
        free(data->content);
    free(data);

    return 0;
}

static struct daidai_node *create_node(int type, const char *path) {
    struct daidai_node *node = malloc(sizeof(struct daidai_node));
    memset(node, 0, sizeof(struct daidai_node));

    node->type = type;
    node->capacity = 1;
    if (path != NULL)
        node->path = strdup(path);
    if (type == Directory)
        node->content = NULL;
    else
        node->content = strdup("");

    return node;
}

static struct daidai_node *find_node(struct rb_root *root, const char *path) {
    struct rb_node *node = root->rb_node;

    while (node) {
        struct daidai_node *data = container_of(node, struct daidai_node, rb_node);
        int result;

        result = strcmp(path, data->path);

        if (result < 0)
            node = node->rb_left;
        else if (result > 0)
            node = node->rb_right;
        else
            return data;
    }
    return NULL;
}

static int insert_node(struct rb_root *root, struct daidai_node *data) {
    struct rb_node **new = &(root->rb_node), *parent = NULL;

    /* Figure out where to put new node */
    while (*new) {
        struct daidai_node *this = container_of(*new, struct daidai_node, rb_node);
        int result = strcmp(data->path, this->path);

        parent = *new;
        if (result < 0)
            new = &((*new)->rb_left);
        else if (result > 0)
            new = &((*new)->rb_right);
        else
            return -1;
    }

    /* Add new node and rebalance tree. */
    rb_link_node(&data->rb_node, parent, new);
    rb_insert_color(&data->rb_node, root);

    return 0;
}

static int erase_node(struct rb_root *root, const char *path) {
    struct daidai_node *data = find_node(root, path);
    if (data == NULL)
        return -ENOENT;

    rb_erase(&data->rb_node, root);
    free_node(data);

    return 0;
}


/* 
 * debug log file 
 */
#ifdef debug

static char *debugLog;

static void printFunctionLog(const char *function, const char *path) {
    strcat(debugLog, function);
    strcat(debugLog, "\t");
    strcat(debugLog, path);
    strcat(debugLog, "\n");
}

static void printMsg(const char *function, const char *path, const char *msg) {
    strcat(debugLog, function);
    strcat(debugLog, "\t");
    strcat(debugLog, path);
    strcat(debugLog, "\t");
    strcat(debugLog, msg);
    strcat(debugLog, "\n");
}

#endif


/*
 * Command line options
 */
static struct options {
    int show_help;
} options;

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
        OPTION("-h", show_help),
        OPTION("--help", show_help),
        FUSE_OPT_END
};

static char *reverse_path(const char *path) {
    char *res = malloc(strlen(path) + 1);

    const char *pos = NULL;
    int cnt = 0;
    for (const char *cur = path; *cur != '\0'; cur++) {
        if (*cur != '/')
            continue;
        if (cnt == 0)
            cnt++;
        else {
            pos = cur;
            break;
        }
    }

    int idx = 0;
    for (int cur = pos - path; path[cur] != '\0'; cur++, idx++)
        res[idx] = path[cur];
    res[idx++] = '/';
    for (int cur = 1; path[cur] != '/'; cur++, idx++)
        res[idx] = path[cur];
    res[idx] = '\0';

    return res;
}

static void *daidai_init(struct fuse_conn_info *conn,
                         struct fuse_config *cfg) {
#ifdef debug
    printFunctionLog("init", "");
#endif

    (void) conn;
    cfg->kernel_cache = 0;
    return NULL;
}

static int daidai_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
#ifdef debug
    printFunctionLog("getattr", path);
#endif

    (void) fi;
    struct daidai_node *res = NULL;

    memset(stbuf, 0, sizeof(struct stat));
    res = find_node(&rb_root, path);
    if (res == NULL)
        return -ENOENT;

    if (res->type == Directory) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (res->type == File) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(res->content);
    } else
        return -EPERM;

    return 0;
}

static int daidai_open(const char *path, struct fuse_file_info *fi) {
#ifdef debug
    printFunctionLog("open", path);
#endif

    (void) fi;

    struct daidai_node *res = find_node(&rb_root, path);
    if (res == NULL)
        return -ENOENT;

    return 0;
}

static int daidai_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi) {
#ifdef debug
    printFunctionLog("read", path);
#endif

    size_t len;
    (void) fi;

    struct daidai_node *res = find_node(&rb_root, path);
    if (res == NULL)
        return -ENOENT;

    len = strlen(res->content);
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, res->content + offset, size);
    } else
        size = 0;

    return size;
}

static int write_file(const char *path, const char *buf, size_t size, off_t offset) {
    struct daidai_node *res = find_node(&rb_root, path);
    if (res == NULL)
        return -ENOENT;

    while (offset + size > res->capacity) {
        char *tmp = res->content;
        size_t len = strlen(tmp);
        res->capacity *= 2;
        char *new_space = malloc(res->capacity);
        memcpy(new_space, tmp, len);
        free(tmp);
        res->content = new_space;
    }

    memcpy(res->content + offset, buf, size);

    return 0;
}

static int daidai_write(const char *path, const char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
#ifdef debug
    printFunctionLog("write", path);
#endif

    (void) fi;
    write_file(path, buf, size, offset);

    //reverse
    const char *rev_path = reverse_path(path);
#ifdef debug
    printFunctionLog("write: reverse path", rev_path);
#endif

    write_file(rev_path, buf, size, offset);

    return size;
}

static int daidai_mkdir(const char *path, mode_t mode) {
#ifdef debug
    printFunctionLog("mkdir", path);
#endif

    (void) mode;

    struct daidai_node *node = create_node(Directory, path);
    int res = insert_node(&rb_root, node);
    if (res != 0) {
        free_node(node);
        return -EEXIST;
    }

    return 0;
}

static int daidai_rmdir(const char *path) {
#ifdef debug
    printFunctionLog("rmdir", path);
#endif

    int res = erase_node(&rb_root, path);
    if (res != 0)
        return -ENOENT;

    return 0;
}

static int daidai_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                          struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
#ifdef debug
    printFunctionLog("readdir", path);
#endif

    (void) offset;
    (void) fi;
    (void) flags;

    struct daidai_node *res = find_node(&rb_root, path);
    if (res == NULL || res->type != Directory)
        return -ENOENT;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    for (struct rb_node *node = rb_next(&res->rb_node); node != NULL; node = rb_next(node)) {
        struct daidai_node *entry = rb_entry(node, struct daidai_node, rb_node);

        const char *sub_path = entry->path;
        if (path[0] == '/' && strlen(path) == 1 && sub_path[0] == '/') {
            if (strchr(sub_path + 1, '/') != NULL)
                continue;
            filler(buf, sub_path + 1, NULL, 0, 0);
        } else {
            while (*path != '\0' && *sub_path != '\0' && *path == *sub_path) {
                path++;
                sub_path++;
            }
            if (*path == '\0' && *sub_path == '/') {
                if (strchr(sub_path + 1, '/') != NULL)
                    continue;
                filler(buf, sub_path + 1, NULL, 0, 0);
            } else
                break;
        }

#ifdef debug
        printMsg("readdir", path, sub_path);
#endif
    }

    return 0;
}

static int daidai_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
#ifdef debug
    printFunctionLog("create", path);
#endif

    (void) mode;
    (void) fi;

    //reverse
    char *rev_path = reverse_path(path);
#ifdef debug
    printFunctionLog("create: reverse path", rev_path);
#endif

    struct daidai_node *node = create_node(File, path);
    struct daidai_node *rev_node = create_node(File, rev_path);
    insert_node(&rb_root, node);
    insert_node(&rb_root, rev_node);

    return 0;
}

static int daidai_mknod(const char *path, mode_t mode, dev_t dev) {
#ifdef debug
    printFunctionLog("mknod", path);
#endif

    (void) mode;
    (void) dev;

    struct daidai_node *node = create_node(File, path);
    int res = insert_node(&rb_root, node);
    if (res != 0) {
        free_node(node);
        return -EEXIST;
    }

    return 0;
}

static int daidai_unlink(const char *path) {
#ifdef debug
    printFunctionLog("unlink", path);
#endif

    int res = erase_node(&rb_root, path);
    if (res != 0)
        return -ENOENT;

    return 0;
}

static int daidai_release(const char *path, struct fuse_file_info *fi) {
#ifdef debug
    printFunctionLog("release", path);
#endif

    (void) fi;

    return 0;
}

static int daidai_utimens(const char *path, const struct timespec tv[2],
                          struct fuse_file_info *fi) {
#ifdef debug
    printFunctionLog("utimens", path);
#endif

    (void) path;
    (void) tv[2];
    (void) fi;

    return 0;
}

static const struct fuse_operations daidai_oper = {
        .init       = daidai_init,
        .getattr    = daidai_getattr,
        .open       = daidai_open,
        .read       = daidai_read,
        .write      = daidai_write,
        .mkdir      = daidai_mkdir,
        .rmdir      = daidai_rmdir,
        .readdir    = daidai_readdir,
        .create     = daidai_create,
        .mknod      = daidai_mknod,
        .unlink     = daidai_unlink,
        .release    = daidai_release,
        .utimens    = daidai_utimens,
};


static void show_help(const char *progname) {
    printf("usage: %s [options] <mountpoint>\n\n", progname);
    printf("File-system specific options:\n"
           "no options at present"
           "\n");
}


int main(int argc, char *argv[]) {
    int ret;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    /* initialize */
    struct daidai_node *root_node = create_node(Directory, "/");
    insert_node(&rb_root, root_node);

#ifdef debug
    struct daidai_node *log_node = create_node(File, "/log_file");
    debugLog = malloc(100000);
    log_node->content = debugLog;
    insert_node(&rb_root, log_node);
#endif

    /* Parse options */
    if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
        return 1;

    /* When --help is specified, first print our own file-system
       specific help text, then signal fuse_main to show
       additional help (by adding `--help` to the options again)
       without usage: line (by setting argv[0] to the empty
       string) */
    if (options.show_help) {
        show_help(argv[0]);
        assert(fuse_opt_add_arg(&args, "--help") == 0);
        args.argv[0][0] = '\0';
    }

    ret = fuse_main(args.argc, args.argv, &daidai_oper, NULL);
    fuse_opt_free_args(&args);
    return ret;
}