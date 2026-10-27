#include "rs/structure.h"
#include "rs/region_edit.h"
#include "rs/sim.h"
#include "edit_command.h"
#include "console_command.h"
#include "command_completion.h"
#include "sim_internal.h"
#include "save_file.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#define CHDIR(path) _chdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(path) mkdir(path,0755)
#define CHDIR(path) chdir(path)
#endif

static size_t checks;
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)

static unsigned char *read_bytes(const char *path, size_t *size)
{
    FILE *f=fopen(path,"rb"); CHECK(f);
    CHECK(fseek(f,0,SEEK_END)==0); long length=ftell(f); CHECK(length>=0);
    CHECK(fseek(f,0,SEEK_SET)==0); *size=(size_t)length;
    unsigned char *data=malloc(*size ? *size : 1); CHECK(data);
    CHECK(fread(data,1,*size,f)==*size); CHECK(fclose(f)==0); return data;
}

static void write_bytes(const char *path, const unsigned char *data, size_t size)
{
    FILE *f=fopen(path,"wb"); CHECK(f);
    CHECK(fwrite(data,1,size,f)==size); CHECK(fclose(f)==0);
}

static void same_file(const char *a, const char *b)
{
    size_t an,bn; unsigned char *ad=read_bytes(a,&an), *bd=read_bytes(b,&bn);
    CHECK(an==bn && memcmp(ad,bd,an)==0); free(ad); free(bd);
}

static void roundtrip(const Structure *source, const char *path)
{
    CHECK(structure_write(source,path,true)==STRUCTURE_OK);
    StructureResult result=STRUCTURE_IO_ERROR;
    Structure *copy=structure_read(path,&result); CHECK(copy && result==STRUCTURE_OK);
    StructureInfo a=structure_info(source), b=structure_info(copy);
    CHECK(a.width==b.width && a.height==b.height && a.length==b.length && a.volume==b.volume);
    CHECK(a.container_count==b.container_count && a.source_tick==b.source_tick);
    CHECK(a.origin.x==b.origin.x && a.origin.y==b.origin.y && a.origin.z==b.origin.z);
    CHECK(a.anchor.x==b.anchor.x && a.anchor.y==b.anchor.y && a.anchor.z==b.anchor.z);
    for (uint32_t y=0;y<a.height;++y) for (uint32_t z=0;z<a.length;++z)
        for (uint32_t x=0;x<a.width;++x) {
            BlockPos p={(int)x,(int)y,(int)z}; BlockState s,t; StateId si,ti;
            CHECK(structure_get(source,p,&s) && structure_get(copy,p,&t));
            CHECK(state_encode(s,&si) && state_encode(t,&ti) && si==ti);
            size_t slots=structure_container_size(source,p);
            CHECK(slots==structure_container_size(copy,p));
            for (size_t slot=0;slot<slots;++slot) {
                ContainerStack one,two;
                CHECK(structure_get_container_slot(source,p,slot,&one)==STRUCTURE_OK);
                CHECK(structure_get_container_slot(copy,p,slot,&two)==STRUCTURE_OK);
                CHECK(one.item==two.item && one.count==two.count && one.limit==two.limit);
            }
        }
    CHECK(structure_write(copy,"canonical.mcs",true)==STRUCTURE_OK);
    same_file(path,"canonical.mcs");
    structure_destroy(copy);
}

static void test_states(void)
{
    World *w=world_create((WorldConfig){0}); CHECK(w);
    CHECK(sim_set_updates_enabled(w,false)==RS_OK);
    size_t count=0;
    /* Enumerate valid non-container block states, including dust geometry,
     * every facing/delay/lock combination, and static piston heads. */
    for (unsigned id=0;id<STATE_ID_LIMIT;++id) {
        BlockState state; StateId encoded;
        if (!state_decode((StateId)id,&state) || state.id==BLOCK_MOVING_PISTON ||
            container_is_block((BlockId)state.id) || !state_encode(state,&encoded)) continue;
        BlockPos p={(int)(count%128),1,(int)(count/128)};
        CHECK(world_set_block(w,p,state,UPDATE_CLIENTS)); ++count;
    }
    CHECK(count>1000);
    uint64_t revision=world_revision(w), tick=world_tick(w);
    StructureResult result;
    Structure *s=structure_capture(w,(BlockPos){127,1,(int)((count-1)/128)},
        (BlockPos){0,1,0},NULL,&result);
    CHECK(s && result==STRUCTURE_OK);
    CHECK(world_revision(w)==revision && world_tick(w)==tick && !sim_updates_enabled(w));
    roundtrip(s,"states.mcs"); structure_destroy(s); world_destroy(w);
    printf("Block-state roundtrip: %zu source states\n",count);
}

static void test_capture_and_files(void)
{
    World *w=world_create((WorldConfig){0}); CHECK(w);
    CHECK(sim_set_updates_enabled(w,false)==RS_OK);
    CHECK(world_expand_plot(w,DIR_WEST,1)==RS_OK);
    CHECK(world_expand_plot(w,DIR_NORTH,1)==RS_OK);
    BlockPos positions[]={{-1,-1,-1},{0,0,0},{1,1,1}};
    BlockId types[]={BLOCK_BARREL,BLOCK_HOPPER,BLOCK_DROPPER};
    for (size_t c=0;c<3;++c) {
        CHECK(world_place(w,positions[c],types[c],DIR_EAST)==RS_OK);
        size_t slots=world_container_size(w,positions[c]);
        for (size_t j=0;j<slots;++j) {
            unsigned limit=j%3==0 ? 1 : j%3==1 ? 16 : 64;
            ContainerStack stack={(uint32_t)(UINT32_MAX-j),(uint8_t)(1+j%limit),(uint8_t)limit};
            CHECK(world_set_container_slot(w,positions[c],j,stack)==RS_OK);
        }
    }
    CHECK(sim_set_frozen(w,true)==RS_OK);
    CHECK(sim_request_steps(w,3)==RS_OK);
    CHECK(save_file_write(w,"before.mcw")==GAME_SESSION_OK);
    StructureResult result; BlockPos anchor={INT_MIN,INT_MAX,-123};
    uint64_t revision=world_revision(w);
    Structure *s=structure_capture(w,(BlockPos){1,1,1},(BlockPos){-1,-1,-1},&anchor,&result);
    CHECK(s && result==STRUCTURE_OK && world_revision(w)==revision);
    CHECK(sim_pending_steps(w)==3 && sim_is_frozen(w) && !sim_updates_enabled(w));
    StructureInfo info=structure_info(s);
    CHECK(info.volume==27 && info.container_count==3 && info.origin.x==-1 && info.anchor.x==INT_MIN);
    roundtrip(s,"roundtrip.mcs");
    CHECK(save_file_write(w,"after.mcw")==GAME_SESSION_OK); same_file("before.mcw","after.mcw");
    CHECK(structure_write(s,"roundtrip.mcs",false)==STRUCTURE_EXISTS);
    same_file("roundtrip.mcs","canonical.mcs");
    CHECK(structure_write(s,"missing/none.mcs",true)==STRUCTURE_IO_ERROR);
    write_bytes("roundtrip.mcs.tmp",(const unsigned char *)"keep",4);
    CHECK(structure_write(s,"roundtrip.mcs",true)==STRUCTURE_IO_ERROR);
    size_t temp_size; unsigned char *temp=read_bytes("roundtrip.mcs.tmp",&temp_size);
    CHECK(temp_size==4 && memcmp(temp,"keep",4)==0); free(temp);
    CHECK(remove("roundtrip.mcs.tmp")==0); same_file("roundtrip.mcs","canonical.mcs");
    CHECK(!structure_get(s,(BlockPos){-1,0,0},&(BlockState){0}));
    CHECK(!structure_get(s,(BlockPos){3,0,0},&(BlockState){0}));
    CHECK(structure_get_container_slot(s,(BlockPos){0,0,0},27,&(ContainerStack){0})==STRUCTURE_INVALID_ARGUMENT);
    size_t size; unsigned char *data=read_bytes("roundtrip.mcs",&size);
    for (size_t n=0;n<size;++n) {
        write_bytes("bad.mcs",data,n);
        CHECK(!structure_read("bad.mcs",&result) && result==STRUCTURE_CORRUPT);
    }
    data[72]^=1; write_bytes("bad.mcs",data,size);
    CHECK(!structure_read("bad.mcs",&result) && result==STRUCTURE_CORRUPT);
    data[72]^=1; data[8]=2; write_bytes("bad.mcs",data,size);
    CHECK(!structure_read("bad.mcs",&result) && result==STRUCTURE_UNSUPPORTED);
    free(data); structure_destroy(s);
    CHECK(!structure_capture(NULL,(BlockPos){0},(BlockPos){0},NULL,&result) && result==STRUCTURE_INVALID_ARGUMENT);
    CHECK(!structure_capture(w,(BlockPos){0,320,0},(BlockPos){0},NULL,&result) && result==STRUCTURE_OUT_OF_BOUNDS);
    CHECK(!structure_capture(w,(BlockPos){0,-64,0},(BlockPos){511,319,511},NULL,&result) && result==STRUCTURE_TOO_LARGE);
    CHECK(!structure_read("missing.mcs",&result) && result==STRUCTURE_IO_ERROR);
    world_destroy(w);
    w=world_create((WorldConfig){0}); CHECK(w);
    s=structure_capture(w,(BlockPos){0},(BlockPos){0},NULL,&result); CHECK(s);
    roundtrip(s,"air.mcs"); structure_destroy(s);
    CHECK(sim_set_updates_enabled(w,false)==RS_OK);
    CHECK(world_set_block(w,(BlockPos){0},block_default(BLOCK_MOVING_PISTON),2));
    CHECK(!structure_capture(w,(BlockPos){0},(BlockPos){0},NULL,&result) && result==STRUCTURE_UNSUPPORTED);
    world_destroy(w);
}

static void test_commands(void)
{
    GameSessionResult sr;
    GameSession *session=game_session_create("unused.mcw",(WorldConfig){1},&sr); CHECK(session);
    World *w=game_session_world(session);
    CHECK(world_place(w,(BlockPos){1,1,1},BLOCK_REDSTONE_BLOCK,DIR_DOWN)==RS_OK);
    RegionEditor *editor=game_session_editor(session);
    CHECK(region_editor_select(editor,1,(BlockPos){1,1,1})==RS_OK);
    CHECK(region_editor_select(editor,2,(BlockPos){2,1,1})==RS_OK);
    CHECK(region_editor_copy(editor,NULL)==RS_OK);
    EditCommand command; char message[192];
    CHECK(edit_command_parse("//schem save command_case",&command) && command.kind==EDIT_SCHEM_SAVE);
    uint64_t revision=world_revision(w), tick=world_tick(w);
    CHECK(edit_command_run(session,&command,(EditCommandContext){0},message,sizeof(message))==RS_OK);
    CHECK(world_revision(w)==revision && world_tick(w)==tick);
    CHECK(edit_command_run(session,&command,(EditCommandContext){0},message,sizeof(message))!=RS_OK);
    CHECK(strstr(message,"already exists"));
    CHECK(edit_command_parse("schem save -f command_case.mcs -1,2,3",&command) && command.kind==EDIT_SCHEM_SAVE);
    CHECK(edit_command_run(session,&command,(EditCommandContext){0},message,sizeof(message))==RS_OK);
    StructureResult result; Structure *s=structure_read("structures/command_case.mcs",&result); CHECK(s);
    CHECK(structure_info(s).anchor.x==-1 && structure_info(s).anchor.y==2); structure_destroy(s);
    const char *bad[]={"schem", "schem save", "schem load a", "schem save -f -f a",
        "schem save a 1 2", "schem save a 1 2 3 4"};
    for (size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i)
        CHECK(edit_command_parse(bad[i],&command) && command.kind==EDIT_INVALID);
    CHECK(edit_command_parse("schem save ../outside",&command));
    CHECK(edit_command_run(session,&command,(EditCommandContext){0},message,sizeof(message))==RS_INVALID_ARGUMENT);
    CommandCompletion completion={0}; char line[512]="//sch";
    CHECK(command_complete(&completion,line,sizeof(line),true,false) && !strcmp(line,"//schem"));
    strcpy(line,"//schem s");
    CHECK(command_complete(&completion,line,sizeof(line),true,false) && !strcmp(line,"//schem save"));
    size_t changed;
    CHECK(region_editor_paste(editor,(BlockPos){5,1,1},false,&changed)==RS_OK);
    CHECK(world_get(w,(BlockPos){5,1,1}).id==BLOCK_REDSTONE_BLOCK);
    CHECK(remove("structures/command_case.mcs")==0);
    ConsoleCommandState console={0};
    ConsoleCommandReply reply=console_command_run(session,&console,
        "//schem save console_case",NULL,NULL);
    CHECK(!reply.failed && !reply.fatal && !reply.saved);
    s=structure_read("structures/console_case.mcs",&result); CHECK(s);
    structure_destroy(s); CHECK(remove("structures/console_case.mcs")==0);
    game_session_destroy(session);
}

int main(void)
{
    (void)MKDIR("build"); (void)MKDIR("build/structure-tests");
    CHECK(CHDIR("build/structure-tests")==0);
    (void)remove("structures/command_case.mcs");
    (void)remove("structures/console_case.mcs");
    test_states(); test_capture_and_files(); test_commands();
    printf("Structure tests passed (%zu checks)\n",checks); return 0;
}
