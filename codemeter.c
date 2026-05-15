#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define PATH_BUFFER_SIZE 4096
#define MAX_FILTER_EXTS 128
#define MAX_FILTER_EXT_LEN 31
#define MAX_EXCLUDES 128
#define MAX_EXCLUDE_NAME_LEN 127

typedef struct {
    uint64_t loc;
    uint64_t files;
    uint64_t skipped;
} Totals;

typedef struct {
    bool include_comments;
    const char* root;
    bool use_filter;
    size_t filter_count;
    char filters[MAX_FILTER_EXTS][MAX_FILTER_EXT_LEN + 1];
    size_t exclude_count;
    char excludes[MAX_EXCLUDES][MAX_EXCLUDE_NAME_LEN + 1];
} Options;

static int icmp_ascii(const char* a, const char* b) {
    // This keeps extension checks case-insensitive on all platforms.
    while (*a != '\0' && *b != '\0') {
        int da = tolower((unsigned char)*a);
        int db = tolower((unsigned char)*b);
        if (da != db) {
            return da - db;
        }
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int trim_token(const char* src, char* out, size_t out_size) {
    if (src == NULL || out == NULL || out_size == 0) {
        return -1;
    }

    size_t start = 0;
    size_t len = strlen(src);
    while (start < len && isspace((unsigned char)src[start])) {
        ++start;
    }

    size_t end = len;
    while (end > start && isspace((unsigned char)src[end - 1])) {
        --end;
    }

    while (start < end && src[start] == '.') {
        ++start;
    }

    size_t n = end > start ? (end - start) : 0;
    if (n == 0 || n > out_size - 1) {
        return -1;
    }

    for (size_t i = 0; i < n; ++i) {
        out[i] = (char)tolower((unsigned char)src[start + i]);
    }
    out[n] = '\0';

    return 0;
}

static int trim_plain_token(const char* src, char* out, size_t out_size) {
    if (src == NULL || out == NULL || out_size == 0) {
        return -1;
    }

    size_t start = 0;
    size_t len = strlen(src);
    while (start < len && isspace((unsigned char)src[start])) {
        ++start;
    }

    size_t end = len;
    while (end > start && isspace((unsigned char)src[end - 1])) {
        --end;
    }

    size_t n = end > start ? (end - start) : 0;
    if (n == 0 || n > out_size - 1) {
        return -1;
    }

    for (size_t i = 0; i < n; ++i) {
        out[i] = (char)tolower((unsigned char)src[start + i]);
    }
    out[n] = '\0';

    return 0;
}

static int parse_filter_list(Options* options, const char* list) {
    if (options == NULL || list == NULL) {
        return -1;
    }

    options->use_filter = true;
    options->filter_count = 0;

    const char* p = list;
    while (*p != '\0') {
        const char* comma = strchr(p, ',');
        size_t token_len = comma != NULL ? (size_t)(comma - p) : strlen(p);

        char raw[MAX_FILTER_EXT_LEN + 8];
        if (token_len == 0 || token_len >= sizeof(raw)) {
            return -1;
        }
        memcpy(raw, p, token_len);
        raw[token_len] = '\0';

        if (options->filter_count >= MAX_FILTER_EXTS) {
            return -1;
        }

        if (trim_token(raw, options->filters[options->filter_count], sizeof(options->filters[0])) != 0) {
            return -1;
        }

        ++options->filter_count;

        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }

    return options->filter_count > 0 ? 0 : -1;
}

static int parse_exclude_list(Options* options, const char* list) {
    if (options == NULL || list == NULL) {
        return -1;
    }

    options->exclude_count = 0;

    const char* p = list;
    while (*p != '\0') {
        const char* comma = strchr(p, ',');
        size_t token_len = comma != NULL ? (size_t)(comma - p) : strlen(p);

        char raw[MAX_EXCLUDE_NAME_LEN + 8];
        if (token_len == 0 || token_len >= sizeof(raw)) {
            return -1;
        }
        memcpy(raw, p, token_len);
        raw[token_len] = '\0';

        if (options->exclude_count >= MAX_EXCLUDES) {
            return -1;
        }

        if (trim_plain_token(raw, options->excludes[options->exclude_count], sizeof(options->excludes[0])) != 0) {
            return -1;
        }

        ++options->exclude_count;

        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }

    return options->exclude_count > 0 ? 0 : -1;
}

static int basename_from_path(const char* path, char* out, size_t out_size) {
    if (path == NULL || out == NULL || out_size == 0) {
        return -1;
    }

    size_t len = strlen(path);
    while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) {
        --len;
    }
    if (len == 0) {
        return -1;
    }

    size_t start = len;
    while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\') {
        --start;
    }

    size_t n = len - start;
    if (n == 0 || n > out_size - 1) {
        return -1;
    }

    memcpy(out, path + start, n);
    out[n] = '\0';
    return 0;
}

static bool is_excluded_name(const Options* options, const char* name) {
    if (options == NULL || name == NULL || options->exclude_count == 0) {
        return false;
    }

    char normalized[MAX_EXCLUDE_NAME_LEN + 1];
    if (trim_plain_token(name, normalized, sizeof(normalized)) != 0) {
        return false;
    }

    for (size_t i = 0; i < options->exclude_count; ++i) {
        if (icmp_ascii(normalized, options->excludes[i]) == 0) {
            return true;
        }
    }

    return false;
}

static bool has_code_extension(const char* path, const Options* options) {
    // Small allowlist so we skip binaries and big vendor assets.
    static const char* exts[] = {
        ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx",
        ".py", ".js", ".ts", ".tsx", ".jsx", ".java", ".cs", ".go",
        ".rs", ".php", ".rb", ".swift", ".kt", ".kts", ".scala", ".lua",
        ".sh", ".bash", ".zsh", ".fish", ".ps1", ".bat", ".cmd",
        ".sql", ".html", ".css", ".xml", ".yaml", ".yml", ".toml"
    };

    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* base = path;

    if (slash != NULL && slash + 1 > base) {
        base = slash + 1;
    }
    if (backslash != NULL && backslash + 1 > base) {
        base = backslash + 1;
    }

    if (icmp_ascii(base, "Makefile") == 0 ||
        icmp_ascii(base, "Dockerfile") == 0 ||
        icmp_ascii(base, "CMakeLists.txt") == 0) {
        return true;
    }

    const char* dot = strrchr(base, '.');
    if (dot == NULL) {
        return false;
    }

    if (options != NULL && options->use_filter) {
        const char* ext = dot + 1;
        char normalized[MAX_FILTER_EXT_LEN + 1];

        if (trim_token(ext, normalized, sizeof(normalized)) != 0) {
            return false;
        }

        for (size_t i = 0; i < options->filter_count; ++i) {
            if (icmp_ascii(normalized, options->filters[i]) == 0) {
                return true;
            }
        }
        return false;
    }

    size_t count = sizeof(exts) / sizeof(exts[0]);
    for (size_t i = 0; i < count; ++i) {
        if (icmp_ascii(dot, exts[i]) == 0) {
            return true;
        }
    }

    return false;
}

static bool join_path(const char* left, const char* right, char* out, size_t out_size) {
    if (left == NULL || right == NULL || out == NULL || out_size == 0) {
        return false;
    }

    size_t l = strlen(left);
    size_t r = strlen(right);

    // Add a separator only when the base path does not already end with one.
    bool need_sep = l > 0 && left[l - 1] != '/' && left[l - 1] != '\\';
    size_t total = l + (need_sep ? 1u : 0u) + r + 1u;

    if (total > out_size) {
        return false;
    }

    memcpy(out, left, l);
    size_t pos = l;
    if (need_sep) {
#ifdef _WIN32
        out[pos++] = '\\';
#else
        out[pos++] = '/';
#endif
    }
    memcpy(out + pos, right, r);
    out[pos + r] = '\0';

    return true;
}

static uint64_t count_file_loc(const char* path, bool include_comments, bool* ok) {
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        *ok = false;
        return 0;
    }

    bool in_block_comment = false;
    bool in_string = false;
    bool in_char = false;
    bool escaped = false;

    bool line_has_code = false;
    bool line_has_comment = false;

    uint64_t total = 0;
    int ch = 0;
    bool seen_any = false;

    // Single-pass scanner: tracks strings/chars/comments.
    while ((ch = fgetc(fp)) != EOF) {
        seen_any = true;

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (line_has_code || (include_comments && line_has_comment)) {
                ++total;
            }
            line_has_code = false;
            line_has_comment = false;
            continue;
        }

        if (in_block_comment) {
            line_has_comment = true;
            if (ch == '*') {
                int next = fgetc(fp);
                if (next == '/') {
                    in_block_comment = false;
                    line_has_comment = true;
                } else if (next != EOF) {
                    ungetc(next, fp);
                }
            }
            continue;
        }

        if (in_string) {
            line_has_code = true;
            if (!escaped && ch == '"') {
                in_string = false;
            }
            escaped = (!escaped && ch == '\\');
            continue;
        }

        if (in_char) {
            line_has_code = true;
            if (!escaped && ch == '\'') {
                in_char = false;
            }
            escaped = (!escaped && ch == '\\');
            continue;
        }

        if (ch == '/') {
            int next = fgetc(fp);

            if (next == '/') {
                line_has_comment = true;
                // Eat the rest of the line for // comments.
                while ((ch = fgetc(fp)) != EOF && ch != '\n') {
                }

                if (line_has_code || (include_comments && line_has_comment)) {
                    ++total;
                }
                line_has_code = false;
                line_has_comment = false;

                if (ch == EOF) {
                    seen_any = false;
                    break;
                }

                continue;
            }

            if (next == '*') {
                in_block_comment = true;
                line_has_comment = true;
                continue;
            }

            line_has_code = true;
            if (next != EOF) {
                ungetc(next, fp);
            }
            escaped = false;
            continue;
        }

        if (ch == '"') {
            in_string = true;
            escaped = false;
            line_has_code = true;
            continue;
        }

        if (ch == '\'') {
            in_char = true;
            escaped = false;
            line_has_code = true;
            continue;
        }

        if (!isspace((unsigned char)ch)) {
            line_has_code = true;
        }
    }

    if (seen_any && (line_has_code || (include_comments && line_has_comment))) {
        ++total;
    }

    fclose(fp);
    *ok = true;
    return total;
}

static void walk_tree(const char* path, const Options* options, Totals* totals);

#ifdef _WIN32
static void walk_tree_windows(const char* path, const Options* options, Totals* totals) {
    // Windows directory walk via FindFirstFile/FindNextFile.
    char pattern[PATH_BUFFER_SIZE];
    if (!join_path(path, "*", pattern, sizeof(pattern))) {
        ++totals->skipped;
        return;
    }

    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) {
        ++totals->skipped;
        return;
    }

    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) {
            continue;
        }

        if (is_excluded_name(options, data.cFileName)) {
            continue;
        }

        char full[PATH_BUFFER_SIZE];
        if (!join_path(path, data.cFileName, full, sizeof(full))) {
            ++totals->skipped;
            continue;
        }

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            walk_tree(full, options, totals);
            continue;
        }

        if (!has_code_extension(full, options)) {
            continue;
        }

        bool ok = false;
        uint64_t loc = count_file_loc(full, options->include_comments, &ok);
        if (ok) {
            ++totals->files;
            totals->loc += loc;
        } else {
            ++totals->skipped;
        }

    } while (FindNextFileA(h, &data));

    FindClose(h);
}
#else
static void walk_tree_posix(const char* path, const Options* options, Totals* totals) {
    // POSIX directory walk via opendir/readdir.
    DIR* dir = opendir(path);
    if (dir == NULL) {
        ++totals->skipped;
        return;
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (is_excluded_name(options, entry->d_name)) {
            continue;
        }

        char full[PATH_BUFFER_SIZE];
        if (!join_path(path, entry->d_name, full, sizeof(full))) {
            ++totals->skipped;
            continue;
        }

        struct stat st;
        if (stat(full, &st) != 0) {
            ++totals->skipped;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            walk_tree(full, options, totals);
            continue;
        }

        if (!S_ISREG(st.st_mode) || !has_code_extension(full, options)) {
            continue;
        }

        bool ok = false;
        uint64_t loc = count_file_loc(full, options->include_comments, &ok);
        if (ok) {
            ++totals->files;
            totals->loc += loc;
        } else {
            ++totals->skipped;
        }
    }

    closedir(dir);
}
#endif

static void walk_tree(const char* path, const Options* options, Totals* totals) {
    // If the root entry itself is excluded, skip it completely.
    char root_name[MAX_EXCLUDE_NAME_LEN + 1];
    if (basename_from_path(path, root_name, sizeof(root_name)) == 0 && is_excluded_name(options, root_name)) {
        return;
    }

#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        ++totals->skipped;
        return;
    }

    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        walk_tree_windows(path, options, totals);
        return;
    }

    if (has_code_extension(path, options)) {
        bool ok = false;
        uint64_t loc = count_file_loc(path, options->include_comments, &ok);
        if (ok) {
            ++totals->files;
            totals->loc += loc;
        } else {
            ++totals->skipped;
        }
    }
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        ++totals->skipped;
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        walk_tree_posix(path, options, totals);
        return;
    }

    if (S_ISREG(st.st_mode) && has_code_extension(path, options)) {
        bool ok = false;
        uint64_t loc = count_file_loc(path, options->include_comments, &ok);
        if (ok) {
            ++totals->files;
            totals->loc += loc;
        } else {
            ++totals->skipped;
        }
    }
#endif
}

static void print_usage(const char* exe) {
    printf("Usage: %s [-c] [-f ext1,ext2,...] [-x name1,name2,...] [path]\n", exe);
    printf("  -c    include comment-only lines in LOC\n");
    printf("  -f    only count these extensions (example: cpp,hpp,h)\n");
    printf("  -x    exclude file/folder names (example: vendor,backup,build)\n");
}

int main(int argc, char** argv) {
    Options options = {0};
    options.include_comments = false;
    options.root = ".";
    options.use_filter = false;
    options.filter_count = 0;
    options.exclude_count = 0;

    for (int i = 1; i < argc; ++i) {
        // Flags can appear before or after the path.
        if (strcmp(argv[i], "-c") == 0) {
            options.include_comments = true;
            continue;
        }

        if (strcmp(argv[i], "-f") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for -f.\n");
                print_usage(argv[0]);
                return 1;
            }

            if (parse_filter_list(&options, argv[i + 1]) != 0) {
                fprintf(stderr, "Invalid extension list for -f.\n");
                print_usage(argv[0]);
                return 1;
            }

            ++i;
            continue;
        }

        if (strcmp(argv[i], "-x") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for -x.\n");
                print_usage(argv[0]);
                return 1;
            }

            if (parse_exclude_list(&options, argv[i + 1]) != 0) {
                fprintf(stderr, "Invalid exclude list for -x.\n");
                print_usage(argv[0]);
                return 1;
            }

            ++i;
            continue;
        }

        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        if (options.root != NULL && strcmp(options.root, ".") != 0) {
            fprintf(stderr, "Too many arguments.\n");
            print_usage(argv[0]);
            return 1;
        }

        options.root = argv[i];
    }

    Totals totals = {0};
    walk_tree(options.root, &options, &totals);

    printf("Path: %s\n", options.root);
    printf("Files scanned: %llu\n", (unsigned long long)totals.files);
    printf("Lines of code: %llu\n", (unsigned long long)totals.loc);

    if (totals.skipped > 0) {
        printf("Skipped entries: %llu\n", (unsigned long long)totals.skipped);
    }

    return 0;
}
