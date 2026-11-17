/* rd_assets_host (rd_assets_host.h): files loaded on first use and kept until close. */
#include "rd_assets_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 128   /* every file of the set (wave 1: the shared rows, the faces' layers and the brass) */
typedef struct { char name[40]; void *mem; size_t size; } file_t;
static file_t s_files[MAX_FILES];
static unsigned s_n;
static char s_dir[512];

bool rd_assets_host_open(const char *dir)
{
    rd_assets_host_close();
    if (!dir || strlen(dir) >= sizeof s_dir) return false;
    strcpy(s_dir, dir);
    return true;
}

void rd_assets_host_close(void)
{
    for (unsigned i = 0; i < s_n; i++) free(s_files[i].mem);
    memset(s_files, 0, sizeof s_files);
    s_n = 0;
}

static void *load(const char *name, size_t *size)
{
    char path[600];
    snprintf(path, sizeof path, "%s/%s", s_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    void *mem = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0) {
            const size_t al = ((size_t)n + 63) / 64 * 64;
            if (posix_memalign(&mem, 64, al) != 0) mem = NULL;
            if (mem && fread(mem, 1, (size_t)n, f) == (size_t)n) {
                *size = (size_t)n;
            } else {
                free(mem);
                mem = NULL;
            }
        }
    }
    fclose(f);
    return mem;
}

const void *rd_assets_host_get(const char *name, size_t *size)
{
    const file_t *hit = NULL;
    for (unsigned i = 0; name && i < s_n && !hit; i++)
        if (strcmp(s_files[i].name, name) == 0) hit = &s_files[i];
    if (!hit && name && strlen(name) < sizeof s_files[0].name && !strchr(name, '/')) {
        if (s_n == MAX_FILES) {   /* never a silent miss: a full table is the harness's bug */
            fprintf(stderr, "rd_assets_host: %u files already loaded (MAX_FILES): cannot load %s\n", (unsigned)MAX_FILES,
                    name);
            abort();
        }
        file_t f = {{0}, NULL, 0};
        strcpy(f.name, name);
        if ((f.mem = load(name, &f.size)) != NULL) {
            s_files[s_n] = f;
            hit = &s_files[s_n++];
        }
    }
    if (size) *size = hit ? hit->size : 0;
    return hit ? hit->mem : NULL;
}
