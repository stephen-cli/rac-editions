#include "files.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

/* PATH as UTF-16 for the Windows API; NULL when it is not valid UTF-8. Freed by the caller. */
static wchar_t *wide_path(const char *path)
{
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (length == 0) {
        return NULL;
    }
    wchar_t *wide = malloc(sizeof(wchar_t) * (size_t)length);
    if (wide == NULL) {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, length);
    /* A path with the "\\?\" prefix (Tauri's resource folder is one) reaches the file system as it is, so its
       "/" are not taken as separators; the core joins names on with "/", so they become "\" here. */
    if (wcsncmp(wide, L"\\\\?\\", 4) == 0) {
        for (wchar_t *c = wide; *c != 0; c++) {
            if (*c == L'/') {
                *c = L'\\';
            }
        }
    }
    return wide;
}

FILE *file_open(const char *path, const char *mode)
{
    wchar_t *wide = wide_path(path);
    wchar_t wide_mode[8];
    size_t used = 0;
    for (; mode[used] != 0 && used < 7; used++) {
        wide_mode[used] = (wchar_t)mode[used];
    }
    wide_mode[used] = 0;
    if (wide == NULL) {
        errno = EINVAL;
        return NULL;
    }
    FILE *file = _wfopen(wide, wide_mode);
    free(wide);
    return file;
}

bool file_info(const char *path, FileInfo *info)
{
    wchar_t *wide = wide_path(path);
    if (wide == NULL) {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL found = GetFileAttributesExW(wide, GetFileExInfoStandard, &data);
    free(wide);
    if (!found) {
        return false;
    }
    /* FILETIME counts 100 ns intervals since 1601; 11644473600 seconds separate 1601 from 1970. */
    int64_t since_1601 = (int64_t)((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32 | data.ftLastWriteTime.dwLowDateTime);
    info->modified_ns = (since_1601 - 116444736000000000LL) * 100;
    info->size = (uint64_t)data.nFileSizeHigh << 32 | data.nFileSizeLow;
    info->is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
}

bool file_seek(FILE *file, uint64_t offset)
{
    return _fseeki64(file, (long long)offset, SEEK_SET) == 0;
}

uint64_t file_size(FILE *file)
{
    _fseeki64(file, 0, SEEK_END);
    return (uint64_t)_ftelli64(file);
}

/* A UTF-16 string as UTF-8 into BUFFER; false when it does not fit. */
static bool narrow_path(const wchar_t *wide, char *buffer, size_t capacity)
{
    int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, (int)capacity, NULL, NULL);
    return length > 0;
}

bool file_resolve(const char *path, char *resolved, size_t capacity)
{
    wchar_t *wide = wide_path(path);
    if (wide == NULL) {
        return false;
    }
    /* FILE_FLAG_BACKUP_SEMANTICS opens folders too; no access is asked for, only the handle's final path */
    HANDLE handle = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    wchar_t final_path[32768];
    DWORD length = GetFinalPathNameByHandleW(handle, final_path, 32768, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    if (length == 0 || length >= 32768) {
        return false;
    }
    /* "\\?\C:\x" is C:\x, and "\\?\UNC\server\share" is \\server\share, as Python's resolve() spells them */
    const wchar_t *shown = final_path;
    if (wcsncmp(final_path, L"\\\\?\\UNC\\", 8) == 0) {
        final_path[6] = L'\\';
        shown = final_path + 6;
    } else if (wcsncmp(final_path, L"\\\\?\\", 4) == 0) {
        shown = final_path + 4;
    }
    return narrow_path(shown, resolved, capacity);
}

bool file_replace(const char *from, const char *to)
{
    wchar_t *wide_from = wide_path(from);
    wchar_t *wide_to = wide_path(to);
    bool replaced = wide_from != NULL && wide_to != NULL &&
                    MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING) != 0;
    free(wide_from);
    free(wide_to);
    return replaced;
}

static bool make_directory(const char *directory)
{
    wchar_t *wide = wide_path(directory);
    if (wide == NULL) {
        return false;
    }
    bool made = CreateDirectoryW(wide, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
    free(wide);
    return made;
}

static bool is_separator(char c)
{
    return c == '\\' || c == '/';
}

#define SEPARATOR '\\'

bool file_remove(const char *path)
{
    wchar_t *wide = wide_path(path);
    if (wide == NULL) {
        errno = EINVAL;
        return false;
    }
    bool removed = _wremove(wide) == 0;
    free(wide);
    return removed;
}

bool file_free_space(const char *folder, uint64_t *free_bytes)
{
    wchar_t *wide = wide_path(folder);
    if (wide == NULL) {
        return false;
    }
    /* the drive's free bytes, as Python's shutil.disk_usage counts them */
    ULARGE_INTEGER total_free;
    BOOL asked = GetDiskFreeSpaceExW(wide, NULL, NULL, &total_free);
    free(wide);
    *free_bytes = total_free.QuadPart;
    return asked != 0;
}

/* The length of PATH's anchor: "C:\" or "C:", "\\server\share\", "\", or none. */
static size_t anchor_length(const char *path)
{
    if (path[0] != 0 && path[1] == ':') {
        return is_separator(path[2]) ? 3 : 2;
    }
    if (is_separator(path[0]) && is_separator(path[1])) {
        /* \\server\share\ */
        size_t at = 2;
        for (int parts = 0; parts < 2 && path[at] != 0; parts++) {
            while (path[at] != 0 && !is_separator(path[at])) {
                at++;
            }
            if (path[at] != 0) {
                at++;
            }
        }
        return at;
    }
    return is_separator(path[0]) ? 1 : 0;
}

#else
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(__lv2ppu__)
/* the PS3's lv2: newlib's POSIX calls over its file system, which has no links and no statvfs */
#include <lv2/sysfs.h>
#include <ppu-lv2.h>
#else
#include <sys/statvfs.h>
#endif

#if defined(__lv2ppu__)
/* The toolchain's stdio loses an image's size past 4 GB (a seek to the end comes back cut to 32 bits, and the
   stream then reads nothing), while lv2's own calls are 64-bit. So a file is a stdio stream over those calls. */
#include <sys/file.h>

typedef struct {
    s32 fd;
} Lv2File;

/* An lv2 error (RPCS3's ErrorCodes.h names them) as errno: the core tells a full drive (ENOSPC) and a name taken
   (EEXIST) from other failures. */
static int lv2_errno(s32 error)
{
    switch ((u32)error) {
    case 0x80010006u:
        return ENOENT;
    case 0x80010012u:
        return EISDIR;
    case 0x80010014u:
        return EEXIST;
    case 0x80010023u:
        return ENOSPC;
    case 0x80010026u:
        return EROFS;
    case 0x80010029u:
        return EACCES;
    default:
        return EIO;
    }
}

static ssize_t lv2_read(void *cookie, char *buffer, size_t size)
{
    u64 got = 0;
    s32 failed = sysLv2FsRead(((Lv2File *)cookie)->fd, buffer, size, &got);
    if (failed != 0) {
        errno = lv2_errno(failed);
        return -1;
    }
    return (ssize_t)got;
}

static ssize_t lv2_write(void *cookie, const char *buffer, size_t size)
{
    u64 written = 0;
    s32 failed = sysLv2FsWrite(((Lv2File *)cookie)->fd, buffer, size, &written);
    if (failed != 0) {
        errno = lv2_errno(failed);
        return -1;
    }
    return (ssize_t)written;
}

#ifdef __LARGE64_FILES
static int lv2_seek(void *cookie, _off64_t *offset, int whence)
#else
static int lv2_seek(void *cookie, off_t *offset, int whence)
#endif
{
    u64 position = 0;
    if (sysLv2FsLSeek64(((Lv2File *)cookie)->fd, (u64)*offset, whence, &position) != 0) {
        errno = EINVAL;
        return -1;
    }
    *offset = (off_t)position;
    return 0;
}

static int lv2_close(void *cookie)
{
    s32 closed = sysLv2FsClose(((Lv2File *)cookie)->fd);
    free(cookie);
    return closed == 0 ? 0 : -1;
}

FILE *file_open(const char *path, const char *mode)
{
    /* the modes the core uses: "rb", "r+b", "wb", "ab", "wbx" */
    s32 flags;
    if (mode[0] == 'r') {
        flags = strchr(mode, '+') != NULL ? SYS_O_RDWR : SYS_O_RDONLY;
    } else if (mode[0] == 'w') {
        flags = SYS_O_WRONLY | SYS_O_CREAT | (strchr(mode, 'x') != NULL ? SYS_O_EXCL : SYS_O_TRUNC);
    } else if (mode[0] == 'a') {
        flags = SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND;
    } else {
        errno = EINVAL;
        return NULL;
    }
    Lv2File *cookie = malloc(sizeof *cookie);
    if (cookie == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    s32 failed = sysLv2FsOpen(path, flags, &cookie->fd, 0666, NULL, 0);
    if (failed != 0) {
        free(cookie);
        errno = lv2_errno(failed);
        return NULL;
    }
    char stream_mode[4] = {mode[0], 'b', strchr(mode, '+') != NULL ? '+' : 0, 0};
    cookie_io_functions_t functions = {lv2_read, lv2_write, lv2_seek, lv2_close};
    FILE *file = fopencookie(cookie, stream_mode, functions);
    if (file == NULL) {
        sysLv2FsClose(cookie->fd);
        free(cookie);
        return NULL;
    }
    /* reads and writes in 1 MiB, not stdio's 1 KiB: the PS3's hard drive, between a disc image read and the new
       image written, is many times faster in large pieces (64 KiB copied at 12 MB/s) */
    setvbuf(file, NULL, _IOFBF, 1024 * 1024);
    return file;
}
#else
FILE *file_open(const char *path, const char *mode)
{
    return fopen(path, mode);
}
#endif

#if defined(__lv2ppu__)
bool file_info(const char *path, FileInfo *info)
{
    sysFSStat status;
    if (sysLv2FsStat(path, &status) != 0) {
        return false;
    }
    info->size = status.st_size;
    info->modified_ns = (int64_t)status.st_mtime * 1000000000;   /* lv2 keeps whole seconds */
    info->is_directory = S_ISDIR(status.st_mode);
    return true;
}
#else
bool file_info(const char *path, FileInfo *info)
{
    struct stat status;
    if (stat(path, &status) != 0) {
        return false;
    }
    info->size = (uint64_t)status.st_size;
#if defined(__APPLE__)
    info->modified_ns = (int64_t)status.st_mtimespec.tv_sec * 1000000000 + status.st_mtimespec.tv_nsec;
#else
    info->modified_ns = (int64_t)status.st_mtim.tv_sec * 1000000000 + status.st_mtim.tv_nsec;
#endif
    info->is_directory = S_ISDIR(status.st_mode);
    return true;
}
#endif

bool file_seek(FILE *file, uint64_t offset)
{
    return fseeko(file, (off_t)offset, SEEK_SET) == 0;
}

uint64_t file_size(FILE *file)
{
    fseeko(file, 0, SEEK_END);
    return (uint64_t)ftello(file);
}

#if defined(__lv2ppu__)
/* lv2 has no links, and its paths are absolute ("/dev_hdd0/..."), so a path that exists is its own real spelling. */
bool file_resolve(const char *path, char *resolved, size_t capacity)
{
    struct stat status;
    if (stat(path, &status) != 0 || strlen(path) >= capacity) {
        return false;
    }
    strcpy(resolved, path);
    return true;
}

/* lv2 renames only onto a free name, so the file being replaced goes first. */
bool file_replace(const char *from, const char *to)
{
    struct stat status;
    if (stat(to, &status) == 0 && remove(to) != 0) {
        return false;
    }
    return rename(from, to) == 0;
}
#else
bool file_resolve(const char *path, char *resolved, size_t capacity)
{
    char *real = realpath(path, NULL);
    if (real == NULL) {
        return false;
    }
    bool fits = strlen(real) < capacity;
    if (fits) {
        strcpy(resolved, real);
    }
    free(real);
    return fits;
}

bool file_replace(const char *from, const char *to)
{
    return rename(from, to) == 0;
}
#endif

static bool make_directory(const char *directory)
{
    return mkdir(directory, 0777) == 0 || errno == EEXIST;
}

static bool is_separator(char c)
{
    return c == '/';
}

#define SEPARATOR '/'

bool file_remove(const char *path)
{
    return remove(path) == 0;
}

#if defined(__lv2ppu__)
/* lv2's sys_fs_disk_free (syscall 840): a device's size and free bytes. Not the sys_fs library's
   cellFsGetFreeSize (PSL1GHT's sysFsGetFreeSize): a console hangs in that unless the library is loaded first,
   and webMAN MOD uses the syscall too. */
static s32 disk_free(const char *device, u64 *total, u64 *available)
{
    lv2syscall3(840, (u64)(uintptr_t)device, (u64)(uintptr_t)total, (u64)(uintptr_t)available);
    return_to_user_prog(s32);
}

bool file_free_space(const char *folder, uint64_t *free_bytes)
{
    /* the device a path is on: "/dev_hdd0" of "/dev_hdd0/PS2ISO" */
    char device[64];
    if (folder[0] != '/') {
        return false;
    }
    const char *end = strchr(folder + 1, '/');
    size_t length = end != NULL ? (size_t)(end - folder) : strlen(folder);
    if (length >= sizeof device) {
        return false;
    }
    memcpy(device, folder, length);
    device[length] = 0;
    u64 total = 0, available = 0;
    if (disk_free(device, &total, &available) != 0) {
        return false;
    }
    *free_bytes = available;
    return true;
}
#else
bool file_free_space(const char *folder, uint64_t *free_bytes)
{
    struct statvfs status;
    if (statvfs(folder, &status) != 0) {
        return false;
    }
    /* the bytes free to an unprivileged writer, as Python's shutil.disk_usage counts them */
    *free_bytes = (uint64_t)status.f_bavail * status.f_frsize;
    return true;
}
#endif

static size_t anchor_length(const char *path)
{
    return is_separator(path[0]) ? 1 : 0;
}
#endif

void path_clean(const char *path, char *clean, size_t capacity)
{
    if (capacity == 0) {
        return;
    }
    size_t anchor = anchor_length(path);
    size_t used = 0;
    for (size_t i = 0; i < anchor && used + 1 < capacity; i++) {
        clean[used++] = is_separator(path[i]) ? SEPARATOR : path[i];
    }
    const char *part = path + anchor;
    while (*part != 0) {
        size_t length = 0;
        while (part[length] != 0 && !is_separator(part[length])) {
            length++;
        }
        bool kept = length > 0 && !(length == 1 && part[0] == '.');
        if (kept) {
            if (used > anchor && used + 1 < capacity) {
                clean[used++] = SEPARATOR;
            }
            for (size_t i = 0; i < length && used + 1 < capacity; i++) {
                clean[used++] = part[i];
            }
        }
        part += length;
        while (is_separator(*part)) {
            part++;
        }
    }
    if (used == 0 && capacity > 1) {
        clean[used++] = '.';
    }
    clean[used] = 0;
}

void path_parent(const char *path, char *parent, size_t capacity)
{
    if (capacity == 0) {
        return;
    }
    size_t anchor = anchor_length(path);
    size_t last = strlen(path);
    while (last > anchor && !is_separator(path[last - 1])) {
        last--;
    }
    size_t length;
    if (last > anchor) {
        length = last - 1;          /* up to the last separator */
    } else if (anchor > 0) {
        length = anchor;            /* "C:\x" has "C:\"; a root, itself */
    } else {
        length = 0;                 /* "x" is in "." */
    }
    if (length == 0) {
        snprintf(parent, capacity, ".");
        return;
    }
    snprintf(parent, capacity, "%.*s", (int)length, path);
}

bool file_read_at(FILE *file, uint64_t offset, void *buffer, size_t size)
{
    if (!file_seek(file, offset)) {
        return false;
    }
    return fread(buffer, 1, size, file) == size;
}

bool file_write_at(FILE *file, uint64_t offset, const void *buffer, size_t size)
{
    if (!file_seek(file, offset)) {
        return false;
    }
    return fwrite(buffer, 1, size, file) == size;
}

char *file_read_all(const char *path, size_t *size)
{
    FILE *file = file_open(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    uint64_t length = file_size(file);
    char *bytes = length < SIZE_MAX ? malloc((size_t)length + 1) : NULL;
    if (bytes == NULL || !file_read_at(file, 0, bytes, (size_t)length)) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    bytes[length] = 0;
    *size = (size_t)length;
    return bytes;
}

bool file_make_directories(const char *directory)
{
    size_t length = strlen(directory);
    char *partial = malloc(length + 1);
    if (partial == NULL) {
        return false;
    }
    memcpy(partial, directory, length + 1);
    /* each folder from the top down, skipping a drive ("C:") or the root, which always exist */
    for (size_t i = 1; i < length; i++) {
        if (is_separator(partial[i]) && partial[i - 1] != ':' && !is_separator(partial[i - 1])) {
            partial[i] = 0;
            make_directory(partial);
            partial[i] = directory[i];
        }
    }
    bool made = make_directory(partial);
    free(partial);
    return made;
}
