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

char *decompress_zlib(char *in, size_t in_len, size_t *out_len) {
    z_stream strm = {0};
    inflateInit(&strm);
    strm.avail_in = in_len;
    strm.next_in = in;
    size_t cap = in_len * 4;
    char *out = malloc(cap);
    strm.avail_out = cap;
    strm.next_out = out;
    inflate(&strm, Z_FINISH);
    *out_len = strm.total_out;
    inflateEnd(&strm);     
    return out;
} 

char *compress_zlib(char *in, size_t in_len, size_t *out_len) {
    z_stream strm = {0};
    deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    strm.avail_in = in_len;
    strm.next_in = in;
    size_t cap = in_len * 4;
    char *out = malloc(cap);
    strm.avail_out = cap;
    strm.next_out = out;
    deflate(&strm, Z_FINISH);
    *out_len = strm.total_out;
    deflateEnd(&strm);     
    return out;
}

static void parse_object(char *object, size_t len) {
    size_t decom_len = 0;
    object = decompress_zlib(object, len, &decom_len);
    if (memcmp(object, "blob", strlen("blob")) == 0) {
        int content_len = atoi(object + 5);
        int header_len = strlen(object);
        char *content = object + header_len + 1;
        printf("%.*s", content_len, content);
    } else if (memcmp(object, "tree", strlen("tree")) == 0) {
        int content_len = atoi(object + 5);
        int header_len = strlen(object);
        char *entry = object + header_len + 1;
        char *content_end = entry + content_len;
        while (entry < content_end) {
            char *name = strchr(entry, ' ') + 1;
            printf("%s\n", name);
            entry = strchr(entry, '\0') + 21;
        }
    }

    free(object);
}

static char *file_read(char *file_name, size_t *out_len) {
    FILE *fp = fopen(file_name, "rb");
    fseek(fp, 0, SEEK_END);
    size_t len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc(len);
    size_t size = fread(buf, 1, len, fp);
    *out_len = size;
    return buf;
}

static void file_write(char *file_name, char *buf, size_t size) {
    FILE *fp = fopen(file_name, "w");
    fwrite(buf, 1, size, fp);
    fclose(fp);
}

int main(int argc, char *argv[]) {
    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 2) {
        fprintf(stderr, "Usage: ./your_program.sh <command> [<args>]\n");
        return 1;
    }
    
    const char *command = argv[1];
    
    if (strcmp(command, "init") == 0) {
        // You can use print statements as follows for debugging, they'll be visible when running tests.
        fprintf(stderr, "Logs from your program will appear here!\n");

        // TODO: Uncomment the code below to pass the first stage
        
        if (mkdir(".git", 0755) == -1 || 
            mkdir(".git/objects", 0755) == -1 || 
            mkdir(".git/refs", 0755) == -1) {
            fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
            return 1;
        }
        
        FILE *headFile = fopen(".git/HEAD", "w");
        if (headFile == NULL) {
            fprintf(stderr, "Failed to create .git/HEAD file: %s\n", strerror(errno));
            return 1;
        }
        fprintf(headFile, "ref: refs/heads/main\n");
        fclose(headFile);
        
        printf("Initialized git directory\n");
    } else if (strcmp(command, "cat-file") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: ./your_program.sh <command> [<args>]\n");
            return 1;
        }
        const char *flag = argv[2];
        if (strcmp(flag, "-p") == 0) {
            const char *hash = argv[3];
            if (strlen(hash) != 40) {
                fprintf(stderr, "invalid hash len\n");
                return 1;
            }
            char path[PATH_MAX] = {0};
            sprintf(path, ".git/objects/%.*s/%s", 2, hash, hash + 2);
            size_t size = 0;
            char *buf = file_read(path, &size);
            parse_object(buf, size);
            free(buf);
        }
    } else if (strcmp(command, "hash-object") == 0) {
        const char *flag = argv[2];
        if (strcmp(flag, "-w") == 0) {
            char *file_name = argv[3];
            size_t size = 0;
            char *buf = file_read(file_name, &size);
            char header[256];
            sprintf(header, "blob %lu", size);
            int header_size = strlen(header) + 1;
            char *buf_with_header = malloc(size + header_size);
            memcpy(buf_with_header, header, header_size);
            memcpy(buf_with_header + header_size, buf, size);
            free(buf);
            unsigned char hash[20];
            SHA1(buf_with_header, size + header_size, hash);
            char hex[41] = {0};
            for (int i = 0; i < 20; i++) {
                sprintf(hex + i * 2, "%02x", hash[i]);
            }
            printf("%s", hex);
            size_t com_len = 0;
            char *comp_content = compress_zlib(buf_with_header, size + header_size, &com_len);
            free(buf_with_header);
            char dir[64] = {0};
            sprintf(dir, ".git/objects/%.*s", 2, hex);
            mkdir(dir, 0755);
            char blob_name[256];
            sprintf(blob_name, "%s/%s", dir, hex + 2);
            file_write(blob_name, comp_content, com_len);
        }
    } else if (strcmp(command, "ls-tree") == 0) {
        const char *flag = argv[2];
        const char *hash = argv[3];
        if (strlen(hash) != 40) {
            fprintf(stderr, "invalid hash len\n");
            return 1;
        }
        char path[PATH_MAX] = {0};
        sprintf(path, ".git/objects/%.*s/%s", 2, hash, hash + 2);
        size_t size = 0;
        char *buf = file_read(path, &size);
        parse_object(buf, size);
        free(buf);       
    } else {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }
    
    return 0;
}
