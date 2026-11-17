/* dw_assets_host (dw_assets_host.h): files loaded on first use and kept until close. */
#include "dw_assets_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 128   /* every file of the set (93 at wave 2 Task 14: #12 took the count past 64) */
typedef struct { char name[40]; void *mem; size_t size; bool overlay; } file_t;
static file_t s_files[MAX_FILES];
static unsigned s_n;
static char s_dir[512], s_ov[512];
static bool s_has_ov;

bool dw_assets_host_open(const char *dir, const char *overlay_dir)
{
    dw_assets_host_close();
    if (!dir || strlen(dir) >= sizeof s_dir || (overlay_dir && strlen(overlay_dir) >= sizeof s_ov)) return false;
    strcpy(s_dir, dir);
    s_has_ov = overlay_dir != NULL;
    if (s_has_ov) strcpy(s_ov, overlay_dir);
    return true;
}

void dw_assets_host_close(void)
{
    for (unsigned i = 0; i < s_n; i++) free(s_files[i].mem);
    memset(s_files, 0, sizeof s_files);
    s_n = 0;
}

static void *load(const char *dir, const char *name, size_t *size)
{
    char path[600];
    snprintf(path, sizeof path, "%s/%s", dir, name);
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

static file_t *find(const char *name)
{
    for (unsigned i = 0; i < s_n; i++)
        if (strcmp(s_files[i].name, name) == 0) return &s_files[i];
    if (strlen(name) >= sizeof s_files[0].name || strchr(name, '/')) return NULL;
    if (s_n == MAX_FILES) {   /* never a silent miss (the Task 14 review): a full table is the harness's bug */
        fprintf(stderr, "dw_assets_host: %u files already loaded (MAX_FILES): cannot load %s\n", (unsigned)MAX_FILES, name);
        abort();
    }
    file_t f = {{0}, NULL, 0, false};
    strcpy(f.name, name);
    if (s_has_ov && (f.mem = load(s_ov, name, &f.size)) != NULL) f.overlay = true;
    else f.mem = load(s_dir, name, &f.size);
    if (!f.mem) return NULL;
    s_files[s_n] = f;
    return &s_files[s_n++];
}

const void *dw_assets_host_get(const char *name, size_t *size)
{
    const file_t *f = name ? find(name) : NULL;
    if (size) *size = f ? f->size : 0;
    return f ? f->mem : NULL;
}

bool dw_assets_host_from_overlay(const char *name)
{
    const file_t *f = name ? find(name) : NULL;
    return f && f->overlay;
}
