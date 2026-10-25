#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "line_input.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

bool line_input_init(LineInput *r, FILE *input, FILE *echo,
                     char *line, size_t capacity)
{
    if (!r || !input || !line || capacity < 2) return false;
    *r = (LineInput){.echo = echo, .line = line, .capacity = capacity};
#ifdef _WIN32
    r->fd = _fileno(input);
    if (r->fd < 0) return false;
    intptr_t handle = _get_osfhandle(r->fd);
    if (handle == -1) return false;
    r->handle = (HANDLE)handle;
    DWORD mode;
    r->console = GetConsoleMode(r->handle, &mode) != 0;
    r->pipe = !r->console && GetFileType(r->handle) == FILE_TYPE_PIPE;
#else
    r->fd = fileno(input);
    if (r->fd < 0) return false;
#endif
    line[0] = '\0';
    return true;
}

/* Character result: 1 byte, 0 EOF, -1 error, 2 no byte yet. */
static int input_byte(LineInput *r, unsigned timeout_ms, unsigned char *byte)
{
#ifdef _WIN32
    if (r->repeat) {
        --r->repeat;
        *byte = r->repeated;
        return 1;
    }
    if (r->console) {
        DWORD wait = WaitForSingleObject(r->handle, timeout_ms);
        if (wait == WAIT_TIMEOUT) return 2;
        if (wait != WAIT_OBJECT_0) return -1;
        INPUT_RECORD event;
        DWORD count;
        if (!ReadConsoleInputW(r->handle, &event, 1, &count)) return -1;
        if (!count || event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown)
            return 2;
        WCHAR ch = event.Event.KeyEvent.uChar.UnicodeChar;
        if (!ch) return 2;
        *byte = ch < 128 ? (unsigned char)ch : (unsigned char)'?';
        r->repeated = *byte;
        r->repeat = event.Event.KeyEvent.wRepeatCount ?
                    event.Event.KeyEvent.wRepeatCount - 1u : 0u;
        return 1;
    }
    if (r->pipe) {
        DWORD available;
        if (!PeekNamedPipe(r->handle, NULL, 0, NULL, &available, NULL))
            return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
        if (!available) {
            if (timeout_ms) Sleep(timeout_ms < 10 ? timeout_ms : 10);
            return 2;
        }
    }
    DWORD count;
    if (!ReadFile(r->handle, byte, 1, &count, NULL))
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    return count ? 1 : 0;
#else
    struct pollfd descriptor = {.fd = r->fd, .events = POLLIN};
    int timeout = timeout_ms > INT_MAX ? INT_MAX : (int)timeout_ms;
    int available = poll(&descriptor, 1, timeout);
    if (available < 0) return errno == EINTR ? 2 : -1;
    if (!available) return 2;
    if (descriptor.revents & POLLNVAL) return -1;
    ssize_t count = read(r->fd, byte, 1);
    if (count < 0)
        return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK ? 2 : -1;
    return count ? 1 : 0;
#endif
}

int line_input_poll(LineInput *r, unsigned timeout_ms)
{
    if (!r || !r->line) return LINE_INPUT_ERROR;
    if (r->ready) {
        r->ready = false;
        r->length = r->discarded = 0;
        r->line[0] = '\0';
    }
    if (r->eof) return LINE_INPUT_EOF;
    unsigned char ch;
    int status = input_byte(r, timeout_ms, &ch);
    if (status < 0) return LINE_INPUT_ERROR;
    if (status == 2) return LINE_INPUT_PENDING;
#ifdef _WIN32
    if (r->console && status == 1) {
        if (ch == 26) status = 0;
        else if (ch == '\r') ch = '\n';
        else if (ch == '\b') {
            if (r->discarded) --r->discarded;
            else if (r->length) r->line[--r->length] = '\0';
            else return LINE_INPUT_PENDING;
            if (r->echo) {
                fputs("\b \b", r->echo);
                fflush(r->echo);
            }
            return LINE_INPUT_PENDING;
        }
        if (status == 1 && r->echo) {
            fputc(ch, r->echo);
            fflush(r->echo);
        }
    }
#endif
    if (!status) {
        r->eof = true;
        if (!r->length && !r->discarded) return LINE_INPUT_EOF;
    }
    if (!status || ch == '\n') {
        r->ready = true;
        return r->discarded ? LINE_INPUT_TOO_LONG : LINE_INPUT_READY;
    }
    if (!ch) {
        r->discarded = SIZE_MAX;
        return LINE_INPUT_PENDING;
    }
    if (ch == '\r') return LINE_INPUT_PENDING;
    if (r->length + 1 < r->capacity)
        r->line[r->length++] = (char)ch;
    else if (r->discarded < SIZE_MAX)
        ++r->discarded;
    r->line[r->length] = '\0';
    return LINE_INPUT_PENDING;
}
