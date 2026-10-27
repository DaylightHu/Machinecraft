#include "console_command.h"
#include "plot_command.h"
#include "tick_command.h"
#include "edit_command.h"
#include "rs/sim.h"

#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { TOKEN_CAPACITY = 8 };

typedef struct {
    ConsoleCommandOutput write;
    void *context;
    bool error;
} ConsoleSink;

static void console_puts(const char *text, ConsoleSink *sink)
{
    if (sink->write) sink->write(sink->context, text, sink->error);
}

static void console_printf(ConsoleSink *sink, const char *format, ...)
{
    char text[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    console_puts(text, sink);
}


typedef enum {
    COMMAND_HELP, COMMAND_PLACE, COMMAND_REMOVE, COMMAND_GET,
    COMMAND_DOT, COMMAND_DELAY, COMMAND_MODE, COMMAND_TUNE, COMMAND_CAPACITY,
    COMMAND_HIT, COMMAND_SLOT, COMMAND_SLOTS, COMMAND_STATS,
    COMMAND_DROPPER_SEED, COMMAND_SAVE, COMMAND_TITLE, COMMAND_QUIT
} CommandKind;

typedef struct {
    CommandKind kind;
    BlockPos position;
    BlockId block;
    Direction support;
    uint32_t ticks;
    double hit_u, hit_v;
    bool persistent_projectile;
    uint32_t slot;
    ContainerStack stack;
    uint64_t seed;
} Command;

static const char *const direction_names[DIR_COUNT] = {
    "east", "west", "up", "down", "south", "north"
};

static int tokenize(char *line, char *tokens[TOKEN_CAPACITY])
{
    int count = 0;
    char *cursor = line;

    while (*cursor) {
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (!*cursor) {
            break;
        }
        if (count == TOKEN_CAPACITY) {
            return -1;
        }
        tokens[count++] = cursor;
        while (*cursor && !isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor) {
            *cursor++ = '\0';
        }
    }
    return count;
}

static bool parse_integer(const char *text, int *value)
{
    errno = 0;
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end || parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool parse_position(char *tokens[], BlockPos *position)
{
    return parse_integer(tokens[1], &position->x) &&
           parse_integer(tokens[2], &position->y) &&
           parse_integer(tokens[3], &position->z);
}

static bool parse_block(const char *name, BlockId *block)
{
    for (BlockId id = BLOCK_IRON; id < BLOCK_INTERNAL_FIRST; ++id) {
        if (strcmp(name, block_name(id)) == 0) {
            *block = id;
            return true;
        }
    }
    return false;
}

static bool parse_direction(const char *name, Direction *direction)
{
    for (Direction face = 0; face < DIR_COUNT; ++face) {
        if (strcmp(name, direction_names[face]) == 0) {
            *direction = face;
            return true;
        }
    }
    return false;
}

static bool parse_ticks(const char *text, uint32_t *ticks)
{
    if (*text == '-') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end || parsed > UINT32_MAX) {
        return false;
    }
    *ticks = (uint32_t)parsed;
    return true;
}

static bool parse_barrel_level(const char *text, uint32_t *level)
{
    return parse_ticks(text, level) && *level <= 15;
}

static bool parse_seed(const char *text, uint64_t *seed)
{
    if (*text == '-') return false;
    errno = 0;
    char *end = NULL;
    uintmax_t parsed = strtoumax(text, &end, 10);
    if (errno == ERANGE || end == text || *end || parsed > UINT64_MAX)
        return false;
    *seed = (uint64_t)parsed;
    return true;
}

static bool parse_hit_coordinate(const char *text, double *value)
{
    errno = 0;
    char *end = NULL;
    double parsed = strtod(text, &end);
    if (errno == ERANGE || end == text || *end ||
        !(parsed >= 0.0 && parsed <= 1.0)) return false;
    *value = parsed;
    return true;
}

static bool parse_command(char *tokens[], int count, Command *command)
{
    *command = (Command){ .support = DIR_DOWN, .ticks = 1 };
    const char *name = tokens[0];

    if (strcmp(name, "help") == 0 && count == 1) {
        command->kind = COMMAND_HELP;
    } else if (strcmp(name, "save") == 0 && count == 1) {
        command->kind = COMMAND_SAVE;
    } else if (strcmp(name, "title") == 0 && count == 1) {
        command->kind = COMMAND_TITLE;
    } else if (strcmp(name, "quit") == 0 && count == 1) {
        command->kind = COMMAND_QUIT;
    } else if (strcmp(name, "stats") == 0 && count == 1) {
        command->kind = COMMAND_STATS;
    } else if (strcmp(name, "dropper-seed") == 0 && count == 2) {
        command->kind = COMMAND_DROPPER_SEED;
        return parse_seed(tokens[1], &command->seed);
    } else if (strcmp(name, "place") == 0 && (count == 5 || count == 6)) {
        command->kind = COMMAND_PLACE;
        bool valid = parse_position(tokens, &command->position) &&
                     parse_block(tokens[4], &command->block) &&
                     (count == 5 || parse_direction(tokens[5], &command->support));
        if (valid && count == 5 &&
            (command->block == BLOCK_REDSTONE_REPEATER ||
             command->block == BLOCK_REDSTONE_COMPARATOR ||
             command->block == BLOCK_POWERED_RAIL ||
             command->block == BLOCK_ACTIVATOR_RAIL ||
             command->block == BLOCK_OBSERVER ||
             command->block == BLOCK_BARREL ||
             command->block == BLOCK_DROPPER ||
             block_is_piston_base(command->block)))
            command->support = DIR_NORTH;
        return valid;
    } else if (strcmp(name, "capacity") == 0 && count == 5) {
        command->kind = COMMAND_CAPACITY;
        return parse_position(tokens, &command->position) &&
               parse_barrel_level(tokens[4], &command->ticks);
    } else if (strcmp(name, "hit") == 0 && count == 7) {
        command->kind = COMMAND_HIT;
        command->persistent_projectile = strcmp(tokens[6], "arrow") == 0 ||
                                         strcmp(tokens[6], "trident") == 0;
        return parse_position(tokens, &command->position) &&
               parse_hit_coordinate(tokens[4], &command->hit_u) &&
               parse_hit_coordinate(tokens[5], &command->hit_v) &&
               (command->persistent_projectile || strcmp(tokens[6], "other") == 0);
    } else if (strcmp(name, "slot") == 0 && count == 8) {
        command->kind = COMMAND_SLOT;
        uint32_t item, amount, limit;
        if (!parse_position(tokens, &command->position) ||
            !parse_ticks(tokens[4], &command->slot) ||
            !parse_ticks(tokens[5], &item) || !parse_ticks(tokens[6], &amount) ||
            !parse_ticks(tokens[7], &limit) || amount > 64 || limit > 64)
            return false;
        command->stack = (ContainerStack){item, (uint8_t)amount, (uint8_t)limit};
        return (!amount && !item && !limit) ||
               (item && amount && amount <= limit && (limit == 1 || limit == 16 || limit == 64));
    } else if (count == 4) {
        if (strcmp(name, "slots") == 0) {
            command->kind = COMMAND_SLOTS;
        } else if (strcmp(name, "remove") == 0) {
            command->kind = COMMAND_REMOVE;
        } else if (strcmp(name, "get") == 0) {
            command->kind = COMMAND_GET;
        } else if (strcmp(name, "dot") == 0) {
            command->kind = COMMAND_DOT;
        } else if (strcmp(name, "delay") == 0) {
            command->kind = COMMAND_DELAY;
        } else if (strcmp(name, "mode") == 0) {
            command->kind = COMMAND_MODE;
        } else if (strcmp(name, "tune") == 0) {
            command->kind = COMMAND_TUNE;
        } else {
            return false;
        }
        return parse_position(tokens, &command->position);
    } else {
        return false;
    }
    return true;
}

static void print_help(ConsoleSink *output)
{
    console_puts("place X Y Z BLOCK [DIRECTION]\n"
          "remove X Y Z\nget X Y Z\ndot X Y Z\ndelay X Y Z\n"
          "mode X Y Z\ntune X Y Z\ncapacity X Y Z LEVEL\n"
          "slots X Y Z\nslot X Y Z INDEX ITEM COUNT LIMIT (zero based; 0 0 0 clears)\n"
          "hit X Y Z U V arrow|trident|other (U/V: 0..1 on the hit face)\n"
          "dropper-seed SEED\n"
          "plot info\nplot expand east|west|south|north [COUNT]\n"
          "plot shrink east|west|south|north [COUNT] (y/n confirmation when deleting edits)\n"
          "tick freeze (toggle; also cancels pending manual steps)\n"
          "tick step NUMBER (positive integer; requires freeze)\n"
          "tick noupdate (toggle block updates; skipped updates are not replayed)\n"
          "pos1 X Y Z / pos2 X Y Z (select inclusive corners)\n"
          "copy [X Y Z] (default anchor: selection minimum)\n"
          "schem save [-f] NAME [X Y Z] (save selection to structures/NAME.mcs; -f replaces)\n"
          "paste [-a] X Y Z (paste at anchor; -a skips air)\n"
          "stack [COUNT] DIRECTION / move [DISTANCE] DIRECTION\n"
          "undo / redo (one paste, stack or move edit)\n"
          "stats\nsave\n"
          "title (save and return to world selection)\n"
          "help\nquit (save and exit)\nBlocks:", output);
    for (BlockId id = BLOCK_IRON; id < BLOCK_INTERNAL_FIRST; ++id) {
        console_printf(output, " %s", block_name(id));
    }
    console_puts("\nTorch direction is its support: down/east/west/north/south; default down.\n"
          "Repeater/comparator/observer direction is output; piston direction is its front; barrel direction is its lid; rail direction selects its horizontal axis.\n"
          "Observer, barrel, dropper and both piston types accept all six directions. Gates and rails are horizontal; default north.\n"
          "Hopper direction is its outlet: down or horizontal; default down. Up maps to down.\n",
          output);
}

static RsResult print_block(const World *world, BlockPos position, ConsoleSink *output)
{
    if (!world_in_bounds(world, position)) {
        return RS_OUT_OF_BOUNDS;
    }

    BlockState state = world_get(world, position);
    console_printf(output, "%d %d %d: %s", position.x, position.y, position.z,
            block_name((BlockId)state.id));
    switch ((BlockId)state.id) {
    case BLOCK_REDSTONE_WIRE:
        console_printf(output, " power=%u connections=0x%02x",
                (unsigned)state.data.wire.power, (unsigned)state.data.wire.connections);
        break;
    case BLOCK_REDSTONE_TORCH:
        console_printf(output, " lit=%s support=%s", state.data.torch.lit ? "on" : "off",
                direction_names[state.data.torch.support_dir]);
        break;
    case BLOCK_REDSTONE_LAMP:
        console_printf(output, " lit=%s", state.data.lamp.lit ? "on" : "off");
        break;
    case BLOCK_REDSTONE_REPEATER:
        console_printf(output, " powered=%s facing=%s delay=%u locked=%s",
                state.data.repeater.powered ? "on" : "off",
                direction_names[state.data.repeater.facing],
                (unsigned)state.data.repeater.delay,
                state.data.repeater.locked ? "yes" : "no");
        break;
    case BLOCK_REDSTONE_COMPARATOR:
        console_printf(output, " output=%u facing=%s mode=%s powered=%s stored_output=%u",
                state.data.comparator.powered ?
                    (unsigned)state.data.comparator.output : 0u,
                direction_names[state.data.comparator.facing],
                state.data.comparator.subtract ? "subtract" : "compare",
                state.data.comparator.powered ? "on" : "off",
                (unsigned)state.data.comparator.output);
        break;
    case BLOCK_POWERED_RAIL:
    case BLOCK_ACTIVATOR_RAIL:
        console_printf(output, " powered=%s facing=%s slope=%s",
                state.data.rail.powered ? "yes" : "no",
                direction_names[state.data.rail.facing],
                state.data.rail.ascending ? "ascending" : "flat");
        break;
    case BLOCK_OBSERVER:
        console_printf(output, " powered=%s facing=%s",
                state.data.observer.powered ? "yes" : "no",
                direction_names[state.data.observer.facing]);
        break;
    case BLOCK_PISTON:
    case BLOCK_STICKY_PISTON:
        console_printf(output, " powered=%s extended=%s facing=%s",
                state.data.piston.powered ? "yes" : "no",
                state.data.piston.extended ? "yes" : "no",
                direction_names[state.data.piston.facing]);
        break;
    case BLOCK_PISTON_HEAD:
    case BLOCK_STICKY_PISTON_HEAD:
        console_printf(output, " powered=%s facing=%s",
                state.data.piston.powered ? "yes" : "no",
                direction_names[state.data.piston.facing]);
        break;
    case BLOCK_MOVING_PISTON: {
        WorldPistonMotion motion;
        if (world_get_piston_motion(world, position, &motion))
            console_printf(output,
                    " payload=%s facing=%s extending=%s source=%s "
                    "progress=%u/2 previous=%u/2",
                    block_name((BlockId)motion.moved_state.id),
                    direction_names[motion.direction],
                    motion.extending ? "yes" : "no",
                    motion.source ? "yes" : "no",
                    (unsigned)motion.progress,
                    (unsigned)motion.previous_progress);
        break;
    }
    case BLOCK_BARREL:
        console_printf(output, " capacity=%u facing=%s open=%s",
                (unsigned)state.data.barrel.level,
                direction_names[state.data.barrel.facing],
                state.data.barrel.open ? "yes" : "no");
        break;
    case BLOCK_NOTE_BLOCK:
        console_printf(output, " powered=%s note=%u instrument=%s",
                state.data.note_block.powered ? "yes" : "no",
                (unsigned)state.data.note_block.note,
                note_instrument_name((NoteInstrument)state.data.note_block.instrument));
        break;
    case BLOCK_TARGET:
        console_printf(output, " power=%u", (unsigned)state.data.target.power);
        break;
    case BLOCK_HOPPER:
        console_printf(output, " facing=%s enabled=%s level=%u cooldown=%u",
                direction_names[state.data.hopper.facing],
                state.data.hopper.enabled ? "yes" : "no",
                (unsigned)state.data.hopper.level,
                (unsigned)world_hopper_cooldown(world, position));
        break;
    case BLOCK_DROPPER:
        console_printf(output, " facing=%s triggered=%s level=%u",
                direction_names[state.data.dropper.facing],
                state.data.dropper.triggered ? "yes" : "no",
                (unsigned)state.data.dropper.level);
        break;
    case BLOCK_IRON: {
        SolidPower power = redstone_solid_power(world, position);
        console_printf(output, " received=%u strong=%u", (unsigned)power.received,
                (unsigned)power.strong);
        break;
    }
    default:
        break;
    }
    console_puts("\n", output);
    return RS_OK;
}

static void print_stats(const World *world, ConsoleSink *output)
{
    SimStats stats = sim_stats(world);
    console_printf(output, "revision=%" PRIu64 " pending=%zu "
            "loaded_chunks=%zu stored_chunks=%zu components=%" PRIu64
            " wires=%" PRIu64 " rail_components=%" PRIu64
            " rails=%" PRIu64 " events=%" PRIu64 "\n",
            world_revision(world), stats.pending_events,
            world_loaded_chunks(world), world_stored_chunks(world),
            stats.components_solved, stats.wires_visited,
            stats.rail_components_solved, stats.rails_visited,
            stats.events_processed);
}

static RsResult execute_command(World *world, const Command *command,
                                ConsoleSink *output)
{
    RsResult result;

    switch (command->kind) {
    case COMMAND_HELP:
        print_help(output);
        return RS_OK;
    case COMMAND_GET:
        return print_block(world, command->position, output);
    case COMMAND_SLOTS: {
        size_t count = world_container_size(world, command->position);
        if (!count) return RS_INVALID_ARGUMENT;
        for (size_t i = 0; i < count; ++i) {
            ContainerStack stack;
            result = world_get_container_slot(world, command->position, i, &stack);
            if (result != RS_OK) return result;
            console_printf(output, "%zu item=%" PRIu32 " count=%u limit=%u\n", i, stack.item,
                    (unsigned)stack.count, (unsigned)stack.limit);
        }
        return RS_OK;
    }
    case COMMAND_STATS:
        print_stats(world, output);
        return RS_OK;
    case COMMAND_PLACE:
        result = world_place(world, command->position, command->block, command->support);
        break;
    case COMMAND_DROPPER_SEED:
        result = world_set_dropper_seed(world, command->seed);
        break;
    case COMMAND_REMOVE:
        result = world_remove(world, command->position);
        break;
    case COMMAND_DOT:
        result = world_toggle_wire_dot(world, command->position);
        break;
    case COMMAND_DELAY:
        result = world_cycle_repeater_delay(world, command->position);
        break;
    case COMMAND_MODE:
        result = world_toggle_comparator_mode(world, command->position);
        break;
    case COMMAND_TUNE:
        result = world_cycle_note(world, command->position);
        break;
    case COMMAND_CAPACITY:
        result = world_set_barrel_level(
            world, command->position, (uint8_t)command->ticks);
        break;
    case COMMAND_HIT:
        result = world_hit_target(world, command->position,
                                  command->hit_u, command->hit_v,
                                  command->persistent_projectile);
        break;
    case COMMAND_SLOT:
        result = world_set_container_slot(world, command->position,
                                          command->slot, command->stack);
        break;
    default:
        return RS_INVALID_ARGUMENT;
    }

    if (result != RS_OK && result != RS_UNCHANGED) {
        return result;
    }
    RsResult flushed = sim_flush(world);
    if (flushed != RS_OK) {
        return flushed;
    }
    console_printf(output, "%s\n", rs_result_name(result));
    return RS_OK;
}

/* Execute one command only. The host owns input, output and time advancement. */
ConsoleCommandReply console_command_run(GameSession *session,
    ConsoleCommandState *state, const char *input,
    ConsoleCommandOutput write, void *context)
{
    ConsoleCommandReply reply = {0};
    ConsoleSink output_sink = {write, context, false};
    ConsoleSink error_sink = {write, context, true};
    ConsoleSink *output = &output_sink, *errors = &error_sink;
    if (!state || !input) {
        reply.failed = true;
        return reply;
    }
    if (strlen(input) >= CONSOLE_COMMAND_CAPACITY) {
        console_puts("error: command too long\n", errors);
        reply.failed = true;
        return reply;
    }
    char line[CONSOLE_COMMAND_CAPACITY];
    strcpy(line, input);
    EditCommand edit;
    TickCommand tick;
    PlotCommand plot;

    if (!session) {
        state->plot = (PlotCommandState){0};
        if (edit_command_parse(line, &edit) || tick_command_parse(line, &tick) ||
            plot_command_parse(line, &plot)) {
            console_puts("error: no world loaded\n", errors);
            reply.failed = true;
            return reply;
        }
        char *tokens[TOKEN_CAPACITY];
        int count = tokenize(line, tokens);
        if (!count) return reply;
        Command command;
        if (count < 0 || !parse_command(tokens, count, &command)) {
            console_puts("error: invalid command; use help\n", errors);
            reply.failed = true;
            return reply;
        }
        if (command.kind == COMMAND_HELP) {
            print_help(output);
        } else if (command.kind == COMMAND_TITLE) {
            reply.action = CONSOLE_TITLE;
        } else if (command.kind == COMMAND_QUIT) {
            reply.action = CONSOLE_QUIT;
        } else {
            console_puts("error: no world loaded\n", errors);
            reply.failed = true;
        }
        return reply;
    }

    World *world = game_session_world(session);
    RsResult result = RS_OK;
    char message[192] = {0};
    if (state->plot.pending) {
        result = plot_command_answer(world, &state->plot, line, message, sizeof(message));
    } else if (edit_command_parse(line, &edit)) {
        result = edit_command_run(session, &edit,
            (EditCommandContext){.facing=DIR_COUNT}, message, sizeof(message));
    } else if (tick_command_parse(line, &tick)) {
        result = tick_command_run(world, &tick, message, sizeof(message));
        if (result == RS_OK) {
            reply.reset_clock = tick.kind != TICK_COMMAND_NOUPDATE;
            if (tick.kind == TICK_COMMAND_FREEZE) game_session_reset_clock(session);
            if (tick.kind == TICK_COMMAND_STEP) {
                reply.steps = tick.count;
                message[0] = '\0';
            }
        }
    } else if (plot_command_parse(line, &plot)) {
        result = plot_command_run(world, &plot, &state->plot, message, sizeof(message));
    } else {
        char *tokens[TOKEN_CAPACITY];
        int count = tokenize(line, tokens);
        if (!count) return reply;
        Command command;
        if (count < 0 || !parse_command(tokens, count, &command)) {
            console_puts("error: invalid command; use help\n", errors);
            reply.failed = true;
            return reply;
        }
        if (command.kind == COMMAND_SAVE || command.kind == COMMAND_TITLE ||
            command.kind == COMMAND_QUIT) {
            GameSessionResult saved = game_session_save(session);
            if (saved == GAME_SESSION_OK) {
                console_puts("saved\n", output);
                reply.saved = true;
                if (command.kind == COMMAND_TITLE) reply.action = CONSOLE_TITLE;
                if (command.kind == COMMAND_QUIT) reply.action = CONSOLE_QUIT;
            } else {
                console_printf(errors, "error: %s\n", game_session_result_name(saved));
                reply.failed = true;
            }
            return reply;
        }
        result = execute_command(world, &command, output);
    }
    if (result == RS_OK) {
        if (message[0]) console_printf(output, "%s\n", message);
    } else {
        console_printf(errors, "error: %s\n", message[0] ? message : rs_result_name(result));
        reply.failed = true;
        reply.fatal = result == RS_INTERNAL_ERROR || result == RS_OUT_OF_MEMORY;
    }
    return reply;
}
