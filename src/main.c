#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <zlib.h>

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

static void parse_blob(char *blob, size_t len) {
    size_t decom_len = 0;
    blob = decompress_zlib(blob, len, &decom_len);
    int content_len = atoi(blob + 5);
    int header_len = strlen(blob);
    char *content = blob + header_len + 1;
    printf("%.*s", content_len, content);
    free(blob);
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
        const char *hash = argv[3];
        if (strcmp(flag, "-p") != 0) {
            fprintf(stderr, "unsupportted flag\n");
            return 1;
        }
        if (strlen(hash) != 40) {
            fprintf(stderr, "invalid hash len\n");
            return 1;
        }
        char path[PATH_MAX] = {0};
        sprintf(path, ".git/objects/%.*s/%s", 2, hash, hash + 2);
        FILE *fp = fopen(path, "rb");
        if (fp == NULL) {
            fprintf(stderr, "invalid hash len\n");
            return 1;
        }
        fseek(fp, 0, SEEK_END);
        size_t len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char *buf = malloc(len);
        size_t size = fread(buf, 1, len, fp);
        parse_blob(buf, size);
        free(buf);
        fclose(fp);
    } else {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }
    
    return 0;
}
