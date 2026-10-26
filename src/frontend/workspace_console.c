#include "workspace_console.h"
#include "command_completion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HISTORY_LINES = 192, LINE_LENGTH = 768, INPUT_HISTORY = 32,
       CELL_WIDTH = 12, FONT_SIZE = 16, ROW_HEIGHT = 20, HEADER_HEIGHT = 25 };
typedef struct { char text[LINE_LENGTH]; bool error; } ConsoleLine;
struct WorkspaceConsole {
    ConsoleCommandState commands;
    CommandCompletion completion;
    ConsoleLine lines[HISTORY_LINES];
    int first, count, scroll;
    char partial[LINE_LENGTH];
    size_t partial_length;
    bool partial_error;
    char input[CONSOLE_COMMAND_CAPACITY], draft[CONSOLE_COMMAND_CAPACITY];
    size_t length, cursor;
    char history[INPUT_HISTORY][CONSOLE_COMMAND_CAPACITY];
    int history_count, history_at;
};

void workspace_console_message(WorkspaceConsole *console, const char *text, bool error)
{
    int index;
    if (console->count == HISTORY_LINES) {
        index = console->first;
        console->first = (console->first + 1) % HISTORY_LINES;
    } else index = (console->first + console->count++) % HISTORY_LINES;
    snprintf(console->lines[index].text, LINE_LENGTH, "%s", text);
    console->lines[index].error = error;
    console->scroll = 0;
}
WorkspaceConsole *workspace_console_create(void)
{
    WorkspaceConsole *console = calloc(1, sizeof(*console));
    if (console) {
        workspace_console_message(console, "mcmux CLI", false);
        workspace_console_message(console, "Type help. Ctrl+b ? for pane keys.", false);
    }
    return console;
}
void workspace_console_destroy(WorkspaceConsole *console) { free(console); }
void workspace_console_detach_world(WorkspaceConsole *console)
{
    if (!console) return;
    if (console->commands.plot.pending)
        workspace_console_message(console, "World detached; pending shrink cancelled", false);
    console->commands.plot = (PlotCommandState){0};
    console->completion = (CommandCompletion){0};
}
static void output_fragment(void *context, const char *text, bool error)
{
    WorkspaceConsole *console = context;
    while (*text) {
        if (*text == '\n') {
            console->partial[console->partial_length] = '\0';
            workspace_console_message(console, console->partial, console->partial_error || error);
            console->partial_length = 0;
            console->partial_error = false;
        } else if (*text != '\r') {
            console->partial[console->partial_length++] = *text;
            console->partial_error |= error;
            if (console->partial_length == sizeof(console->partial) - 1) {
                console->partial[console->partial_length] = '\0';
                workspace_console_message(console, console->partial, console->partial_error);
                console->partial_length = 0;
                console->partial_error = false;
            }
        }
        ++text;
    }
}
static bool key(int code) { return IsKeyPressed(code) || IsKeyPressedRepeat(code); }
static void changed(WorkspaceConsole *console)
{
    console->completion.active = false;
    console->history_at = console->history_count;
}
static void insert(WorkspaceConsole *console, int character)
{
    if (character < 32 || character >= 127 || console->length + 1 >= sizeof(console->input)) return;
    memmove(console->input + console->cursor + 1, console->input + console->cursor,
            console->length - console->cursor + 1);
    console->input[console->cursor++] = (char)character;
    ++console->length;
    changed(console);
}
static void recall(WorkspaceConsole *console, int delta)
{
    if (!console->history_count) return;
    if (console->history_at == console->history_count)
        strcpy(console->draft, console->input);
    int next = console->history_at + delta;
    if (next < 0 || next > console->history_count) return;
    console->history_at = next;
    strcpy(console->input, next == console->history_count ? console->draft : console->history[next]);
    console->length = console->cursor = strlen(console->input);
    console->completion.active = false;
}

static bool workspace_help_requested(const char *input)
{
    while (*input == ' ' || *input == '\t') ++input;
    if (strncmp(input, "help", 4) != 0) return false;
    input += 4;
    while (*input == ' ' || *input == '\t') ++input;
    return !*input;
}

ConsoleCommandReply workspace_console_update(WorkspaceConsole *console,
    GameSession **session, WorkspaceConsoleWorldCommand world_command,
    void *context)
{
    ConsoleCommandReply reply = {0};
    if (!console || !session) return reply;
    bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool super = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (console->commands.plot.pending)
            reply = console_command_run(*session, &console->commands, "n",
                                        output_fragment, console);
        console->input[0] = '\0';
        console->length = console->cursor = 0;
        changed(console);
        if (console->partial_length) {
            console->partial[console->partial_length] = '\0';
            workspace_console_message(console, console->partial,
                                      console->partial_error);
            console->partial_length = 0;
            console->partial_error = false;
        }
        return reply;
    }
    if (IsKeyPressed(KEY_PAGE_UP)) console->scroll += 8;
    if (IsKeyPressed(KEY_PAGE_DOWN)) console->scroll -= 8;
    if (console->scroll < 0) console->scroll = 0;
    if (console->scroll > HISTORY_LINES*LINE_LENGTH) console->scroll = HISTORY_LINES*LINE_LENGTH;
    if (control && IsKeyPressed(KEY_C)) {
        if (console->commands.plot.pending)
            reply = console_command_run(*session, &console->commands, "n",
                                        output_fragment, console);
        console->input[0] = '\0';
        console->length = console->cursor = 0;
        changed(console);
    } else if (control && IsKeyPressed(KEY_U)) {
        console->input[0] = '\0'; console->length = console->cursor = 0; changed(console);
    } else if ((control || super) && IsKeyPressed(KEY_V)) {
        const char *text = GetClipboardText();
        if (text) for (; *text; ++text) insert(console, *text == '\n' || *text == '\r' ? ' ' : (unsigned char)*text);
    }
    int character;
    while ((character = GetCharPressed()) != 0)
        if (!control && !super) insert(console, character);
    if (key(KEY_BACKSPACE) && console->cursor) {
        memmove(console->input + console->cursor - 1, console->input + console->cursor,
                console->length - console->cursor + 1);
        --console->cursor; --console->length; changed(console);
    }
    if (key(KEY_DELETE) && console->cursor < console->length) {
        memmove(console->input + console->cursor, console->input + console->cursor + 1,
                console->length - console->cursor);
        --console->length; changed(console);
    }
    if (key(KEY_LEFT) && console->cursor) --console->cursor;
    if (key(KEY_RIGHT) && console->cursor < console->length) ++console->cursor;
    if (IsKeyPressed(KEY_HOME)) console->cursor = 0;
    if (IsKeyPressed(KEY_END)) console->cursor = console->length;
    if (key(KEY_UP)) recall(console, -1);
    if (key(KEY_DOWN)) recall(console, 1);
    if (IsKeyPressed(KEY_TAB) && !console->commands.plot.pending && console->cursor == console->length) {
        command_complete(&console->completion, console->input,
                         sizeof(console->input), false, true);
        console->length = console->cursor = strlen(console->input);
    }
    if (!IsKeyPressed(KEY_ENTER)) return reply;
    char echo[CONSOLE_COMMAND_CAPACITY + 8];
    snprintf(echo, sizeof(echo), "%s%s", console->commands.plot.pending ? "y/n > " : "> ", console->input);
    workspace_console_message(console, echo, false);
    if (console->length) {
        if (console->history_count == INPUT_HISTORY) {
            memmove(console->history, console->history + 1,
                    (INPUT_HISTORY - 1)*sizeof(console->history[0]));
            --console->history_count;
        }
        strcpy(console->history[console->history_count++], console->input);
    }
    bool show_workspace_help = workspace_help_requested(console->input);
    bool handled = world_command &&
        world_command(context, console, console->input, session);
    if (!handled) {
        reply = console_command_run(*session, &console->commands, console->input,
                                    output_fragment, console);
        if (show_workspace_help) {
            workspace_console_message(console,
                "Here, title saves and detaches CLI; quit saves and closes this pane",
                false);
        }
    }
    if (console->partial_length) {
        console->partial[console->partial_length] = '\0';
        workspace_console_message(console, console->partial, console->partial_error);
        console->partial_length = 0; console->partial_error = false;
    }
    if (reply.steps) {
        char message[96];
        snprintf(message, sizeof(message), "Queued %u manual ticks", reply.steps);
        workspace_console_message(console, message, false);
    }
    console->input[0] = '\0';
    console->length = console->cursor = 0;
    changed(console);
    return reply;
}
static void draw_cells(const char *text, size_t length, int x, int y, Color color)
{
    for (size_t i = 0; i < length; ++i) {
        char character[2] = {text[i], '\0'};
        DrawText(character, x + (int)i*CELL_WIDTH, y, FONT_SIZE, color);
    }
}
void workspace_console_draw(WorkspaceConsole *console, Rectangle box, int pane,
                              bool focused, bool zoomed)
{
    if (box.width < 2 || box.height < 2) return;
    Color accent = focused ? (Color){172, 143, 255, 255} : (Color){79, 89, 99, 255};
    DrawRectangleRec(box, (Color){17, 22, 29, 255});
    BeginScissorMode((int)box.x, (int)box.y, (int)box.width, (int)box.height);
    DrawRectangle((int)box.x, (int)box.y, (int)box.width, HEADER_HEIGHT, (Color){34, 42, 53, 255});
    DrawText(TextFormat("%d: mc-cli%s%s", pane, zoomed ? " [zoom]" : "", focused ? " *" : ""),
        (int)box.x + 8, (int)box.y + 5, FONT_SIZE, accent);
    int columns = ((int)box.width - 16)/CELL_WIDTH;
    if (columns < 1) columns = 1;
    int prompt_y = (int)(box.y + box.height) - ROW_HEIGHT - 6;
    int rows = (prompt_y - (int)box.y - HEADER_HEIGHT - 5)/ROW_HEIGHT;
    if (rows < 0) rows = 0;
    int total = 0;
    for (int i = 0; i < console->count; ++i) {
        size_t length = strlen(console->lines[(console->first + i) % HISTORY_LINES].text);
        total += length ? (int)((length + columns - 1)/columns) : 1;
    }
    int maximum = total > rows ? total - rows : 0;
    if (console->scroll > maximum) console->scroll = maximum;
    int skip = console->scroll, drawn = 0;
    for (int i = console->count - 1; i >= 0 && drawn < rows; --i) {
        ConsoleLine *line = &console->lines[(console->first + i) % HISTORY_LINES];
        size_t length = strlen(line->text);
        int wrapped = length ? (int)((length + columns - 1)/columns) : 1;
        for (int row = wrapped - 1; row >= 0 && drawn < rows; --row) {
            if (skip) { --skip; continue; }
            size_t start = (size_t)row*columns;
            size_t count = length - start;
            if (count > (size_t)columns) count = (size_t)columns;
            int y = prompt_y - (++drawn)*ROW_HEIGHT - 4;
            draw_cells(line->text + start, count, (int)box.x + 8, y,
                line->error ? (Color){255, 128, 128, 255} : (Color){211, 222, 231, 255});
        }
    }
    if (prompt_y >= (int)box.y + HEADER_HEIGHT) {
        const char *prompt = console->commands.plot.pending ? "y/n> " : "> ";
        size_t prefix = strlen(prompt);
        int available = columns - (int)prefix;
        if (available < 1) available = 1;
        size_t start = console->cursor >= (size_t)available ? console->cursor - available + 1 : 0;
        size_t count = console->length - start;
        if (count > (size_t)available) count = (size_t)available;
        DrawLine((int)box.x + 4, prompt_y - 5, (int)(box.x + box.width) - 4, prompt_y - 5, accent);
        draw_cells(prompt, prefix, (int)box.x + 8, prompt_y, accent);
        draw_cells(console->input + start, count, (int)box.x + 8 + (int)prefix*CELL_WIDTH, prompt_y, RAYWHITE);
        if (focused && ((int)(GetTime()*2)) % 2 == 0) {
            int x = (int)box.x + 8 + ((int)prefix + (int)(console->cursor - start))*CELL_WIDTH;
            DrawRectangle(x, prompt_y + FONT_SIZE, CELL_WIDTH - 2, 2, accent);
        }
    }
    EndScissorMode();
    DrawRectangleLinesEx(box, focused ? 2 : 1, accent);
}
