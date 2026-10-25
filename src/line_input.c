#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "line_input.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifndef _WIN32
/* Only the CLI opts into character input; the world picker stays unchanged. */
static const int terminal_signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};
enum { TERMINAL_SIGNAL_COUNT = sizeof(terminal_signals)/sizeof(terminal_signals[0]) };
static struct termios terminal_saved;
static struct sigaction terminal_actions[TERMINAL_SIGNAL_COUNT];
static int terminal_fd;
static volatile sig_atomic_t terminal_active;
static bool terminal_cleanup_registered;

static void terminal_signal_set(sigset_t *set)
{
    sigemptyset(set);
    for (size_t i = 0; i < TERMINAL_SIGNAL_COUNT; ++i)
        sigaddset(set, terminal_signals[i]);
}

static int set_terminal_mode(int fd, const struct termios *mode)
{
    int result;
    do { result = tcsetattr(fd, TCSANOW, mode); } while (result < 0 && errno == EINTR);
    return result;
}

static void restore_terminal(void)
{
    sigset_t set, previous;
    terminal_signal_set(&set);
    if (sigprocmask(SIG_BLOCK, &set, &previous) != 0) return;
    if (terminal_active) {
        terminal_active = 0;
        (void)set_terminal_mode(terminal_fd, &terminal_saved);
        for (size_t i = 0; i < TERMINAL_SIGNAL_COUNT; ++i)
            (void)sigaction(terminal_signals[i], &terminal_actions[i], NULL);
    }
    (void)sigprocmask(SIG_SETMASK, &previous, NULL);
}

static void terminal_interrupted(int signal_number)
{
    int previous_errno = errno;
    restore_terminal();
    (void)raise(signal_number);
    errno = previous_errno;
}

static bool start_terminal(int fd)
{
    if (terminal_active) return terminal_fd == fd;
    if (!terminal_cleanup_registered) {
        if (atexit(restore_terminal) != 0) return false;
        terminal_cleanup_registered = true;
    }
    sigset_t set, previous;
    terminal_signal_set(&set);
    if (sigprocmask(SIG_BLOCK, &set, &previous) != 0) return false;
    bool have_settings = tcgetattr(fd, &terminal_saved) == 0;
    bool ok = have_settings;
    size_t installed = 0;
    struct sigaction action = {0};
    action.sa_handler = terminal_interrupted;
    action.sa_mask = set;
    while (ok && installed < TERMINAL_SIGNAL_COUNT) {
        ok = sigaction(terminal_signals[installed], &action,
                       &terminal_actions[installed]) == 0;
        if (ok) ++installed;
    }
    if (ok) {
        struct termios mode = terminal_saved;
        mode.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        mode.c_cc[VMIN] = 1;
        mode.c_cc[VTIME] = 0;
        ok = set_terminal_mode(fd, &mode) == 0;
    }
    if (ok) {
        terminal_fd = fd;
        terminal_active = 1;
    } else {
        if (have_settings) (void)set_terminal_mode(fd, &terminal_saved);
        for (size_t i = 0; i < installed; ++i)
            (void)sigaction(terminal_signals[i], &terminal_actions[i], NULL);
    }
    (void)sigprocmask(SIG_SETMASK, &previous, NULL);
    return ok;
}
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

bool line_input_set_completion(LineInput *r, LineInputComplete complete,
                                void *context)
{
    if (!r || !r->line || !complete) return false;
#ifdef _WIN32
    r->interactive = r->console;
#else
    r->interactive = r->echo && isatty(r->fd) && isatty(fileno(r->echo));
    if (r->interactive && !start_terminal(r->fd)) {
        r->interactive = false;
        return false;
    }
#endif
    r->complete = r->interactive ? complete : NULL;
    r->complete_context = context;
    return true;
}

void line_input_close(LineInput *r)
{
    if (!r) return;
#ifndef _WIN32
    if (r->interactive && terminal_active && terminal_fd == r->fd)
        restore_terminal();
#endif
    r->interactive = false;
    r->complete = NULL;
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
#ifndef _WIN32
    /* A stopped/resumed process or a returning signal handler restores the
     * terminal first; switch back to character input on the next poll. */
    if (r->interactive && !terminal_active && !start_terminal(r->fd))
        return LINE_INPUT_ERROR;
#endif
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
    bool edit = r->interactive;
#ifdef _WIN32
    edit = r->console;
#endif
    if (edit && status == 1) {
        if (ch == '\t' && r->complete) {
            size_t before = r->length;
            if (!r->discarded && r->complete(r->line, r->capacity,
                                             !r->completing, r->complete_context)) {
                r->length = strlen(r->line);
                if (r->echo) {
                    for (size_t i = 0; i < before; ++i) fputs("\b \b", r->echo);
                    fputs(r->line, r->echo);
                    fflush(r->echo);
                }
            }
            r->completing = true;
            return LINE_INPUT_PENDING;
        }
        r->completing = false;
#ifndef _WIN32
        if (ch == 27) { r->escape = 1; return LINE_INPUT_PENDING; }
        if (r->escape == 1) {
            r->escape = 0;
            if (ch == '[' || ch == 'O') { r->escape = 2; return LINE_INPUT_PENDING; }
        } else if (r->escape == 2) {
            if (ch >= 0x40 && ch <= 0x7e) r->escape = 0;
            return LINE_INPUT_PENDING;
        }
        if (ch == 4) status = 0;
#else
        if (ch == 26) status = 0;
#endif
        else if (ch == '\r') ch = '\n';
        else if (ch == '\b' || ch == 127) {
            if (r->discarded) --r->discarded;
            else if (r->length) r->line[--r->length] = '\0';
            else return LINE_INPUT_PENDING;
            if (r->echo) {
                fputs("\b \b", r->echo);
                fflush(r->echo);
            }
            return LINE_INPUT_PENDING;
        }
        if (status == 1 && ch < 32 && ch != '\n' && ch != '\t')
            return LINE_INPUT_PENDING;
        if (ch >= 128) ch = '?';
        if (status == 1 && r->echo) {
            fputc(ch, r->echo);
            fflush(r->echo);
        }
    }
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
