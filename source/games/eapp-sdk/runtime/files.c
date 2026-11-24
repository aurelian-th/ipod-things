/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/files.c (8fc840d) by lgreitis. */

#include "osos-2.0.5.h"
#include "platform.h"

struct eapp_file {
    uint32_t handle;
    uint32_t size;
    uint32_t position;
    uint8_t open;
    uint8_t writable;
};

static struct eapp_file files[12];

/* 0 when `name` stays inside a folder: no leading slash, no empty, "." or ".."
 * component, no trailing slash, no backslash or colon. */
static int name_is_inside(const char *name)
{
    if (*name == '/')
        return -1;
    const char *component = name;
    for (; *name; ++name) {
        if (name == component &&
            (*name == '.' && (name[1] == '/' || name[1] == 0 ||
                              (name[1] == '.' && (name[2] == '/' || name[2] == 0)))))
            return -1;
        if (*name == '\\' || *name == ':')
            return -1;
        if (*name == '/') {
            if (name == component || !name[1])
                return -1;
            component = name + 1;
        }
    }
    return 0;
}

const char *eapp_check_name(const char *name, int allow_root)
{
    if (!name)
        return NULL;
    while (name[0] == '.' && name[1] == '/')
        name += 2;
    if (!*name)
        return allow_root ? name : NULL;
    return name_is_inside(name) < 0 ? NULL : name;
}

static void close_handle(uint32_t handle)
{
    struct GameFileRequest *request = eapp_request_new(GameFileRequest_Close);
    request->handle = handle;
    eapp_request_finish(request, GameFile_QueueClose(request), 0);
}

/* The host's file `name` of the package folder `directory_kind`; the host
 * creates a missing file and its folders when read_only is 0. */
static struct eapp_file *open_name(uint32_t directory_kind, const char *name, int read_only)
{
    struct eapp_file *file = NULL;
    for (unsigned slot = 0; slot < sizeof(files) / sizeof(files[0]) && !file; ++slot)
        if (!files[slot].open)
            file = &files[slot];
    if (!file)
        return NULL;

    struct GameFileRequest *request = eapp_request_new(GameFileRequest_Open);
    if (eapp_request_finish(request,
                            GameFile_QueueOpen(directory_kind, name, read_only, request),
                            EAPP_REQUEST_SIZE | EAPP_REQUEST_HANDLE) != FileStatus_Ok) {
        /* Left open, the handle blocks a later write open of the same path
         * with a sharing conflict the host reports as success with garbage
         * outputs. */
        if (request->handle != EAPP_REQUEST_PENDING)
            close_handle(request->handle);
        return NULL;
    }
    file->handle = request->handle;
    file->size = request->size;
    file->position = 0;
    file->open = 1;
    file->writable = !read_only;
    return file;
}

struct eapp_file *eapp_file_open(const char *name)
{
    name = eapp_check_name(name, 0);
    return name ? open_name(GameDirectory_Package, name, 1) : NULL;
}

/* The host makes a file-only record of the full path on its UI task, with
 * the duration it is given. */
int eapp_music_append_file(const char *name, uint32_t duration_ms)
{
    char path[EAPP_MEDIA_TEXT_SIZE];
    int size = (int)sizeof path;
    name = eapp_check_name(name, 0);
    if (!name || GameSystem_GetPath(GameDirectory_Package, path, &size, name) < 0)
        return -1;
    GameAudio_AppendMediaSelection(path, NULL, 0, duration_ms);
    return 0;
}

static int delete_name(uint32_t directory_kind, const char *name)
{
    struct GameFileRequest *request = eapp_request_new(GameFileRequest_Delete);
    return eapp_request_finish(request, GameFile_QueueDelete(directory_kind, name, request),
                               0) == FileStatus_Ok
               ? 0
               : -1;
}

/* The host writes over a file without truncating it, so a write replaces the
 * old file by deleting it first; a missing one is not the caller's failure. */
static void delete_old(const char *name)
{
    uint32_t status = eapp_file_error_status;
    delete_name(GameDirectory_UserData, name);
    eapp_file_error_status = status;
}

struct eapp_file *eapp_data_open(const char *name, enum eapp_file_mode mode)
{
    name = eapp_check_name(name, 0);
    if (!name || (mode != EAPP_FILE_READ && mode != EAPP_FILE_WRITE))
        return NULL;
    if (mode == EAPP_FILE_WRITE)
        delete_old(name);
    return open_name(GameDirectory_UserData, name, mode == EAPP_FILE_READ);
}

/* What an empty whole-file transfer hands the host for its buffer. */
static uint8_t no_bytes;

/* Waits for a whole-file request. The host opens, transfers and closes in one
 * turn of its worker, so the open's result and size arrive before any byte
 * moves, and the transfer starts the count at 0 and adds to it as bytes are
 * copied. The request is done when the count reaches what the transfer moves
 * (all of a write, the file or the buffer for a read, whichever is smaller),
 * or when the result turns to a failure; the worker may still be closing
 * the file then, and takes the next request after it. The size arrives only
 * from an open that succeeded, and a read with nothing to move is done once
 * it has: the host reports a failure for a read of no bytes. 0, or -1 with
 * the status kept. */
static int finish_whole(struct GameFileRequest *request, int queued)
{
    const volatile struct GameFileRequest *r = request;
    int read = request->type == GameFileRequest_ReadFile;
    if (!queued) {
        eapp_file_error_status = FileStatus_NoSlot;
        return -1;
    }
    for (;; GameSystem_Sleep(0)) {
        if (read && r->size != EAPP_REQUEST_PENDING && (!r->size || !r->length))
            return 0;
        uint32_t result = r->result;
        if (result == EAPP_REQUEST_PENDING)
            continue;
        if (result != FileStatus_Ok) {
            eapp_file_error_status = result;
            return -1;
        }
        uint32_t target = r->length;
        if (read) {
            if (r->size == EAPP_REQUEST_PENDING)
                continue;
            if (r->size < target)
                target = r->size;
        }
        if (r->transferred == target)
            return 0;
    }
}

/* A whole-file read: the host opens the file, reads from its start and
 * closes it whatever happened, so no handle is left to give back. */
static int read_all(uint32_t directory_kind, const char *name, void *buffer, size_t capacity)
{
    if (!name || (!buffer && capacity))
        return -1;
    struct GameFileRequest *request = eapp_request_new(GameFileRequest_ReadFile);
    request->buffer = (uint32_t)(uintptr_t)(buffer ? buffer : &no_bytes);
    request->length = capacity > INT32_MAX ? INT32_MAX : (uint32_t)capacity;
    if (finish_whole(request, GameFile_QueueReadFile(directory_kind, name, request)) < 0 ||
        request->size > INT32_MAX)
        return -1;
    return (int)request->size;
}

int eapp_file_read_all(const char *path, void *buffer, size_t capacity)
{
    return read_all(GameDirectory_Package, eapp_check_name(path, 0), buffer, capacity);
}

int eapp_data_read(const char *name, void *buffer, size_t capacity)
{
    return read_all(GameDirectory_UserData, eapp_check_name(name, 0), buffer, capacity);
}

int eapp_data_write(const char *name, const void *data, size_t size)
{
    name = eapp_check_name(name, 0);
    if (!name || (!data && size) || size > INT32_MAX)
        return -1;
    delete_old(name);
    struct GameFileRequest *request = eapp_request_new(GameFileRequest_WriteFile);
    request->buffer = (uint32_t)(uintptr_t)(data ? data : &no_bytes);
    request->length = (uint32_t)size;
    return finish_whole(request, GameFile_QueueWriteFile(GameDirectory_UserData, name, request));
}

int eapp_data_delete(const char *name)
{
    name = eapp_check_name(name, 0);
    return name ? delete_name(GameDirectory_UserData, name) : -1;
}

/* A read, write or seek on the file's handle; the request's transferred count,
 * or -1 when the host refused. A read reports the count, a write the count
 * and the file's size, a seek only the result. */
static int transfer(struct eapp_file *file, uint32_t type, const void *buffer, uint32_t length,
                    uint32_t offset)
{
    struct GameFileRequest *request = eapp_request_new(type);
    request->handle = file->handle;
    request->buffer = (uint32_t)(uintptr_t)buffer;
    request->length = length;
    request->offset = (int32_t)offset;
    request->whence = FileSeek_Start;
    unsigned outputs = type == GameFileRequest_Read    ? EAPP_REQUEST_TRANSFERRED
                       : type == GameFileRequest_Write ? EAPP_REQUEST_TRANSFERRED |
                                                             EAPP_REQUEST_SIZE
                                                       : 0;
    if (eapp_request_finish(request, GameFile_QueueTransfer(request), outputs) != FileStatus_Ok)
        return -1;
    return type == GameFileRequest_Seek ? 0 : (int)request->transferred;
}

int eapp_file_write(struct eapp_file *file, const void *data, size_t size)
{
    if (!file || !file->open || !file->writable || (!data && size) || size > INT32_MAX)
        return -1;
    if (!size)
        return 0;
    int written = transfer(file, GameFileRequest_Write, data, size, 0);
    if (written != (int)size)
        return -1;
    file->position += written;
    if (file->position > file->size)
        file->size = file->position;
    return written;
}

int eapp_file_read(struct eapp_file *file, void *buffer, size_t size)
{
    if (!file || !file->open || (!buffer && size))
        return -1;
    if (size > file->size - file->position)
        size = file->size - file->position;
    if (!size)
        return 0;
    int read = transfer(file, GameFileRequest_Read, buffer, size, 0);
    if (read < 0 || (size_t)read > size)
        return -1;
    file->position += read;
    return read;
}

int eapp_file_seek(struct eapp_file *file, uint32_t offset)
{
    if (!file || !file->open || offset > file->size || offset > INT32_MAX ||
        transfer(file, GameFileRequest_Seek, NULL, 0, offset) < 0)
        return -1;
    file->position = offset;
    return 0;
}

uint32_t eapp_file_size(const struct eapp_file *file)
{
    return file->size;
}

uint32_t eapp_file_position(const struct eapp_file *file)
{
    return file->position;
}

void eapp_file_close(struct eapp_file *file)
{
    if (!file || !file->open)
        return;
    file->open = 0;
    close_handle(file->handle);
}

void eapp_files_close_all(void)
{
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
        eapp_file_close(&files[i]);
}
