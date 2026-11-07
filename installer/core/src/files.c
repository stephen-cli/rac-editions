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

#else
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

FILE *file_open(const char *path, const char *mode)
{
    return fopen(path, mode);
}

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

bool file_seek(FILE *file, uint64_t offset)
{
    return fseeko(file, (off_t)offset, SEEK_SET) == 0;
}

uint64_t file_size(FILE *file)
{
    fseeko(file, 0, SEEK_END);
    return (uint64_t)ftello(file);
}

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

static bool make_directory(const char *directory)
{
    return mkdir(directory, 0777) == 0 || errno == EEXIST;
}

static bool is_separator(char c)
{
    return c == '/';
}
#endif

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
