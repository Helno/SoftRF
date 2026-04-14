#pragma once
/* POSIX-backed filesystem shim for RPi SoftRF settings */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>


/* SdFat-compatible open flags for RPi */
#ifndef O_READ
#define O_READ   0x01
#define O_WRITE  0x02
#define O_CREAT  0x10
#define O_APPEND 0x04
#define O_TRUNC  0x08
#endif
#ifndef FILE_READ
#define FILE_READ  0
#endif
#ifndef FILE_WRITE
#define FILE_WRITE 1
#endif

class RpiFile {
    FILE *fp_;
public:
    RpiFile() : fp_(nullptr) {}
    explicit RpiFile(FILE *f) : fp_(f) {}
    operator bool() const { return fp_ != nullptr; }
    void   close()  { if (fp_) { fclose(fp_); fp_ = nullptr; } }
    size_t write(const uint8_t *buf, size_t len) {
        return fp_ ? fwrite(buf, 1, len, fp_) : 0;
    }
    int available() {
        long cur = ftell(fp_);
        fseek(fp_, 0, SEEK_END);
        long end = ftell(fp_);
        fseek(fp_, cur, SEEK_SET);
        return (int)(end - cur);
    }
    int read() { return fp_ ? fgetc(fp_) : -1; }
    size_t print(const char *s)    { return fp_ ? fputs(s, fp_) >= 0 ? strlen(s) : 0 : 0; }
    size_t print(int v)            { return fp_ ? (size_t)fprintf(fp_, "%d", v) : 0; }
    size_t println(const char *s)  { return fp_ ? (size_t)fprintf(fp_, "%s\n", s) : 0; }
    size_t println(int v)          { return fp_ ? (size_t)fprintf(fp_, "%d\n", v) : 0; }
    void   flush()                 { if (fp_) fflush(fp_); }
};
typedef RpiFile File;

class RpiFileSys {
public:
    bool    exists(const char *path);
    bool    remove(const char *path);
    RpiFile open(const char *path, int mode);
    bool    rename(const char *from, const char *to);
};

extern RpiFileSys RpiFSobj;
extern bool       RpiFS_is_mounted;

bool getline(RpiFile &f, char *buf, int limit);
uint32_t FILESYS_free_kb();
uint32_t IGCFS_free_kb();
