#include <linux/limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <zlib.h>
#include <openssl/sha.h>
#include <dirent.h>

/* ---- zlib helpers ---- */

static char *zlib_decompress(const char *in, size_t in_len, size_t *out_len) {
    z_stream strm = {0};
    inflateInit(&strm);
    strm.avail_in = in_len;
    strm.next_in = (unsigned char *)in;
    size_t cap = in_len * 4;
    char *out = malloc(cap);
    strm.avail_out = cap;
    strm.next_out = (unsigned char *)out;
    inflate(&strm, Z_FINISH);
    *out_len = strm.total_out;
    inflateEnd(&strm);
    return out;
}

static char *zlib_compress(const char *in, size_t in_len, size_t *out_len) {
    z_stream strm = {0};
    deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    strm.avail_in = in_len;
    strm.next_in = (unsigned char *)in;
    size_t cap = in_len * 4;
    char *out = malloc(cap);
    strm.avail_out = cap;
    strm.next_out = (unsigned char *)out;
    deflate(&strm, Z_FINISH);
    *out_len = strm.total_out;
    deflateEnd(&strm);
    return out;
}

/* ---- SHA-1 / hex helpers ---- */

static void hash_to_hex(const unsigned char hash[20], char hex[41]) {
    for (int i = 0; i < 20; i++)
        sprintf(hex + i * 2, "%02x", hash[i]);
}

static void hex_to_hash(const char *hex, unsigned char hash[20]) {
    for (int i = 0; i < 20; i++)
        sscanf(hex + i * 2, "%02hhx", &hash[i]);
}

/* ---- git object helpers ---- */

// Build git object: "<type> <size>\0<content>"
// Returns malloc'd buffer, caller must free.
static char *build_object(const char *type, const char *content, size_t content_len, size_t *obj_len) {
    char header[64];
    int header_len = sprintf(header, "%s %zu", type, content_len) + 1; // +1 for \0
    *obj_len = header_len + content_len;
    char *obj = malloc(*obj_len);
    memcpy(obj, header, header_len);
    memcpy(obj + header_len, content, content_len);
    return obj;
}

// Write object to .git/objects, return hex hash (caller must free).
static char *write_object(const char *obj, size_t obj_len) {
    unsigned char hash[20];
    SHA1((unsigned char *)obj, obj_len, hash);

    char *hex = calloc(1, 41);
    hash_to_hex(hash, hex);

    size_t comp_len = 0;
    char *comp = zlib_compress(obj, obj_len, &comp_len);

    char dir[64];
    sprintf(dir, ".git/objects/%.2s", hex);
    mkdir(dir, 0755);

    char path[PATH_MAX];
    sprintf(path, "%s/%s", dir, hex + 2);
    FILE *fp = fopen(path, "wb");
    fwrite(comp, 1, comp_len, fp);
    fclose(fp);
    free(comp);

    return hex;
}

// Hash a git object (build + write), return hex hash.
static char *hash_and_write(const char *type, const char *content, size_t content_len) {
    size_t obj_len;
    char *obj = build_object(type, content, content_len, &obj_len);
    char *hex = write_object(obj, obj_len);
    free(obj);
    return hex;
}

/* ---- file I/O ---- */

static char *file_read(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    fseek(fp, 0, SEEK_END);
    size_t len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc(len);
    *out_len = fread(buf, 1, len, fp);
    fclose(fp);
    return buf;
}

// Read and decompress a git object from .git/objects.
// Sets *type to "blob" or "tree", returns decompressed content (caller frees).
static char *read_object(const char *hex, char **type, size_t *content_len) {
    char path[PATH_MAX];
    sprintf(path, ".git/objects/%.2s/%s", hex, hex + 2);

    size_t raw_len;
    char *raw = file_read(path, &raw_len);

    size_t dec_len;
    char *dec = zlib_decompress(raw, raw_len, &dec_len);
    free(raw);

    // Parse header: "<type> <size>\0"
    *content_len = 0;
    char *space = strchr(dec, ' ');
    *type = dec;
    *space = '\0';
    int type_len = space - dec;
    char *size_start = space + 1;
    *content_len = (size_t)atol(size_start);
    char *null = strchr(size_start, '\0');
    char *content = null + 1;

    // Shift content to start of buffer so caller can free easily
    memmove(dec, content, *content_len);

    return dec;
}

/* ---- blob ---- */

static char *hash_blob_file(const char *file_path) {
    size_t size;
    char *buf = file_read(file_path, &size);
    char *hex = hash_and_write("blob", buf, size);
    free(buf);
    return hex;
}

/* ---- tree ---- */

typedef struct {
    char name[256];
    unsigned char type;
} DirEntry;

static int compare_entries(const void *a, const void *b) {
    return strcmp(((DirEntry *)a)->name, ((DirEntry *)b)->name);
}

static char *hash_tree(const char *dir_path);

static void build_tree_entry(const char *mode, int mode_len,
                             const char *name, const unsigned char hash[20],
                             char **out_entry, int *out_size) {
    int name_len = strlen(name);
    *out_size = mode_len + name_len + 1 + 20;
    char *entry = malloc(*out_size);
    char *p = entry;
    memcpy(p, mode, mode_len);     p += mode_len;
    memcpy(p, name, name_len + 1); p += name_len + 1;
    memcpy(p, hash, 20);
    *out_entry = entry;
}

static char *hash_tree(const char *dir_path) {
    DIR *dir = opendir(dir_path);

    DirEntry entries[1024];
    int num = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0 ||
            strcmp(de->d_name, ".git") == 0)
            continue;
        strcpy(entries[num].name, de->d_name);
        entries[num].type = de->d_type;
        num++;
    }
    closedir(dir);

    qsort(entries, num, sizeof(DirEntry), compare_entries);

    // Build tree content: concatenation of all entries
    size_t total = 0;
    char *content = NULL;

    for (int i = 0; i < num; i++) {
        char path[PATH_MAX];
        sprintf(path, "%s/%s", dir_path, entries[i].name);

        unsigned char hash[20];
        const char *mode;
        int mode_len;

        if (entries[i].type == DT_DIR) {
            char *hex = hash_tree(path);
            hex_to_hash(hex, hash);
            free(hex);
            mode = "40000 ";
            mode_len = 6;
        } else {
            char *hex = hash_blob_file(path);
            hex_to_hash(hex, hash);
            free(hex);
            mode = "100644 ";
            mode_len = 7;
        }

        char *entry;
        int entry_size;
        build_tree_entry(mode, mode_len, entries[i].name, hash, &entry, &entry_size);

        content = realloc(content, total + entry_size);
        memcpy(content + total, entry, entry_size);
        total += entry_size;
        free(entry);
    }

    char *hex = hash_and_write("tree", content, total);
    free(content);
    return hex;
}

/* ---- commands ---- */

static void cmd_init(void) {
    fprintf(stderr, "Logs from your program will appear here!\n");

    if (mkdir(".git", 0755) == -1 ||
        mkdir(".git/objects", 0755) == -1 ||
        mkdir(".git/refs", 0755) == -1) {
        fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
        exit(1);
    }

    FILE *fp = fopen(".git/HEAD", "w");
    if (!fp) {
        fprintf(stderr, "Failed to create .git/HEAD: %s\n", strerror(errno));
        exit(1);
    }
    fprintf(fp, "ref: refs/heads/main\n");
    fclose(fp);

    printf("Initialized git directory\n");
}

static char *object_path(const char *hex) {
    char path[PATH_MAX];
    sprintf(path, ".git/objects/%.2s/%s", hex, hex + 2);
    return strdup(path);
}

static void cmd_cat_file(const char *hex) {
    char *path = object_path(hex);
    size_t raw_len;
    char *raw = file_read(path, &raw_len);
    free(path);

    size_t dec_len;
    char *dec = zlib_decompress(raw, raw_len, &dec_len);
    free(raw);

    // Skip header: "<type> <size>\0"
    char *null = memchr(dec, '\0', dec_len);
    char *content = null + 1;
    size_t content_len = dec_len - (content - dec);

    fwrite(content, 1, content_len, stdout);
    free(dec);
}

static void cmd_hash_object(const char *file_path) {
    char *hex = hash_blob_file(file_path);
    printf("%s", hex);
    free(hex);
}

static void cmd_ls_tree(const char *hex) {
    char *path = object_path(hex);
    size_t raw_len;
    char *raw = file_read(path, &raw_len);
    free(path);

    size_t dec_len;
    char *dec = zlib_decompress(raw, raw_len, &dec_len);
    free(raw);

    // Skip header
    char *null = memchr(dec, '\0', dec_len);
    char *entry = null + 1;
    char *end = dec + dec_len;

    while (entry < end) {
        char *name = strchr(entry, ' ') + 1;
        char *name_end = strchr(name, '\0');
        printf("%.*s\n", (int)(name_end - name), name);
        entry = name_end + 21;
    }
    free(dec);
}

static void cmd_write_tree(void) {
    char *hex = hash_tree(".");
    printf("%s", hex);
    free(hex);
}

/* ---- main ---- */

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 2) {
        fprintf(stderr, "Usage: ./your_program.sh <command> [<args>]\n");
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "init") == 0) {
        cmd_init();
    } else if (strcmp(cmd, "cat-file") == 0 && argc >= 4 && strcmp(argv[2], "-p") == 0) {
        cmd_cat_file(argv[3]);
    } else if (strcmp(cmd, "hash-object") == 0 && argc >= 4 && strcmp(argv[2], "-w") == 0) {
        cmd_hash_object(argv[3]);
    } else if (strcmp(cmd, "ls-tree") == 0 && argc >= 4 && strcmp(argv[2], "--name-only") == 0) {
        cmd_ls_tree(argv[3]);
    } else if (strcmp(cmd, "write-tree") == 0) {
        cmd_write_tree();
    } else {
        fprintf(stderr, "Unknown command %s\n", cmd);
        return 1;
    }

    return 0;
}
