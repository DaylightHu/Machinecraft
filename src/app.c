#include "app.h"
#include "frontend/cli.h"
#include "line_input.h"
#include "rs/session.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const char save_directory[] = "saves";

static int select_save(FILE *input, FILE *output, const GameSaveList *saves,
                       size_t *selection)
{
    char line[64];
    LineInput reader;
    if (!line_input_init(&reader, input, output, line, sizeof(line))) return -1;
    for (;;) {
        fprintf(output, "Select a world [1-%zu], or q to quit: ", saves->count);
        if (fflush(output) == EOF) return -1;
        int read;
        do { read = line_input_poll(&reader, 1000); } while (read == LINE_INPUT_PENDING);
        if (read == LINE_INPUT_EOF) return 0;
        if (read == LINE_INPUT_ERROR) return -1;
        if (read == LINE_INPUT_TOO_LONG) {
            fputs("Invalid selection.\n", output);
            continue;
        }
        char *text = line;
        while (isspace((unsigned char)*text)) ++text;
        char *end = text + strlen(text);
        while (end > text && isspace((unsigned char)end[-1])) --end;
        *end = '\0';
        if ((text[0] == 'q' || text[0] == 'Q') && !text[1]) return 0;
        if (!*text || *text == '-') {
            fputs("Invalid selection.\n", output);
            continue;
        }
        errno = 0;
        char *tail = NULL;
        unsigned long value = strtoul(text, &tail, 10);
        if (!errno && tail != text && !*tail && value >= 1 &&
            value <= saves->count) {
            *selection = (size_t)value - 1;
            return 1;
        }
        fputs("Invalid selection.\n", output);
    }
}

int app_run(FILE *input, FILE *output, FILE *errors)
{
    if (!input || !output || !errors) return EXIT_FAILURE;
    for (;;) {
        GameSaveList saves;
        GameSessionResult session_result = game_session_list_saves(save_directory,
                                                                   &saves);
        if (session_result != GAME_SESSION_OK &&
            session_result != GAME_SESSION_TOO_MANY_SAVES) {
            fprintf(errors, "error: cannot list saves: %s\n",
                    game_session_result_name(session_result));
            return EXIT_FAILURE;
        }
        if (session_result == GAME_SESSION_TOO_MANY_SAVES)
            fprintf(errors, "warning: only the first %d saves are shown\n",
                    GAME_SESSION_MAX_SAVES);
        if (!saves.count) {
            fprintf(output, "No saved worlds found in %s/.\n", save_directory);
            return EXIT_SUCCESS;
        }
        fputs("Machinecraft CLI\nSaved worlds:\n", output);
        for (size_t i = 0; i < saves.count; ++i)
            fprintf(output, "  %zu) %s\n", i + 1, saves.names[i]);
        size_t selection;
        int selected = select_save(input, output, &saves, &selection);
        if (selected <= 0) return selected ? EXIT_FAILURE : EXIT_SUCCESS;
        char path[GAME_SESSION_PATH_CAPACITY];
        session_result = game_session_make_path(save_directory,
                                                saves.names[selection], path);
        if (session_result != GAME_SESSION_OK) {
            fprintf(errors, "error: cannot resolve save path: %s\n",
                    game_session_result_name(session_result));
            return EXIT_FAILURE;
        }
        GameSession *session = game_session_load(path, &session_result);
        if (!session) {
            fprintf(errors, "error: cannot load %s: %s\n", saves.names[selection],
                    game_session_result_name(session_result));
            return EXIT_FAILURE;
        }
        fprintf(output, "Loaded %s.\n", saves.names[selection]);
        CliRunResult result = cli_run(session, input, output, errors);
        game_session_destroy(session);
        if (result == CLI_RUN_TITLE) continue;
        return result == CLI_RUN_EXIT ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
