#if defined(RASPBERRY_PI)

#include "RpiFileSys.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <string.h>
#include <stdio.h>

RpiFileSys RpiFSobj;
bool       RpiFS_is_mounted = true;

static const char *SETTINGS_DIR = "/etc/softrf";

static void full_path(char *out, size_t outsz, const char *path) {
    snprintf(out, outsz, "%s%s", SETTINGS_DIR, path);
}

bool RpiFileSys::exists(const char *path) {
    char full[256];
    full_path(full, sizeof(full), path);
    return access(full, F_OK) == 0;
}

bool RpiFileSys::remove(const char *path) {
    char full[256];
    full_path(full, sizeof(full), path);
    return ::remove(full) == 0;
}

RpiFile RpiFileSys::open(const char *path, int mode) {
    char full[256];
    full_path(full, sizeof(full), path);
    mkdir(SETTINGS_DIR, 0755);
    const char *fmode;
    /* FILE_WRITE==1 conflicts with O_READ==0x01; check write first */
    bool is_write = (mode == FILE_WRITE) || (mode & O_WRITE) || (mode & O_CREAT);
    bool is_append = (bool)(mode & O_APPEND);
    if (is_write) {
        fmode = is_append ? "a" : "w";
    } else {
        fmode = "r";  /* FILE_READ==0 or O_READ==0x01 */
    }
    FILE *fp = fopen(full, fmode);
    return RpiFile(fp);
}


bool RpiFileSys::rename(const char *from, const char *to) {
    char f[256], t[256];
    full_path(f, sizeof(f), from);
    full_path(t, sizeof(t), to);
    return ::rename(f, t) == 0;
}
bool getline(RpiFile &f, char *buf, int limit) {
    if (!f || limit <= 0) return false;
    int i = 0, c;
    while (i < limit - 1 && (c = f.read()) != EOF) {
        if (c == '\n') break;
        if (c != '\r') buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return (i > 0) || (c != EOF && c != -1);
}

uint32_t FILESYS_free_kb() {
    struct statvfs st;
    if (statvfs(SETTINGS_DIR, &st) == 0)
        return (uint32_t)((uint64_t)st.f_bavail * st.f_frsize / 1024);
    return 0;
}

uint32_t IGCFS_free_kb() { return FILESYS_free_kb(); }

#endif /* RASPBERRY_PI */
