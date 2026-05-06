#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct block {
    char *data;
    size_t len;
    size_t cap;
    int matched;
    int active;
};

static void reset_block(struct block *blk) {
    blk->len = 0;
    blk->matched = 0;
    blk->active = 0;
    if (blk->data != NULL) {
        blk->data[0] = '\0';
    }
}

static int append_line(struct block *blk, const char *line) {
    size_t line_len = strlen(line);
    size_t needed = blk->len + line_len + 1;

    if (needed > blk->cap) {
        size_t new_cap = blk->cap == 0 ? 4096 : blk->cap;
        while (new_cap < needed) {
            new_cap *= 2;
        }

        char *new_data = realloc(blk->data, new_cap);
        if (new_data == NULL) {
            return -1;
        }

        blk->data = new_data;
        blk->cap = new_cap;
    }

    memcpy(blk->data + blk->len, line, line_len + 1);
    blk->len += line_len;
    return 0;
}

static int contains_case_insensitive(const char *haystack, const char *needle) {
    size_t needle_len;

    if (*needle == '\0') {
        return 1;
    }

    needle_len = strlen(needle);
    for (; *haystack != '\0'; haystack++) {
        size_t i;

        for (i = 0; i < needle_len; i++) {
            unsigned char hc = (unsigned char) haystack[i];
            unsigned char nc = (unsigned char) needle[i];

            if (hc == '\0' || tolower(hc) != tolower(nc)) {
                break;
            }
        }

        if (i == needle_len) {
            return 1;
        }
    }

    return 0;
}

static int line_matches(const char *line, const char *needle, int ignore_case) {
    if (ignore_case) {
        return contains_case_insensitive(line, needle);
    }

    return strstr(line, needle) != NULL;
}

static int is_bss_header(const char *line) {
    return strncmp(line, "BSS ", 4) == 0;
}

static int flush_block(struct block *blk, int *printed_any) {
    if (!blk->active || !blk->matched) {
        return 0;
    }

    if (*printed_any) {
        if (putchar('\n') == EOF) {
            return -1;
        }
    }

    if (fwrite(blk->data, 1, blk->len, stdout) != blk->len) {
        return -1;
    }

    *printed_any = 1;
    return 0;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-i] <pattern>\n", prog);
}

int main(int argc, char *argv[]) {
    const char *pattern = NULL;
    int ignore_case = 0;
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    struct block blk = {0};
    int printed_any = 0;
    int exit_code = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            ignore_case = 1;
            continue;
        }

        if (pattern != NULL) {
            print_usage(argv[0]);
            return 1;
        }

        pattern = argv[i];
    }

    if (pattern == NULL) {
        print_usage(argv[0]);
        return 1;
    }

    while ((line_len = getline(&line, &line_cap, stdin)) != -1) {
        if (is_bss_header(line)) {
            if (flush_block(&blk, &printed_any) != 0) {
                perror("stdout");
                exit_code = 1;
                goto cleanup;
            }
            reset_block(&blk);
            blk.active = 1;
        }

        if (!blk.active) {
            continue;
        }

        if (append_line(&blk, line) != 0) {
            fprintf(stderr, "Failed to allocate memory for scan block\n");
            exit_code = 1;
            goto cleanup;
        }

        if (!blk.matched && line_matches(line, pattern, ignore_case)) {
            blk.matched = 1;
        }
    }

    if (ferror(stdin)) {
        perror("stdin");
        exit_code = 1;
        goto cleanup;
    }

    if (flush_block(&blk, &printed_any) != 0) {
        perror("stdout");
        exit_code = 1;
        goto cleanup;
    }

    if (!printed_any) {
        exit_code = 1;
    }

cleanup:
    free(blk.data);
    free(line);
    return exit_code;
}
