#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "workspace_code.h"
#include "raylib.h"
#define JSMN_STATIC
#include "../../third_party/jsmn.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

enum { CODE_LIMIT=32*1024*1024, CODE_DOCUMENT_LIMIT=64, CODE_READ_BUDGET=256*1024 };
typedef struct { char *data; size_t size,capacity; } CodeBuffer;
typedef struct {
    bool exited,eof,err_eof;
    int status;
#ifdef _WIN32
    HANDLE process,job,input,output,error;
    HANDLE writer;
    char *writing;
    size_t writing_size;
    bool write_ok;
#else
    pid_t pid;
    int input,output,error;
#endif
} CodeProcess;
struct WorkspaceCode {
    CodeProcess process;
    enum { CODE_OFF, CODE_CONFIG, CODE_INITIALIZING, CODE_READY } stage;
    CodeBuffer tx,rx,pending,query;
    char root[4096],failure[4096];
    char *open_uris[CODE_DOCUMENT_LIMIT];
    size_t open_count;
    int next_id,request_id;
    int sync_ids[CODE_DOCUMENT_LIMIT];
    size_t sync_count;
    bool waiting,query_sent,indexing;
    WorkspaceCodeKind kind;
    double deadline,query_at;
    void *source;
    WorkspaceCodeReceiver receiver;
};

static bool append(CodeBuffer *b,const char *text,size_t length)
{
    if (length>CODE_LIMIT-b->size) return false;
    size_t required=b->size+length+1;
    if (required>b->capacity) {
        size_t capacity=b->capacity ? b->capacity : 1024;
        while (capacity<required) capacity*=2;
        char *data=realloc(b->data,capacity);
        if (!data) return false;
        b->data=data; b->capacity=capacity;
    }
    if (length) memcpy(b->data+b->size,text,length);
    b->size+=length; b->data[b->size]='\0';
    return true;
}
static bool text(CodeBuffer *b,const char *value) { return append(b,value,strlen(value)); }
static bool format(CodeBuffer *b,const char *fmt,...)
{
    va_list args; va_start(args,fmt);
    va_list copy; va_copy(copy,args);
    int size=vsnprintf(NULL,0,fmt,copy); va_end(copy);
    char *value=size>=0 ? malloc((size_t)size+1) : NULL;
    if (!value) { va_end(args); return false; }
    vsnprintf(value,(size_t)size+1,fmt,args); va_end(args);
    bool ok=append(b,value,(size_t)size); free(value); return ok;
}
static bool quote(CodeBuffer *b,const char *value)
{
    if (!text(b,"\"")) return false;
    for (const unsigned char *p=(const unsigned char *)value; *p; ++p) {
        if ((*p=='"' || *p=='\\') && !text(b,"\\")) return false;
        if (*p<32) { if (!format(b,"\\u%04x",*p)) return false; }
        else if (!append(b,(const char *)p,1)) return false;
    }
    return text(b,"\"");
}
static void clear_buffer(CodeBuffer *b) { b->size=0; if (b->data) b->data[0]='\0'; }
static bool frame(CodeBuffer *out,const CodeBuffer *json)
{
    return format(out,"Content-Length: %zu\r\n\r\n",json->size) && append(out,json->data,json->size);
}
static char *file_uri(const char *path)
{
    CodeBuffer b={0};
    bool ok=text(&b,path[0]=='/' ? "file://" : "file:///");
    for (const unsigned char *p=(const unsigned char *)path; ok && *p; ++p) {
        unsigned char c=*p;
#ifdef _WIN32
        if (c=='\\') c='/';
#endif
        if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
            c=='-' || c=='_' || c=='.' || c=='~' || c=='/' || c==':')
            ok=append(&b,(const char *)&c,1);
        else ok=format(&b,"%%%02X",c);
    }
    if (!ok) { free(b.data); return NULL; }
    return b.data;
}
static int hex_digit(char c)
{
    if (c>='0' && c<='9') return c-'0';
    c=(char)tolower((unsigned char)c);
    return c>='a' && c<='f' ? c-'a'+10 : -1;
}
static bool uri_path(const char *uri,char path[4096])
{
    if (strncmp(uri,"file://",7)) return false;
    const char *p=uri+7;
    if (!strncmp(p,"localhost/",10)) p+=9;
#ifdef _WIN32
    if (p[0]=='/' && isalpha((unsigned char)p[1]) && p[2]==':') ++p;
#endif
    size_t n=0;
    for (; *p; ++p) {
        int c=(unsigned char)*p;
        if (*p=='%') {
            if (!p[1] || !p[2] || hex_digit(p[1])<0 || hex_digit(p[2])<0) return false;
            c=16*hex_digit(p[1])+hex_digit(p[2]); p+=2;
        }
        if (!c || n+1>=4096) return false;
        path[n++]=(char)c;
    }
    path[n]='\0'; return n!=0;
}

typedef struct { const char *data; jsmntok_t *tokens; int count; } CodeJson;
static int skip(const CodeJson *j,int token)
{
    int next=token+1;
    while (next<j->count && j->tokens[next].start<j->tokens[token].end) ++next;
    return next;
}
static bool equals(const CodeJson *j,int token,const char *value)
{
    return token>=0 && j->tokens[token].end-j->tokens[token].start==(int)strlen(value) &&
        !memcmp(j->data+j->tokens[token].start,value,strlen(value));
}
static int member(const CodeJson *j,int object,const char *name)
{
    if (object<0 || j->tokens[object].type!=JSMN_OBJECT) return -1;
    for (int key=object+1; key+1<j->count && j->tokens[key].start<j->tokens[object].end;) {
        int value=key+1;
        if (equals(j,key,name)) return value;
        key=skip(j,value);
    }
    return -1;
}
static int number(const CodeJson *j,int token)
{
    if (token<0 || j->tokens[token].type!=JSMN_PRIMITIVE) return -1;
    char *end=NULL;
    long value=strtol(j->data+j->tokens[token].start,&end,10);
    if (end!=j->data+j->tokens[token].end || value<0 || value>INT_MAX) return -1;
    return (int)value;
}
static unsigned unicode_escape(const char *p)
{
    unsigned value=0;
    for (int i=0;i<4;++i) { int digit=hex_digit(p[i]); if (digit<0) return UINT_MAX; value=16*value+(unsigned)digit; }
    return value;
}
static char *string_value(const CodeJson *j,int token)
{
    if (token<0 || j->tokens[token].type!=JSMN_STRING) return NULL;
    const char *p=j->data+j->tokens[token].start,*end=j->data+j->tokens[token].end;
    char *result=malloc((size_t)(end-p)+1),*out=result;
    if (!result) return NULL;
    while (p<end) {
        if (*p!='\\') { *out++=*p++; continue; }
        ++p;
        if (p==end) goto fail;
        char c=*p++;
        if (c=='"' || c=='\\' || c=='/') *out++=c;
        else if (c=='n') *out++='\n';
        else if (c=='r') *out++='\r';
        else if (c=='t') *out++='\t';
        else if (c=='b') *out++='\b';
        else if (c=='f') *out++='\f';
        else if (c=='u') {
            if (end-p<4) goto fail;
            unsigned cp=unicode_escape(p); p+=4;
            if (cp==UINT_MAX || !cp) goto fail;
            if (cp>=0xd800 && cp<=0xdbff) {
                if (end-p<6 || p[0]!='\\' || p[1]!='u') goto fail;
                unsigned low=unicode_escape(p+2); p+=6;
                if (low<0xdc00 || low>0xdfff) goto fail;
                cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
            } else if (cp>=0xdc00 && cp<=0xdfff) goto fail;
            if (cp<128) *out++=(char)cp;
            else if (cp<2048) { *out++=(char)(0xc0|(cp>>6)); *out++=(char)(0x80|(cp&63)); }
            else if (cp<65536) { *out++=(char)(0xe0|(cp>>12)); *out++=(char)(0x80|((cp>>6)&63)); *out++=(char)(0x80|(cp&63)); }
            else { *out++=(char)(0xf0|(cp>>18)); *out++=(char)(0x80|((cp>>12)&63)); *out++=(char)(0x80|((cp>>6)&63)); *out++=(char)(0x80|(cp&63)); }
        } else goto fail;
    }
    *out='\0'; return result;
fail:
    free(result); return NULL;
}

#ifdef _WIN32
static char *windows_arguments(const char *const *args)
{
    CodeBuffer b={0};
    for (size_t i=0;args[i];++i) {
        if ((i && !text(&b," ")) || !text(&b,"\"")) goto fail;
        size_t slashes=0;
        for (const char *p=args[i];; ++p) {
            if (*p=='\\') { ++slashes; continue; }
            size_t n=(!*p || *p=='"') ? 2*slashes : slashes;
            while (n--) if (!text(&b,"\\")) goto fail;
            slashes=0;
            if (!*p) break;
            if (*p=='"' && !text(&b,"\\")) goto fail;
            if (!append(&b,p,1)) goto fail;
        }
        if (!text(&b,"\"")) goto fail;
    }
    return b.data;
fail:
    free(b.data); return NULL;
}
static DWORD WINAPI write_worker(void *context)
{
    CodeProcess *p=context;
    size_t offset=0; p->write_ok=true;
    while (offset<p->writing_size) {
        DWORD written=0;
        size_t left=p->writing_size-offset;
        if (!WriteFile(p->input,p->writing+offset,(DWORD)(left>65536 ? 65536 : left),&written,NULL) || !written) {
            p->write_ok=false; break;
        }
        offset+=written;
    }
    return 0;
}
#endif
static void process_init(CodeProcess *p)
{
    memset(p,0,sizeof(*p));
#ifndef _WIN32
    p->input=p->output=p->error=-1;
#endif
}
static void process_stop(CodeProcess *p)
{
#ifdef _WIN32
    if (p->process && !p->exited && !TerminateJobObject(p->job,1)) TerminateProcess(p->process,1);
    if (p->writer) {
        CancelSynchronousIo(p->writer);
        WaitForSingleObject(p->writer,INFINITE); CloseHandle(p->writer);
    }
    free(p->writing);
    if (p->input) CloseHandle(p->input);
    if (p->output) CloseHandle(p->output);
    if (p->error) CloseHandle(p->error);
    if (p->process) CloseHandle(p->process);
    if (p->job) CloseHandle(p->job);
#else
    if (p->pid>0) {
        kill(-p->pid,SIGKILL);
        if (!p->exited) while (waitpid(p->pid,NULL,0)<0 && errno==EINTR) {}
    }
    if (p->input>=0) close(p->input);
    if (p->output>=0) close(p->output);
    if (p->error>=0) close(p->error);
#endif
    process_init(p);
}
static bool process_start(CodeProcess *p,const char *const *args,const char *directory)
{
    process_init(p);
#ifdef _WIN32
    SECURITY_ATTRIBUTES security={sizeof(security),NULL,TRUE};
    HANDLE child_input=NULL,child_output=NULL,child_error=NULL;
    PROCESS_INFORMATION child={0};
    char *line=windows_arguments(args);
    if (!line) goto fail;
    if (!CreatePipe(&child_input,&p->input,&security,65536) ||
        !CreatePipe(&p->output,&child_output,&security,65536) ||
        !CreatePipe(&p->error,&child_error,&security,65536) ||
        !SetHandleInformation(p->input,HANDLE_FLAG_INHERIT,0) ||
        !SetHandleInformation(p->output,HANDLE_FLAG_INHERIT,0) ||
        !SetHandleInformation(p->error,HANDLE_FLAG_INHERIT,0)) goto fail;
    p->job=CreateJobObjectA(NULL,NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!p->job || !SetInformationJobObject(p->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) goto fail;
    STARTUPINFOA startup={0}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=child_input; startup.hStdOutput=child_output; startup.hStdError=child_error;
    if (!CreateProcessA(NULL,line,NULL,NULL,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,directory,&startup,&child)) goto fail;
    p->process=child.hProcess;
    if (!AssignProcessToJobObject(p->job,p->process) || ResumeThread(child.hThread)==(DWORD)-1) {
        TerminateProcess(p->process,1); goto fail;
    }
    CloseHandle(child.hThread); CloseHandle(child_input); CloseHandle(child_output); CloseHandle(child_error);
    free(line); return true;
fail:
    if (child.hThread) CloseHandle(child.hThread);
    if (child_input) CloseHandle(child_input);
    if (child_output) CloseHandle(child_output);
    if (child_error) CloseHandle(child_error);
    free(line); process_stop(p); return false;
#else
    int pipes[3][2]={{-1,-1},{-1,-1},{-1,-1}};
    bool ok=false;
    char **arguments=NULL;
    for (int i=0;i<3;++i) {
        if ((i==0 ? socketpair(AF_UNIX,SOCK_STREAM,0,pipes[i]) : pipe(pipes[i]))<0) goto done;
        for (int j=0;j<2;++j) {
            if (pipes[i][j]<3) {
                int moved=fcntl(pipes[i][j],F_DUPFD,3);
                if (moved<0) goto done;
                close(pipes[i][j]); pipes[i][j]=moved;
            }
            if (fcntl(pipes[i][j],F_SETFD,FD_CLOEXEC)<0) goto done;
        }
    }
#ifdef SO_NOSIGPIPE
    int enabled=1;
    if (setsockopt(pipes[0][1],SOL_SOCKET,SO_NOSIGPIPE,&enabled,sizeof(enabled))<0) goto done;
#endif
    int parents[]={pipes[0][1],pipes[1][0],pipes[2][0]};
    for (int i=0;i<3;++i) {
        int flags=fcntl(parents[i],F_GETFL);
        if (flags<0 || fcntl(parents[i],F_SETFL,flags|O_NONBLOCK)<0) goto done;
    }
    size_t count=0; while (args[count]) ++count;
    arguments=calloc(count+6,sizeof(*arguments));
    if (!arguments) goto done;
    arguments[0]="sh"; arguments[1]="-c";
    arguments[2]="cd \"$1\" && shift && exec \"$@\"";
    arguments[3]="mc-nvim"; arguments[4]=(char *)directory;
    for (size_t i=0;i<count;++i) arguments[i+5]=(char *)args[i];
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int result=posix_spawn_file_actions_init(&actions);
    if (result) goto done;
    result=posix_spawnattr_init(&attributes);
    if (result) { posix_spawn_file_actions_destroy(&actions); goto done; }
    result=posix_spawn_file_actions_adddup2(&actions,pipes[0][0],0);
    if (!result) result=posix_spawn_file_actions_adddup2(&actions,pipes[1][1],1);
    if (!result) result=posix_spawn_file_actions_adddup2(&actions,pipes[2][1],2);
    for (int i=0;i<3 && !result;++i) for (int j=0;j<2 && !result;++j)
        result=posix_spawn_file_actions_addclose(&actions,pipes[i][j]);
    if (!result) result=posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP);
    if (!result) result=posix_spawnattr_setpgroup(&attributes,0);
    if (!result) result=posix_spawn(&p->pid,"/bin/sh",&actions,&attributes,arguments,environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes);
    if (!result) {
        p->input=pipes[0][1]; pipes[0][1]=-1;
        p->output=pipes[1][0]; pipes[1][0]=-1;
        p->error=pipes[2][0]; pipes[2][0]=-1;
        ok=true;
    }
done:
    free(arguments);
    for (int i=0;i<3;++i) for (int j=0;j<2;++j) if (pipes[i][j]>=0) close(pipes[i][j]);
    return ok;
#endif
}
static bool process_write(CodeProcess *p,CodeBuffer *tx)
{
#ifdef _WIN32
    if (p->writer) {
        if (WaitForSingleObject(p->writer,0)!=WAIT_OBJECT_0) return true;
        CloseHandle(p->writer); p->writer=NULL;
        free(p->writing); p->writing=NULL;
        if (!p->write_ok) return false;
    }
    if (!tx->size) return true;
    p->writing=tx->data; p->writing_size=tx->size;
    *tx=(CodeBuffer){0};
    p->writer=CreateThread(NULL,0,write_worker,p,0,NULL);
    if (!p->writer) { free(p->writing); p->writing=NULL; return false; }
    return true;
#else
    if (!tx->size) return true;
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
    size_t size=tx->size>65536 ? 65536 : tx->size;
    ssize_t written=send(p->input,tx->data,size,MSG_NOSIGNAL);
    if (written<0) return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR;
    if (!written) return false;
    tx->size-=(size_t)written;
    memmove(tx->data,tx->data+written,tx->size); tx->data[tx->size]='\0';
    return true;
#endif
}
static int process_read(CodeProcess *p,bool error,char *data,size_t capacity)
{
    bool *eof=error ? &p->err_eof : &p->eof;
    if (*eof) return 0;
#ifdef _WIN32
    HANDLE pipe=error ? p->error : p->output;
    DWORD available=0,read_size=0;
    if (!PeekNamedPipe(pipe,NULL,0,NULL,&available,NULL)) { *eof=true; return 0; }
    if (!available) return 0;
    if (capacity>available) capacity=available;
    if (!ReadFile(pipe,data,(DWORD)capacity,&read_size,NULL) || !read_size) { *eof=true; return 0; }
    return (int)read_size;
#else
    ssize_t size=read(error ? p->error : p->output,data,capacity);
    if (size>0) return (int)size;
    if (!size || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) *eof=true;
    return 0;
#endif
}
static void process_status(CodeProcess *p)
{
    if (p->exited) return;
#ifdef _WIN32
    if (WaitForSingleObject(p->process,0)==WAIT_OBJECT_0) {
        DWORD status=1; GetExitCodeProcess(p->process,&status);
        p->status=(int)status; p->exited=true;
    }
#else
    int status=0;
    pid_t result=waitpid(p->pid,&status,WNOHANG);
    if (result==p->pid) { p->exited=true; p->status=WIFEXITED(status) ? WEXITSTATUS(status) : 1; }
    else if (result<0 && errno!=EINTR) { p->exited=true; p->status=1; }
#endif
}

static void close_documents(WorkspaceCode *code)
{
    for (size_t i=0;i<code->open_count;++i) free(code->open_uris[i]);
    code->open_count=0;
}
static void stop(WorkspaceCode *code)
{
    process_stop(&code->process); code->stage=CODE_OFF;
    clear_buffer(&code->tx); clear_buffer(&code->rx); clear_buffer(&code->pending); clear_buffer(&code->query);
    code->indexing=false; code->query_sent=false; code->sync_count=0;
    close_documents(code);
}
static void deliver(WorkspaceCode *code,const WorkspaceCodeReply *reply)
{
    void *source=code->source;
    WorkspaceCodeReceiver receiver=code->receiver;
    code->waiting=false; code->source=NULL; code->receiver=NULL;
    if (source && receiver) receiver(source,reply);
}
static void fail(WorkspaceCode *code,const char *message)
{
    WorkspaceCodeReply reply={0}; reply.error=true;
    snprintf(reply.text,sizeof(reply.text),"%s",message);
    stop(code); deliver(code,&reply);
}
WorkspaceCode *workspace_code_create(void)
{
    WorkspaceCode *code=calloc(1,sizeof(*code));
    if (code) { process_init(&code->process); code->next_id=2; }
    return code;
}
void workspace_code_destroy(WorkspaceCode *code)
{
    if (!code) return;
    stop(code); free(code->tx.data); free(code->rx.data); free(code->pending.data); free(code->query.data); free(code);
}
void workspace_code_detach(WorkspaceCode *code,void *source)
{
    if (code && code->source==source) {
        code->source=NULL; code->receiver=NULL; code->waiting=false; stop(code);
    }
}
static const char *server_program(const char *root,char output[4096])
{
    const char *program=getenv("MCMUX_CLANGD");
    if (program && *program) return program;
#ifdef _WIN32
    (void)root;
    DWORD size=SearchPathA(NULL,"clangd.exe",NULL,4096,output,NULL);
    if (size && size<4096) return output;
    const char *prefix=getenv("ProgramFiles");
    if (prefix) {
        int count=snprintf(output,4096,"%s/LLVM/bin/clangd.exe",prefix);
        DWORD attributes=count>0 && count<4096 ? GetFileAttributesA(output) : INVALID_FILE_ATTRIBUTES;
        if (attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY)) return output;
    }
#else
    const char *path=getenv("PATH");
    for (const char *part=path; part; ) {
        const char *end=strchr(part,':');
        size_t length=end ? (size_t)(end-part) : strlen(part);
        if (length<3000) {
            int count=part[0]=='/' ? snprintf(output,4096,"%.*s/clangd",(int)length,part) :
                snprintf(output,4096,"%s/%.*s%sclangd",root,(int)length,part,length ? "/" : "");
            struct stat info;
            if (count>0 && count<4096 && !access(output,X_OK) && !stat(output,&info) && S_ISREG(info.st_mode)) return output;
        }
        part=end ? end+1 : NULL;
    }
#ifdef __APPLE__
    const char *locations[]={"/opt/homebrew/opt/llvm/bin/clangd","/usr/local/opt/llvm/bin/clangd"};
    for (size_t i=0;i<sizeof(locations)/sizeof(locations[0]);++i)
        if (!access(locations[i],X_OK)) return locations[i];
#endif
#endif
    return "clangd";
}

static bool initialize_server(WorkspaceCode *code)
{
    char executable[4096];
    const char *program=server_program(code->root,executable);
    char database[4352];
    snprintf(database,sizeof(database),"--compile-commands-dir=%s/build",code->root);
    const char *args[]={program,"--background-index","-j=2","--log=error",database,NULL};
    if (!process_start(&code->process,args,code->root)) return false;
    CodeBuffer json={0}; char *uri=file_uri(code->root);
    bool ok=uri && text(&json,"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":") &&
        quote(&json,uri) && text(&json,",\"capabilities\":{\"window\":{\"workDoneProgress\":true},\"general\":{\"positionEncodings\":[\"utf-16\"]},\"textDocument\":{\"hover\":{\"contentFormat\":[\"plaintext\"]},\"definition\":{\"linkSupport\":true},\"declaration\":{\"linkSupport\":true}}}}}") && frame(&code->tx,&json);
    free(uri); free(json.data);
    code->stage=CODE_INITIALIZING; code->deadline=GetTime()+45;
    return ok;
}
static bool hover_text(const CodeJson *j,int token,CodeBuffer *out)
{
    if (token<0) return true;
    if (j->tokens[token].type==JSMN_ARRAY) {
        for (int i=token+1;i<j->count && j->tokens[i].start<j->tokens[token].end;i=skip(j,i))
            if (!hover_text(j,i,out)) return false;
        return true;
    }
    if (j->tokens[token].type==JSMN_OBJECT) token=member(j,token,"value");
    char *value=string_value(j,token);
    if (!value) return true;
    bool ok=(!out->size || text(out,"\n\n")) && text(out,value);
    free(value); return ok;
}
static bool consume_sync(WorkspaceCode *code,int id)
{
    for (size_t i=0;i<code->sync_count;++i) if (code->sync_ids[i]==id) {
        code->sync_ids[i]=code->sync_ids[--code->sync_count];
        return true;
    }
    return false;
}

static void response(WorkspaceCode *code,const char *data,size_t length)
{
    jsmntok_t *tokens=NULL;
    int count=JSMN_ERROR_NOMEM,capacity=128;
    while (capacity<=262144) {
        jsmntok_t *next=realloc(tokens,(size_t)capacity*sizeof(*tokens));
        if (!next) { free(tokens); fail(code,"Cannot read code lookup response: out of memory"); return; }
        tokens=next;
        jsmn_parser parser; jsmn_init(&parser);
        count=jsmn_parse(&parser,data,length,tokens,(unsigned)capacity);
        if (count!=JSMN_ERROR_NOMEM) break;
        capacity*=2;
    }
    if (count<1 || tokens[0].type!=JSMN_OBJECT) { free(tokens); fail(code,"clangd returned an invalid response"); return; }
    CodeJson j={data,tokens,count};
    int id=number(&j,member(&j,0,"id"));
    int method=member(&j,0,"method"),error=member(&j,0,"error"),result=member(&j,0,"result");
    if (method>=0) {
        if (equals(&j,method,"$/progress")) {
            int params=member(&j,0,"params"),token=member(&j,params,"token");
            if (equals(&j,token,"backgroundIndexProgress")) {
                int value=member(&j,params,"value"),kind=member(&j,value,"kind");
                if (equals(&j,kind,"begin")) code->indexing=true;
                if (equals(&j,kind,"end")) code->indexing=false;
            }
        }
        /* clangd may ask to create a progress token; no UI registration is needed. */
        int identifier=member(&j,0,"id");
        if (identifier>=0) {
            CodeBuffer json={0};
            bool ok=text(&json,"{\"jsonrpc\":\"2.0\",\"id\":");
            if (j.tokens[identifier].type==JSMN_STRING) {
                char *value=string_value(&j,identifier);
                ok=ok && value && quote(&json,value); free(value);
            } else ok=ok && id>=0 && format(&json,"%d",id);
            ok=ok && text(&json,equals(&j,method,"window/workDoneProgress/create") ?
                ",\"result\":null}" : ",\"error\":{\"code\":-32601,\"message\":\"Unsupported client request\"}}") && frame(&code->tx,&json);
            if (!ok)
                fail(code,"Cannot answer clangd request");
            free(json.data);
        }
    } else if (id==1 && code->stage==CODE_INITIALIZING) {
        if (error>=0 || result<0) fail(code,"clangd could not initialize");
        else {
            CodeBuffer json={0};
            bool ok=text(&json,"{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}") &&
                frame(&code->tx,&json) && append(&code->tx,code->pending.data,code->pending.size);
            free(json.data); clear_buffer(&code->pending);
            if (!ok) fail(code,"Code lookup request is too large");
            else {
                code->stage=CODE_READY; code->deadline=GetTime()+45;
                code->query_at=GetTime()+0.25;
            }
        }
    } else if (code->waiting && consume_sync(code,id)) {
        /* Waiting for documentSymbol ensures each unsaved document's AST and
         * dynamic index are updated before a cross-file lookup is issued. */
    } else if (code->waiting && id==code->request_id) {
        WorkspaceCodeReply reply={0};
        if (error>=0) {
            char *message=string_value(&j,member(&j,error,"message"));
            snprintf(reply.text,sizeof(reply.text),"%s",message ? message : "Code lookup failed");
            free(message); reply.error=true;
        } else if (code->kind==CODE_HOVER) {
            CodeBuffer content={0};
            bool ok=hover_text(&j,member(&j,result,"contents"),&content);
            if (ok && content.size) snprintf(reply.text,sizeof(reply.text),"%s",content.data);
            else { snprintf(reply.text,sizeof(reply.text),"No information for this symbol"); reply.error=true; }
            free(content.data);
        } else {
            int location=result;
            if (location>=0 && tokens[location].type==JSMN_ARRAY)
                location=location+1<count && tokens[location+1].start<tokens[location].end ? location+1 : -1;
            int uri=member(&j,location,"uri"),range=member(&j,location,"range");
            if (uri<0) { uri=member(&j,location,"targetUri"); range=member(&j,location,"targetSelectionRange"); }
            int start=member(&j,range,"start");
            int line=number(&j,member(&j,start,"line")),column=number(&j,member(&j,start,"character"));
            char *path=string_value(&j,uri);
            if (path && line>=0 && column>=0 && uri_path(path,reply.path)) {
                reply.line=(size_t)line; reply.column=(size_t)column;
            } else {
                snprintf(reply.text,sizeof(reply.text),"No %s found for this symbol",code->kind==CODE_DEFINITION ? "definition" : "declaration");
                reply.error=true;
            }
            free(path);
        }
        deliver(code,&reply);
    }
    free(tokens);
}
static bool read_frames(WorkspaceCode *code)
{
    for (;;) {
        char *end=code->rx.data ? strstr(code->rx.data,"\r\n\r\n") : NULL;
        if (!end) return code->rx.size<8192;
        size_t header=(size_t)(end-code->rx.data)+4,body=0;
        const char *length=strstr(code->rx.data,"Content-Length:");
        if (!length || length>=end) return false;
        length+=15; while (*length==' ' || *length=='\t') ++length;
        if (!isdigit((unsigned char)*length)) return false;
        for (;isdigit((unsigned char)*length);++length) {
            if (body>(CODE_LIMIT-(unsigned)(*length-'0'))/10) return false;
            body=body*10+(unsigned)(*length-'0');
        }
        if (body>CODE_LIMIT || code->rx.size<header+body) return body<=CODE_LIMIT;
        response(code,code->rx.data+header,body);
        if (code->stage==CODE_OFF) return true;
        code->rx.size-=header+body;
        memmove(code->rx.data,code->rx.data+header+body,code->rx.size);
        code->rx.data[code->rx.size]='\0';
    }
}
void workspace_code_poll(WorkspaceCode *code)
{
    if (!code || code->stage==CODE_OFF) return;
    if (code->stage!=CODE_CONFIG && !process_write(&code->process,&code->tx)) {
        fail(code,"clangd stopped accepting requests; repeat the lookup to restart it"); return;
    }
    size_t budget=CODE_READ_BUDGET;
    while (budget) {
        char data[4096];
        int size=process_read(&code->process,false,data,budget<sizeof(data) ? budget : sizeof(data));
        if (!size) break;
        budget-=(size_t)size;
        if (code->stage!=CODE_CONFIG && !append(&code->rx,data,(size_t)size)) { fail(code,"Code lookup response is too large"); return; }
    }
    budget=CODE_READ_BUDGET;
    while (budget) {
        char data[1024];
        int size=process_read(&code->process,true,data,budget<sizeof(data)-1 ? budget : sizeof(data)-1);
        if (!size) break;
        budget-=(size_t)size; data[size]='\0';
        size_t used=strlen(code->failure),amount=(size_t)size;
        if (amount>=sizeof(code->failure)) amount=sizeof(code->failure)-1;
        if (used+amount>=sizeof(code->failure)) {
            size_t remove=used+amount-sizeof(code->failure)+1;
            memmove(code->failure,code->failure+remove,used-remove+1); used-=remove;
        }
        memcpy(code->failure+used,data,amount); code->failure[used+amount]='\0';
    }
    process_status(&code->process);
    if (code->stage!=CODE_CONFIG && !read_frames(code)) { fail(code,"Invalid clangd message framing"); return; }
    if (code->stage==CODE_READY && code->waiting && !code->query_sent && !code->sync_count &&
        !code->indexing && GetTime()>=code->query_at) {
        if (!append(&code->tx,code->query.data,code->query.size)) { fail(code,"Code lookup request is too large"); return; }
        clear_buffer(&code->query); code->query_sent=true;
    }
    if (code->stage==CODE_CONFIG && code->process.exited && code->process.eof && code->process.err_eof) {
        int status=code->process.status; process_stop(&code->process);
        if (status) { fail(code,code->failure[0] ? code->failure : "Could not prepare project compilation information"); return; }
        code->failure[0]='\0';
        if (!initialize_server(code)) fail(code,"Cannot start clangd; install clangd or set MCMUX_CLANGD to its executable");
    } else if (code->stage!=CODE_OFF && code->process.exited && code->process.eof && code->process.err_eof) {
        char message[4608];
        snprintf(message,sizeof(message),"clangd exited. Install clangd or set MCMUX_CLANGD. %s",code->failure);
        fail(code,message);
    } else if (code->waiting && GetTime()>code->deadline) {
        fail(code,"Code lookup timed out; repeat the lookup to restart clangd");
    }
}
bool workspace_code_query(WorkspaceCode *code,const char *root,
    const WorkspaceCodeDocument *documents,size_t count,size_t active,
    size_t line,size_t column,WorkspaceCodeKind kind,void *source,
    WorkspaceCodeReceiver receiver,char *error,size_t capacity)
{
    if (!code || !root || !*root || !documents || !count || count>CODE_DOCUMENT_LIMIT || active>=count || strlen(root)>=sizeof(code->root)) {
        snprintf(error,capacity,"Code lookup is unavailable for this file"); return false;
    }
    if (code->waiting) { snprintf(error,capacity,"A code lookup is still running"); return false; }
    if (strcmp(code->root,root)) stop(code);
    snprintf(code->root,sizeof(code->root),"%s",root);
    clear_buffer(&code->pending);
    clear_buffer(&code->query); code->query_sent=false;
    code->sync_count=0;
    if (code->next_id>INT_MAX-CODE_DOCUMENT_LIMIT-2) code->next_id=2;
    bool ok=true;
    for (size_t i=0;i<code->open_count && ok;++i) {
        CodeBuffer json={0};
        ok=text(&json,"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":") &&
            quote(&json,code->open_uris[i]) && text(&json,"}}}") && frame(&code->pending,&json);
        free(json.data);
    }
    close_documents(code);
    for (size_t i=0;i<count && ok;++i) {
        if (!documents[i].path || !documents[i].text) { ok=false; break; }
        char *uri=file_uri(documents[i].path);
        if (!uri) { ok=false; break; }
        code->open_uris[code->open_count++]=uri;
        CodeBuffer json={0};
        ok=text(&json,"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":") &&
            quote(&json,uri) && text(&json,",\"languageId\":\"c\",\"version\":1,\"text\":") && quote(&json,documents[i].text) &&
            text(&json,"}}}") && frame(&code->pending,&json);
        free(json.data);
        if (ok) {
            int id=code->next_id++;
            CodeBuffer sync={0};
            ok=format(&sync,"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/documentSymbol\",\"params\":{\"textDocument\":{\"uri\":",id) &&
                quote(&sync,uri) && text(&sync,"}}}") && frame(&code->pending,&sync);
            free(sync.data);
            if (ok) code->sync_ids[code->sync_count++]=id;
        }
    }
    code->request_id=code->next_id++;
    const char *method=kind==CODE_HOVER ? "textDocument/hover" :
        kind==CODE_DEFINITION ? "textDocument/definition" : "textDocument/declaration";
    CodeBuffer json={0};
    ok=ok && format(&json,"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":",code->request_id) && quote(&json,method) &&
        text(&json,",\"params\":{\"textDocument\":{\"uri\":") && quote(&json,code->open_uris[active]) &&
        format(&json,"},\"position\":{\"line\":%zu,\"character\":%zu}}}",line,column) && frame(&code->query,&json);
    free(json.data);
    if (!ok) { stop(code); snprintf(error,capacity,"Code lookup request is too large or memory is unavailable"); return false; }
    code->source=source; code->receiver=receiver; code->kind=kind;
    code->waiting=true; code->deadline=GetTime()+45;
    if (code->stage==CODE_READY) {
        if (!append(&code->tx,code->pending.data,code->pending.size)) {
            stop(code); code->waiting=false;
            snprintf(error,capacity,"Code lookup request is too large"); return false;
        }
        clear_buffer(&code->pending);
        code->query_at=GetTime();
    } else {
        char tool[4352];
#ifdef _WIN32
        snprintf(tool,sizeof(tool),"%s/nob.exe",root);
#else
        snprintf(tool,sizeof(tool),"%s/nob",root);
#endif
        const char *args[]={tool,"compdb",documents[active].path,NULL};
        code->failure[0]='\0';
        if (!process_start(&code->process,args,root)) {
            stop(code); code->waiting=false;
            snprintf(error,capacity,"Cannot start the project build tool to prepare code navigation"); return false;
        }
        code->stage=CODE_CONFIG;
    }
    return true;
}
