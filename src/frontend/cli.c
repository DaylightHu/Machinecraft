#include "cli.h"
#include "console_command.h"
#include "line_input.h"
#include "command_completion.h"
#include "runtime_clock.h"
#include "rs/sim.h"
#include <inttypes.h>

typedef struct { FILE *output, *errors; } TerminalStreams;
static void terminal_output(void *context, const char *text, bool error)
{
    TerminalStreams *streams = context;
    fputs(text, error ? streams->errors : streams->output);
}

typedef struct {
    CommandCompletion completion;
    ConsoleCommandState commands;
} CliCommandInput;

static bool complete_command(char *line, size_t capacity, bool restart,
                              void *context)
{
    CliCommandInput *input = context;
    if (input->commands.plot.pending) return false;
    CommandCompletion *completion = &input->completion;
    if (restart) completion->active = false;
    return command_complete(completion, line, capacity, false, false);
}

CliRunResult cli_run(GameSession *session, FILE *input, FILE *output,
                     FILE *errors)
{
    if (!session || !input || !output || !errors) {
        return CLI_RUN_ERROR;
    }
    World *world = game_session_world(session);
    TerminalStreams streams = {output, errors};

    fputs("Redstone Sandbox (headless). See README.md for commands.\n", output);
    bool failed = false;
    bool return_to_title = false;
    char line[CONSOLE_COMMAND_CAPACITY];
    LineInput reader;
    if (!line_input_init(&reader, input, output, line, sizeof(line))) {
        fputs("error: cannot read input\n", errors);
        return CLI_RUN_ERROR;
    }
    CliCommandInput command_input = {0};
    if (!line_input_set_completion(&reader, complete_command, &command_input)) {
        fputs("error: cannot configure terminal input\n", errors);
        line_input_close(&reader);
        return CLI_RUN_ERROR;
    }
    double previous = runtime_clock_now();

    for (;;) {
        if (fflush(output) == EOF) {
            failed = true;
            break;
        }
        int read = line_input_poll(&reader, 10);
        double now = runtime_clock_now();
        double elapsed = now >= previous ? now - previous : 0.0;
        previous = now;
        RsResult advanced = game_session_advance(session, elapsed);
        if (advanced != RS_OK && advanced != RS_UNCHANGED) {
            fprintf(errors, "error: simulation: %s\n", rs_result_name(advanced));
            failed = true;
            break;
        }
        if (read == LINE_INPUT_PENDING) continue;
        if (read == LINE_INPUT_EOF) {
            break;
        }
        if (read < 0) {
            fputs(read == -1 ? "error: command too long\n" : "error: cannot read input\n", errors);
            failed = true;
            if (read == -2) {
                break;
            }
            continue;
        }

        ConsoleCommandReply reply = console_command_run(session,
            &command_input.commands, line, terminal_output, &streams);
        if (reply.saved && reader.interactive) failed = false;
        if (reply.failed) failed = true;
        if (reply.fatal) break;
        if (reply.steps) {
            uint64_t before = world_tick(world);
            RsResult result = RS_OK;
            while (sim_pending_steps(world) && result == RS_OK)
                result = game_session_advance(session, 0.0);
            if (result != RS_OK && result != RS_UNCHANGED) {
                fprintf(errors, "error: %s\n", rs_result_name(result));
                failed = true;
                break;
            }
            fprintf(output, "Advanced %" PRIu64 " ticks (frozen)\n", world_tick(world) - before);
        }
        if (reply.reset_clock) previous = runtime_clock_now();
        if (reply.action == CONSOLE_TITLE) { return_to_title = true; break; }
        if (reply.action == CONSOLE_QUIT) break;
    }

    line_input_close(&reader);
    if (fflush(output) == EOF || fflush(errors) == EOF || ferror(output) || ferror(errors)) {
        return CLI_RUN_ERROR;
    }
    if (return_to_title) return CLI_RUN_TITLE;
    return failed ? CLI_RUN_ERROR : CLI_RUN_EXIT;
}
