#include "rs/region_edit.h"
#include "sim_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Bound synchronous edit memory/work, including air and repeated copies. */
enum { EDIT_BLOCK_LIMIT = 4*1024*1024 };
typedef struct { size_t cell; ContainerStack slots[27]; } StoredContainer;
typedef struct {
    RegionBounds bounds;
    BlockPos anchor;
    BlockState *cells;
    StoredContainer *containers;
    size_t container_count, container_capacity;
} Snapshot;
typedef struct {
    BlockPos pos;
    BlockState before, after;
    const StoredContainer *container;
} EditCell;
typedef struct { EditCell *cells; size_t count, capacity; PosMap positions; } EditPlan;
struct RegionEditor {
    World *world;
    BlockPos first, second;
    unsigned selected;
    Snapshot clipboard;
    const char *error;
};

static RsResult fail(RegionEditor *e, RsResult result, const char *message)
{
    if (e) e->error = message;
    return result;
}

static RsResult ready(RegionEditor *e)
{
    if (!e || !e->world) return RS_INVALID_ARGUMENT;
    e->error = "";
    if (e->world->sim->handling_tick || e->world->sim->block_entities.ticking)
        return fail(e, RS_INVALID_ARGUMENT, "Cannot edit during a simulation callback");
    return sim_flush(e->world);
}

static void snapshot_destroy(Snapshot *s)
{
    free(s->cells); free(s->containers); *s = (Snapshot){0};
}

RegionEditor *region_editor_create(World *world)
{
    if (!world) return NULL;
    RegionEditor *e = calloc(1, sizeof(*e));
    if (e) { e->world = world; e->error = ""; }
    return e;
}

void region_editor_destroy(RegionEditor *e)
{
    if (e) { snapshot_destroy(&e->clipboard); free(e); }
}

const char *region_editor_error(const RegionEditor *e)
{
    return e && e->error ? e->error : "";
}

RsResult region_editor_select(RegionEditor *e, unsigned point, BlockPos pos)
{
    if (!e || (point != 1 && point != 2)) return RS_INVALID_ARGUMENT;
    e->error = "";
    if (!world_in_bounds(e->world, pos))
        return fail(e, RS_OUT_OF_BOUNDS, "Selection point is outside the world");
    if (point == 1) e->first = pos; else e->second = pos;
    e->selected |= point;
    return RS_OK;
}

unsigned region_editor_selection(const RegionEditor *e, BlockPos *first, BlockPos *second)
{
    if (!e) return 0;
    if (first) *first = e->first;
    if (second) *second = e->second;
    return e->selected;
}

bool region_editor_bounds(const RegionEditor *e, RegionBounds *b)
{
    if (!e || !b || e->selected != 3) return false;
#define LOW(a) (e->first.a < e->second.a ? e->first.a : e->second.a)
#define HIGH(a) (e->first.a > e->second.a ? e->first.a : e->second.a)
    *b = (RegionBounds){.min={LOW(x),LOW(y),LOW(z)}, .max={HIGH(x),HIGH(y),HIGH(z)}};
#undef LOW
#undef HIGH
    uint64_t x = (uint64_t)((int64_t)b->max.x - b->min.x + 1);
    uint64_t y = (uint64_t)((int64_t)b->max.y - b->min.y + 1);
    uint64_t z = (uint64_t)((int64_t)b->max.z - b->min.z + 1);
    if (x > SIZE_MAX/y || x*y > SIZE_MAX/z) b->volume = SIZE_MAX;
    else b->volume = (size_t)(x*y*z);
    return true;
}

static RsResult selected_bounds(RegionEditor *e, RegionBounds *b)
{
    RsResult result = ready(e);
    if (result != RS_OK) return result;
    if (!region_editor_bounds(e,b))
        return fail(e, RS_INVALID_ARGUMENT, "Set both pos1 and pos2 first");
    if (!world_in_bounds(e->world,b->min) || !world_in_bounds(e->world,b->max))
        return fail(e, RS_OUT_OF_BOUNDS, "Selection is outside the current world");
    if (b->volume > EDIT_BLOCK_LIMIT)
        return fail(e, RS_INVALID_ARGUMENT, "Selection exceeds the 4194304-block edit limit");
    return RS_OK;
}

static BlockPos cell_pos(RegionBounds b, size_t index)
{
    size_t x = (size_t)((int64_t)b.max.x-b.min.x+1);
    size_t z = (size_t)((int64_t)b.max.z-b.min.z+1);
    return (BlockPos){(int)((int64_t)b.min.x + (int64_t)(index%x)),
        (int)((int64_t)b.min.y + (int64_t)(index/(x*z))),
        (int)((int64_t)b.min.z + (int64_t)((index/x)%z))};
}

static RsResult snapshot_read(RegionEditor *e, RegionBounds bounds, Snapshot *s)
{
    *s = (Snapshot){.bounds=bounds, .anchor=bounds.min};
    s->cells = malloc(bounds.volume*sizeof(*s->cells));
    if (!s->cells) return RS_OUT_OF_MEMORY;
    for (size_t i=0; i<bounds.volume; ++i) {
        BlockPos p = cell_pos(bounds,i);
        BlockState state = s->cells[i] = world_get(e->world,p);
        if (e->world->sim->error != RS_OK) return e->world->sim->error;
        if (state.id == BLOCK_MOVING_PISTON)
            return fail(e, RS_UNSUPPORTED, "Finish piston motion before copying or moving this region");
        size_t slots = container_slot_count((BlockId)state.id);
        if (!slots) continue;
        if (s->container_count == s->container_capacity) {
            size_t capacity = s->container_capacity ? s->container_capacity*2 : 8;
            if (capacity > SIZE_MAX/sizeof(*s->containers)) return RS_OUT_OF_MEMORY;
            StoredContainer *next = realloc(s->containers,capacity*sizeof(*next));
            if (!next) return RS_OUT_OF_MEMORY;
            s->containers = next; s->container_capacity = capacity;
        }
        StoredContainer *c = &s->containers[s->container_count++];
        memset(c,0,sizeof(*c)); c->cell = i;
        for (size_t slot=0; slot<slots; ++slot)
            if (world_get_container_slot(e->world,p,slot,&c->slots[slot]) != RS_OK)
                return RS_INTERNAL_ERROR;
    }
    return RS_OK;
}

static const StoredContainer *snapshot_container(const Snapshot *s, size_t cell)
{
    size_t lo=0, hi=s->container_count;
    while (lo<hi) {
        size_t mid=lo+(hi-lo)/2;
        if (s->containers[mid].cell < cell) lo=mid+1; else hi=mid;
    }
    return lo<s->container_count && s->containers[lo].cell == cell ? &s->containers[lo] : NULL;
}

RsResult region_editor_copy(RegionEditor *e, const BlockPos *anchor)
{
    RegionBounds b;
    RsResult result = selected_bounds(e,&b);
    if (result != RS_OK) return result;
    Snapshot next;
    result = snapshot_read(e,b,&next);
    if (result != RS_OK) { snapshot_destroy(&next); return result; }
    if (anchor) next.anchor = *anchor;
    snapshot_destroy(&e->clipboard); e->clipboard = next;
    return RS_OK;
}

static bool translated(World *w, RegionBounds b, int64_t x, int64_t y, int64_t z,
                        RegionBounds *out)
{
    int64_t low[3]={(int64_t)b.min.x+x,(int64_t)b.min.y+y,(int64_t)b.min.z+z};
    int64_t high[3]={(int64_t)b.max.x+x,(int64_t)b.max.y+y,(int64_t)b.max.z+z};
    for (unsigned i=0;i<3;++i)
        if (low[i]<INT_MIN || high[i]>INT_MAX) return false;
    *out=(RegionBounds){.min={(int)low[0],(int)low[1],(int)low[2]},
        .max={(int)high[0],(int)high[1],(int)high[2]},.volume=b.volume};
    return world_in_bounds(w,out->min) && world_in_bounds(w,out->max);
}

static bool inside(RegionBounds b, BlockPos p)
{
    return p.x>=b.min.x && p.x<=b.max.x && p.y>=b.min.y && p.y<=b.max.y &&
           p.z>=b.min.z && p.z<=b.max.z;
}

static bool same_contents(const ContainerRuntime *runtime, const StoredContainer *copy)
{
    if (!runtime || !copy) return !runtime && !copy;
    for (size_t i=0; i<container_slot_count((BlockId)runtime->id); ++i) {
        ContainerStack a=runtime->slots[i], b=copy->slots[i];
        if (a.item != b.item || a.count != b.count || a.limit != b.limit) return false;
    }
    return true;
}

static RsResult plan_cell(RegionEditor *e, EditPlan *plan, BlockPos p,
                          BlockState after, const StoredContainer *contents)
{
    BlockState before=world_get(e->world,p);
    if (e->world->sim->error != RS_OK) return e->world->sim->error;
    if (before.id == BLOCK_MOVING_PISTON)
        return fail(e,RS_UNSUPPORTED,"Finish piston motion before editing the destination");
    StateId a,b;
    if (!state_encode(before,&a) || !state_encode(after,&b)) return RS_INTERNAL_ERROR;
    if (a==b && same_contents(container_runtime(e->world,p),contents)) return RS_OK;
    if (plan->count == plan->capacity) {
        size_t capacity=plan->capacity ? plan->capacity*2 : 64;
        if (capacity>SIZE_MAX/sizeof(*plan->cells)) return RS_OUT_OF_MEMORY;
        EditCell *next=realloc(plan->cells,capacity*sizeof(*next));
        if (!next) return RS_OUT_OF_MEMORY;
        plan->cells=next; plan->capacity=capacity;
    }
    if (!posmap_set(&plan->positions,p,plan->count)) return RS_OUT_OF_MEMORY;
    plan->cells[plan->count++]=(EditCell){p,before,after,contents};
    return RS_OK;
}

static RsResult plan_snapshot(RegionEditor *e, EditPlan *p, const Snapshot *s,
                              RegionBounds target, bool skip_air)
{
    for (size_t i=0; i<s->bounds.volume; ++i) {
        if (skip_air && s->cells[i].id==BLOCK_AIR) continue;
        const StoredContainer *c=container_is_block((BlockId)s->cells[i].id) ? snapshot_container(s,i) : NULL;
        RsResult result=plan_cell(e,p,cell_pos(target,i),s->cells[i],c);
        if (result!=RS_OK) return result;
    }
    return RS_OK;
}

static void plan_destroy(EditPlan *p)
{
    free(p->cells); posmap_destroy(&p->positions);
}

static RsResult apply_plan(RegionEditor *e, EditPlan *plan, size_t *changed)
{
    World *w=e->world;
    sim_forget_edit_cells(w,&plan->positions);
    for (size_t i=0; i<plan->count && w->sim->error==RS_OK; ++i) {
        EditCell c=plan->cells[i];
        sim_forget_cell(w,c.pos);
        if (!world_write_visual(w,c.pos,c.after)) break;
        if (c.after.id==BLOCK_REDSTONE_TORCH && !sim_attach_torch(w,c.pos))
            w->sim->error=RS_OUT_OF_MEMORY;
        if (c.container) {
            if (!container_attach(w,c.pos,c.after)) { w->sim->error=RS_OUT_OF_MEMORY; break; }
            ContainerRuntime *runtime=container_runtime(w,c.pos);
            memcpy(runtime->slots,c.container->slots,
                container_slot_count((BlockId)c.after.id)*sizeof(*runtime->slots));
            ++w->revision;
        }
    }
    /* Every cell and inventory is installed before callbacks see the edit. */
    for (size_t i=0; i<plan->count && w->sim->error==RS_OK; ++i) {
        EditCell c=plan->cells[i];
        BlockState current=world_get(w,c.pos);
        if (current.id != c.after.id) continue;
        sim_edit_state_changed(w,(StateChange){c.pos,c.before,current,3,UPDATE_SHAPE_LIMIT});
    }
    if (changed) *changed=plan->count;
    return sim_flush(w);
}

RsResult region_editor_paste(RegionEditor *e, BlockPos anchor, bool skip_air, size_t *changed)
{
    if (changed) *changed=0;
    RsResult result=ready(e);
    if (result!=RS_OK) return result;
    Snapshot *s=&e->clipboard;
    if (!s->cells) return fail(e,RS_INVALID_ARGUMENT,"Clipboard is empty; use copy first");
    RegionBounds target;
    if (!translated(e->world,s->bounds,(int64_t)anchor.x-s->anchor.x,
        (int64_t)anchor.y-s->anchor.y,(int64_t)anchor.z-s->anchor.z,&target))
        return fail(e,RS_OUT_OF_BOUNDS,"Paste would extend outside the world");
    EditPlan plan={0};
    result=plan_snapshot(e,&plan,s,target,skip_air);
    if (result==RS_OK) result=apply_plan(e,&plan,changed);
    plan_destroy(&plan); return result;
}

static RsResult repeat_or_move(RegionEditor *e, uint32_t amount, Direction direction,
                               bool move, size_t *changed)
{
    if (changed) *changed=0;
    RegionBounds bounds;
    RsResult result=selected_bounds(e,&bounds);
    if (result!=RS_OK) return result;
    if (!amount || (unsigned)direction>=DIR_COUNT) return RS_INVALID_ARGUMENT;
    uint64_t writes=move ? (uint64_t)bounds.volume*2 : (uint64_t)bounds.volume*amount;
    if (writes>EDIT_BLOCK_LIMIT)
        return fail(e,RS_INVALID_ARGUMENT,"Operation exceeds the 4194304-block edit limit");
    BlockPos axis=block_offset((BlockPos){0},direction);
    int64_t stride=move ? 1 : axis.x ? (int64_t)bounds.max.x-bounds.min.x+1 :
        axis.y ? (int64_t)bounds.max.y-bounds.min.y+1 : (int64_t)bounds.max.z-bounds.min.z+1;
    int64_t delta=stride*amount;
    RegionBounds last;
    if (!translated(e->world,bounds,axis.x*delta,axis.y*delta,axis.z*delta,&last))
        return fail(e,RS_OUT_OF_BOUNDS,"Operation would extend outside the world");
    Snapshot s;
    result=snapshot_read(e,bounds,&s);
    EditPlan plan={0};
    if (result==RS_OK && move)
        for (size_t i=0; i<bounds.volume && result==RS_OK; ++i) {
            BlockPos p=cell_pos(bounds,i);
            if (!inside(last,p)) result=plan_cell(e,&plan,p,block_default(BLOCK_AIR),NULL);
        }
    uint32_t copies=move ? 1 : amount;
    for (uint32_t n=1; n<=copies && result==RS_OK; ++n) {
        int64_t step=move ? delta : stride*n;
        RegionBounds target;
        if (!translated(e->world,bounds,axis.x*step,axis.y*step,axis.z*step,&target)) {
            result=RS_OUT_OF_BOUNDS; break;
        }
        result=plan_snapshot(e,&plan,&s,target,false);
    }
    if (result==RS_OK) result=apply_plan(e,&plan,changed);
    plan_destroy(&plan); snapshot_destroy(&s); return result;
}

RsResult region_editor_stack(RegionEditor *e, uint32_t copies, Direction direction, size_t *changed)
{
    return repeat_or_move(e,copies,direction,false,changed);
}

RsResult region_editor_move(RegionEditor *e, uint32_t distance, Direction direction, size_t *changed)
{
    return repeat_or_move(e,distance,direction,true,changed);
}
