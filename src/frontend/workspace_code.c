#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "workspace_code.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <dirent.h>
#endif

/* A lexical C function index, deliberately independent of the build tool and
 * compiler. It does not expand macros or select conditional-compilation arms.
 * Every lookup rescans files, with open buffers taking precedence over disk. */
enum {
    CODE_PATH=4096, CODE_FILE_LIMIT=4*1024*1024,
    CODE_TOTAL_LIMIT=64*1024*1024, CODE_DOCUMENT_LIMIT=64,
    CODE_TASK_LIMIT=16384, CODE_HIT_LIMIT=128, CODE_TOKEN_LIMIT=1024*1024,
    CODE_NEST_LIMIT=256, CODE_FILES_PER_POLL=4
};
#define CODE_NONE UINT32_MAX
typedef struct { uint32_t start,end,pair; unsigned char kind; } CodeToken;
typedef struct { CodeToken *items; size_t count,capacity; } CodeTokens;
typedef struct { char *path,*text; } CodeDocument;
typedef struct { char *path; bool directory; } CodeTask;
typedef struct {
    char path[CODE_PATH],signature[2048],comment[2048];
    size_t line,column,parameters;
    bool definition,local,unspecified,variadic;
} CodeHit;
struct WorkspaceCode {
    CodeDocument documents[CODE_DOCUMENT_LIMIT];
    size_t document_count,active,document_next,bytes;
    CodeTask *tasks;
    size_t task_count,task_capacity,task_next;
    CodeHit *hits;
    size_t hit_count,hit_capacity;
    char name[256],root[CODE_PATH];
    bool waiting;
    WorkspaceCodeKind kind;
    void *source;
    WorkspaceCodeReceiver receiver;
};

static char *code_copy(const char *value)
{
    size_t length=strlen(value);
    char *copy=malloc(length+1);
    if (copy) memcpy(copy,value,length+1);
    return copy;
}

static bool code_path_equal(const char *a,const char *b)
{
    for (; *a && *b; ++a,++b) {
        unsigned char x=(unsigned char)*a,y=(unsigned char)*b;
#ifdef _WIN32
        if (x=='\\') x='/';
        if (y=='\\') y='/';
        x=(unsigned char)tolower(x); y=(unsigned char)tolower(y);
#endif
        if (x!=y) return false;
    }
    return *a==*b;
}

static bool code_join(const char *parent,const char *name,char result[CODE_PATH])
{
    size_t length=strlen(parent);
    int count=snprintf(result,CODE_PATH,"%s%s%s",parent,
        length && (parent[length-1]=='/' || parent[length-1]=='\\') ? "" : "/",name);
    return count>=0 && count<CODE_PATH;
}

static bool code_extension(const char *path)
{
    size_t length=strlen(path);
    return length>2 && path[length-2]=='.' &&
        (tolower((unsigned char)path[length-1])=='c' ||
         tolower((unsigned char)path[length-1])=='h');
}

static void code_reset(WorkspaceCode *code)
{
    for (size_t i=0;i<code->document_count;++i) {
        free(code->documents[i].path); free(code->documents[i].text);
    }
    for (size_t i=0;i<code->task_count;++i) free(code->tasks[i].path);
    free(code->tasks); free(code->hits);
    memset(code,0,sizeof(*code));
}

static void code_deliver(WorkspaceCode *code,const WorkspaceCodeReply *reply)
{
    WorkspaceCodeReceiver receiver=code->receiver;
    void *source=code->source;
    code_reset(code);
    if (receiver) receiver(source,reply);
}

static void code_fail(WorkspaceCode *code,const char *message,const char *path)
{
    WorkspaceCodeReply reply={.error=true};
    snprintf(reply.text,sizeof(reply.text),"%s%s%s",message,path ? ": " : "",path ? path : "");
    code_deliver(code,&reply);
}

static bool code_task(WorkspaceCode *code,const char *path,bool directory)
{
    if (code->task_count==CODE_TASK_LIMIT) return false;
    if (code->task_count==code->task_capacity) {
        size_t capacity=code->task_capacity ? code->task_capacity*2 : 128;
        CodeTask *items=realloc(code->tasks,capacity*sizeof(*items));
        if (!items) return false;
        code->tasks=items; code->task_capacity=capacity;
    }
    char *copy=code_copy(path);
    if (!copy) return false;
    code->tasks[code->task_count++]=(CodeTask){copy,directory};
    return true;
}

static bool code_skip_directory(const char *name)
{
    return name[0]=='.' || !strcmp(name,"build");
}

static bool code_directory(WorkspaceCode *code,const char *path)
{
    bool ok=true;
    char child[CODE_PATH];
#ifdef _WIN32
    char pattern[CODE_PATH];
    if (!code_join(path,"*",pattern)) return false;
    WIN32_FIND_DATAA entry;
    HANDLE handle=FindFirstFileA(pattern,&entry);
    if (handle==INVALID_HANDLE_VALUE) return GetLastError()==ERROR_FILE_NOT_FOUND;
    do {
        if (!strcmp(entry.cFileName,".") || !strcmp(entry.cFileName,"..") ||
            (entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        bool directory=(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        if (directory && code_skip_directory(entry.cFileName)) continue;
        if (!directory && !code_extension(entry.cFileName)) continue;
        if (!code_join(path,entry.cFileName,child) || !code_task(code,child,directory)) { ok=false; break; }
    } while (FindNextFileA(handle,&entry));
    if (ok && GetLastError()!=ERROR_NO_MORE_FILES) ok=false;
    FindClose(handle);
#else
    DIR *directory=opendir(path);
    if (!directory) return false;
    struct dirent *entry;
    for (;;) {
        errno=0; entry=readdir(directory);
        if (!entry) { if (errno) ok=false; break; }
        if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        if (!code_join(path,entry->d_name,child)) { ok=false; break; }
        struct stat info;
        if (lstat(child,&info)!=0) { ok=false; break; }
        bool is_directory=S_ISDIR(info.st_mode);
        if (S_ISLNK(info.st_mode) || (is_directory && code_skip_directory(entry->d_name))) continue;
        if (!is_directory && (!S_ISREG(info.st_mode) || !code_extension(entry->d_name))) continue;
        if (!code_task(code,child,is_directory)) { ok=false; break; }
    }
    closedir(directory);
#endif
    return ok;
}

static bool code_identifier_start(unsigned char c)
{
    return (c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_';
}
static bool code_identifier(unsigned char c)
{
    return code_identifier_start(c) || (c>='0' && c<='9');
}

static bool code_token(CodeTokens *tokens,size_t start,size_t end,unsigned char kind)
{
    if (tokens->count==CODE_TOKEN_LIMIT) return false;
    if (tokens->count==tokens->capacity) {
        size_t capacity=tokens->capacity ? tokens->capacity*2 : 1024;
        CodeToken *items=realloc(tokens->items,capacity*sizeof(*items));
        if (!items) return false;
        tokens->items=items; tokens->capacity=capacity;
    }
    tokens->items[tokens->count++]=(CodeToken){(uint32_t)start,(uint32_t)end,CODE_NONE,kind};
    return true;
}

static bool code_lex(const char *text,CodeTokens *tokens)
{
    size_t length=strlen(text),i=0;
    bool line_start=true;
    if (length>=3 && !memcmp(text,"\xef\xbb\xbf",3)) i=3;
    while (i<length) {
        unsigned char c=(unsigned char)text[i];
        if (isspace(c)) { if (c=='\n') line_start=true; ++i; continue; }
        if (c=='/' && i+1<length && text[i+1]=='/') {
            i+=2;
            while (i<length) {
                if (text[i]=='\n' && (i==0 || text[i-1]!='\\') &&
                    !(i>=2 && text[i-1]=='\r' && text[i-2]=='\\')) break;
                ++i;
            }
            continue;
        }
        if (c=='/' && i+1<length && text[i+1]=='*') {
            i+=2;
            while (i<length && !(text[i]=='*' && i+1<length && text[i+1]=='/')) {
                if (text[i]=='\n') line_start=true;
                ++i;
            }
            if (i<length) i+=2;
            continue;
        }
        if (line_start && c=='#') {
            while (i<length) {
                if (text[i]=='\n' && (i==0 || text[i-1]!='\\') &&
                    !(i>=2 && text[i-1]=='\r' && text[i-2]=='\\')) break;
                ++i;
            }
            continue;
        }
        line_start=false;
        size_t start=i++;
        unsigned char kind=c;
        if (code_identifier_start(c)) {
            while (i<length && code_identifier((unsigned char)text[i])) ++i;
            kind='I';
        } else if (c=='"' || c=='\'') {
            while (i<length && text[i]!='\n') {
                if (text[i]=='\\') { ++i; if (i<length) ++i; }
                else if ((unsigned char)text[i++]==c) break;
            }
            kind='S';
        }
        if (!code_token(tokens,start,i,kind)) return false;
    }
    uint32_t stack[CODE_NEST_LIMIT];
    size_t depth=0;
    for (size_t n=0;n<tokens->count;++n) {
        unsigned char c=tokens->items[n].kind;
        if (c=='(' || c=='[' || c=='{') {
            if (depth==CODE_NEST_LIMIT) return false;
            stack[depth++]=(uint32_t)n;
        } else if (c==')' || c==']' || c=='}') {
            unsigned char open=c==')' ? '(' : c==']' ? '[' : '{';
            if (depth && tokens->items[stack[depth-1]].kind==open) {
                uint32_t start=stack[--depth];
                tokens->items[start].pair=(uint32_t)n; tokens->items[n].pair=start;
            } else depth=0; /* Recover at an incomplete or conditional block. */
        }
    }
    return true;
}

static bool code_word(const char *text,CodeToken token,const char *word)
{
    return token.kind=='I' && strlen(word)==token.end-token.start &&
        !memcmp(text+token.start,word,token.end-token.start);
}

static bool code_keyword(const char *text,CodeToken token)
{
    static const char *words[]={"if","else","for","while","do","switch","case","return",
        "sizeof","_Alignof","_Alignas","_Static_assert","__attribute__","__attribute",
        "__declspec","asm","__asm__","typeof","__typeof__","_Generic",
        "void","char","short","int","long","float","double","signed","unsigned",
        "_Bool","_Complex","struct","union","enum","const","volatile","restrict",
        "static","extern","inline","typedef","_Atomic","_Noreturn"};
    for (size_t i=0;i<sizeof(words)/sizeof(words[0]);++i)
        if (code_word(text,token,words[i])) return true;
    return false;
}

/* Compact a declaration, retaining literal contents and removing comments. */
static void code_signature(const char *text,size_t begin,size_t end,char *output,size_t capacity)
{
    size_t used=0;
    bool space=false;
    for (size_t i=begin;i<end && used+1<capacity;) {
        unsigned char c=(unsigned char)text[i];
        if (isspace(c)) { space=used!=0; ++i; continue; }
        if (c=='/' && i+1<end && text[i+1]=='*') {
            i+=2;
            while (i+1<end && !(text[i]=='*' && text[i+1]=='/')) ++i;
            i=i+1<end ? i+2 : end; space=used!=0; continue;
        }
        if (c=='/' && i+1<end && text[i+1]=='/') {
            while (i<end && text[i]!='\n') ++i;
            space=used!=0; continue;
        }
        if (space && used+2<capacity) output[used++]=' ';
        space=false;
        output[used++]=text[i++];
        if (c=='"' || c=='\'') {
            while (i<end && used+1<capacity) {
                unsigned char d=(unsigned char)text[i++]; output[used++]=(char)d;
                if (d=='\\' && i<end && used+1<capacity) output[used++]=text[i++];
                else if (d==c) break;
            }
        }
    }
    output[used]='\0';
}

static void code_comment(const char *text,size_t before,char *output,size_t capacity)
{
    size_t end=before;
    while (end && isspace((unsigned char)text[end-1])) --end;
    size_t begin=end;
    if (end>=2 && text[end-2]=='*' && text[end-1]=='/') {
        begin=end-2;
        while (begin>=2 && !(text[begin-2]=='/' && text[begin-1]=='*')) --begin;
        if (begin<2) return;
        begin-=2;
    } else {
        while (begin && text[begin-1]!='\n') --begin;
        size_t first=begin;
        while (first<end && (text[first]==' ' || text[first]=='\t')) ++first;
        if (first+1>=end || text[first]!='/' || text[first+1]!='/') return;
        for (;;) {
            if (!begin) break;
            size_t prior_end=begin-1,prior=prior_end;
            while (prior && text[prior-1]!='\n') --prior;
            first=prior;
            while (first<prior_end && (text[first]==' ' || text[first]=='\t')) ++first;
            if (first+1>=prior_end || text[first]!='/' || text[first+1]!='/') break;
            begin=prior;
        }
    }
    size_t size=end-begin;
    if (size>=capacity) size=capacity-1;
    memcpy(output,text+begin,size); output[size]='\0';
}

static void code_position(const char *text,size_t offset,size_t *line,size_t *column)
{
    *line=0; *column=0;
    for (size_t i=0;i<offset;++i) {
        if (text[i]=='\n') { ++*line; *column=0; }
        else ++*column;
    }
}

static bool code_add_hit(WorkspaceCode *code,const char *path,const char *text,
    const CodeTokens *tokens,size_t begin,size_t end,size_t name,size_t open,bool definition,bool local)
{
    if (code->hit_count==CODE_HIT_LIMIT) return false;
    if (code->hit_count==code->hit_capacity) {
        size_t capacity=code->hit_capacity ? code->hit_capacity*2 : 8;
        CodeHit *hits=realloc(code->hits,capacity*sizeof(*hits));
        if (!hits) return false;
        code->hits=hits; code->hit_capacity=capacity;
    }
    CodeHit *hit=&code->hits[code->hit_count++];
    memset(hit,0,sizeof(*hit));
    snprintf(hit->path,sizeof(hit->path),"%s",path);
    hit->definition=definition; hit->local=local;
    code_position(text,tokens->items[name].start,&hit->line,&hit->column);
    code_signature(text,tokens->items[begin].start,tokens->items[end].start,hit->signature,sizeof(hit->signature));
    code_comment(text,tokens->items[begin].start,hit->comment,sizeof(hit->comment));
    size_t close=tokens->items[open].pair;
    if (close==open+1) hit->unspecified=true;
    else if (!(close==open+2 && code_word(text,tokens->items[open+1],"void"))) {
        hit->parameters=1;
        for (size_t i=open+1;i<close;++i) {
            unsigned char c=tokens->items[i].kind;
            if (c==',') ++hit->parameters;
            else if ((c=='(' || c=='[' || c=='{') && tokens->items[i].pair<close) i=tokens->items[i].pair;
            else if (c=='.' && i+2<close && tokens->items[i+1].kind=='.' && tokens->items[i+2].kind=='.') {
                hit->variadic=true; i+=2;
            }
        }
        if (hit->variadic && hit->parameters) --hit->parameters;
    }
    return true;
}

static bool code_declaration(WorkspaceCode *code,const char *path,const char *text,
    const CodeTokens *tokens,size_t begin,size_t end,bool definition)
{
    bool local=false;
    size_t candidate=SIZE_MAX,open=0;
    for (size_t i=begin;i<end;++i) {
        CodeToken token=tokens->items[i];
        if (code_word(text,token,"typedef") || token.kind=='=') return true;
        if (code_word(text,token,"static")) local=true;
        if (token.kind=='(' || token.kind=='[' || token.kind=='{') {
            if (token.pair>=end) return true;
            if (token.kind=='(' && i>begin+1 && tokens->items[i-1].kind=='I' &&
                !code_keyword(text,tokens->items[i-1])) {
                if (candidate!=SIZE_MAX) return true; /* Ambiguous declaration syntax. */
                candidate=i-1; open=i;
            }
            i=token.pair;
        }
    }
    if (candidate==SIZE_MAX || !code_word(text,tokens->items[candidate],code->name)) return true;
    size_t path_length=strlen(path);
    bool header=path_length && tolower((unsigned char)path[path_length-1])=='h';
    if (local && !header && !code_path_equal(path,code->documents[code->active].path)) return true;
    return code_add_hit(code,path,text,tokens,begin,end,candidate,open,definition,local);
}

static bool code_index(WorkspaceCode *code,const char *path,const char *text)
{
    if (!strstr(text,code->name)) return true;
    CodeTokens tokens={0};
    bool ok=code_lex(text,&tokens);
    size_t begin=0;
    for (size_t i=0;ok && i<tokens.count;++i) {
        CodeToken token=tokens.items[i];
        if (token.kind==';') {
            ok=code_declaration(code,path,text,&tokens,begin,i,false); begin=i+1;
        } else if (token.kind=='{') {
            /* Many C headers wrap their declarations in extern "C" for C++. */
            if (i==begin+2 && code_word(text,tokens.items[begin],"extern") &&
                tokens.items[begin+1].kind=='S') { begin=i+1; continue; }
            ok=code_declaration(code,path,text,&tokens,begin,i,true);
            if (token.pair>=tokens.count) break;
            i=token.pair; begin=i+1;
        } else if (token.kind=='(' || token.kind=='[') {
            if (token.pair>=tokens.count) break;
            i=token.pair;
        } else if (token.kind=='}') begin=i+1;
    }
    free(tokens.items);
    return ok;
}

static void code_reply_text(WorkspaceCodeReply *reply,const char *format,...)
{
    size_t length=strlen(reply->text);
    if (length>=sizeof(reply->text)-1) return;
    va_list args; va_start(args,format);
    vsnprintf(reply->text+length,sizeof(reply->text)-length,format,args);
    va_end(args);
}

static int code_compare_hits(const void *a,const void *b)
{
    const CodeHit *x=a,*y=b;
    int order=strcmp(x->path,y->path);
    if (order) return order;
    return x->line<y->line ? -1 : x->line>y->line ? 1 : 0;
}

static void code_finish(WorkspaceCode *code)
{
    WorkspaceCodeReply reply={0};
    if (!code->hit_count) {
        snprintf(reply.text,sizeof(reply.text),"No C function named '%s' found (macros are not expanded)",code->name);
        reply.error=true; code_deliver(code,&reply); return;
    }
    qsort(code->hits,code->hit_count,sizeof(*code->hits),code_compare_hits);
    bool local=false;
    for (size_t i=0;i<code->hit_count;++i)
        if (code->hits[i].local && code_path_equal(code->hits[i].path,code->documents[code->active].path)) local=true;
    size_t definitions=0,declarations=0;
    for (size_t i=0;i<code->hit_count;++i) {
        const CodeHit *hit=&code->hits[i];
        if (local && !code_path_equal(hit->path,code->documents[code->active].path)) continue;
        if (hit->definition) ++definitions; else ++declarations;
    }
    bool want_definition=code->kind==CODE_DEFINITION ? definitions!=0 : declarations==0;
    size_t candidates=0,chosen=0;
    for (size_t i=0;i<code->hit_count;++i) {
        const CodeHit *hit=&code->hits[i];
        if (local && !code_path_equal(hit->path,code->documents[code->active].path)) continue;
        if (hit->definition==want_definition) { chosen=i; ++candidates; }
    }
    size_t current_count=0,current_choice=0;
    for (size_t i=0;i<code->hit_count;++i) {
        const CodeHit *hit=&code->hits[i];
        if (hit->definition==want_definition && code_path_equal(hit->path,code->documents[code->active].path)) {
            ++current_count; current_choice=i;
        }
    }
    if (current_count==1) { candidates=1; chosen=current_choice; }
    if (candidates==1 && code->kind!=CODE_HOVER) {
        const CodeHit *hit=&code->hits[chosen];
        snprintf(reply.path,sizeof(reply.path),"%s",hit->path);
        reply.line=hit->line; reply.column=hit->column;
    } else {
        if (candidates==1) {
            const CodeHit *hit=&code->hits[chosen];
            code_reply_text(&reply,"%s\n\n",hit->signature);
            if (hit->unspecified) code_reply_text(&reply,"Parameters: unspecified (empty C parameter list)\n");
            else code_reply_text(&reply,"Parameters: %zu%s\n",hit->parameters,hit->variadic ? " fixed, plus ..." : "");
            if (hit->comment[0]) code_reply_text(&reply,"\n%s\n",hit->comment);
        } else code_reply_text(&reply,"Multiple %s for '%s'; open a candidate file to inspect it.\n",
            want_definition ? "definitions" : "declarations",code->name);
        code_reply_text(&reply,"\nFunction locations\n");
        for (size_t i=0;i<code->hit_count;++i) {
            const CodeHit *hit=&code->hits[i];
            if (local && !code_path_equal(hit->path,code->documents[code->active].path)) continue;
            const char *path=hit->path;
            size_t root_length=strlen(code->root);
            if (!strncmp(path,code->root,root_length) && path[root_length]=='/') path+=root_length+1;
            code_reply_text(&reply,"\n%s: %s:%zu:%zu\n%s\n",hit->definition ? "Definition" : "Declaration",
                path,hit->line+1,hit->column+1,hit->signature);
        }
    }
    code_deliver(code,&reply);
}

/* Refuse the common local-variable/function-pointer shadowing cases. This is
 * a lexical guard, not C type resolution; macro-defined declarations remain
 * outside the index's contract. */
static bool code_local_shadow(const char *text,const CodeTokens *tokens,size_t selected)
{
    bool scopes[CODE_NEST_LIMIT]={0};
    size_t depth=0,statement=0;
    CodeToken target=tokens->items[selected];
    for (size_t i=0;i<selected;++i) {
        CodeToken token=tokens->items[i];
        if (token.kind=='{') {
            if (depth+1>=CODE_NEST_LIMIT) return true;
            bool inherited=scopes[depth];
            if (!depth && i && tokens->items[i-1].kind==')') {
                size_t open=tokens->items[i-1].pair;
                if (open<i) for (size_t p=open+1;p<i-1;++p) {
                    CodeToken param=tokens->items[p];
                    if (param.kind=='I' && param.end-param.start==target.end-target.start &&
                        !memcmp(text+param.start,text+target.start,param.end-param.start) &&
                        tokens->items[p+1].kind!='(') inherited=true;
                }
            }
            scopes[++depth]=inherited; statement=i+1;
        } else if (token.kind=='}') { if (depth) --depth; statement=i+1; }
        else if (token.kind==';') statement=i+1;
        else if (depth && token.kind=='I' && i>statement && token.end-token.start==target.end-target.start &&
            !memcmp(text+token.start,text+target.start,token.end-token.start) &&
            (tokens->items[i-1].kind=='I' || tokens->items[i-1].kind=='*') &&
            i+1<tokens->count && tokens->items[i+1].kind!='(') {
            bool declaration=tokens->items[statement].kind=='I' &&
                !code_word(text,tokens->items[statement],"return") &&
                !code_word(text,tokens->items[statement],"if") &&
                !code_word(text,tokens->items[statement],"while") &&
                !code_word(text,tokens->items[statement],"for");
            for (size_t p=statement;p<i && declaration;++p)
                if (tokens->items[p].kind=='=') declaration=false;
            if (declaration) scopes[depth]=true;
        }
    }
    return scopes[depth];
}

WorkspaceCode *workspace_code_create(void) { return calloc(1,sizeof(WorkspaceCode)); }
void workspace_code_destroy(WorkspaceCode *code) { if (code) { code_reset(code); free(code); } }
void workspace_code_detach(WorkspaceCode *code,void *source)
{
    if (code && code->waiting && code->source==source) code_reset(code);
}

void workspace_code_poll(WorkspaceCode *code)
{
    if (!code || !code->waiting) return;
    for (size_t step=0;step<CODE_FILES_PER_POLL;++step) {
        if (code->document_next<code->document_count) {
            const CodeDocument *document=&code->documents[code->document_next++];
            if (!code_index(code,document->path,document->text)) {
                code_fail(code,"Cannot index C buffer (syntax size limit or out of memory)",document->path); return;
            }
            continue;
        }
        if (code->task_next==code->task_count) { code_finish(code); return; }
        /* Enqueuing a directory may realloc the task array, so copy the task. */
        CodeTask task=code->tasks[code->task_next++];
        if (task.directory) {
            if (!code_directory(code,task.path)) {
                code_fail(code,"Cannot scan project directory (I/O, path or index limit)",task.path); return;
            }
            continue;
        }
        bool overlay=false;
        for (size_t i=0;i<code->document_count;++i)
            if (code_path_equal(task.path,code->documents[i].path)) { overlay=true; break; }
        if (overlay) continue;
        FILE *file=fopen(task.path,"rb");
        if (!file) { code_fail(code,"Cannot read C source",task.path); return; }
        bool ok=fseek(file,0,SEEK_END)==0;
        long length=ok ? ftell(file) : -1;
        if (length<0 || length>CODE_FILE_LIMIT || (size_t)length>CODE_TOTAL_LIMIT-code->bytes) {
            fclose(file); code_fail(code,"C source exceeds navigation size limit",task.path); return;
        }
        char *text=malloc((size_t)length+1);
        ok=text && fseek(file,0,SEEK_SET)==0;
        if (ok) ok=fread(text,1,(size_t)length,file)==(size_t)length && !ferror(file);
        if (fclose(file)!=0) ok=false;
        if (ok) {
            text[length]='\0';
            ok=!memchr(text,0,(size_t)length) && code_index(code,task.path,text);
            code->bytes+=(size_t)length;
        }
        free(text);
        if (!ok) { code_fail(code,"Cannot index C source (I/O, syntax size limit or out of memory)",task.path); return; }
    }
}

bool workspace_code_query(WorkspaceCode *code,const char *root,
    const WorkspaceCodeDocument *documents,size_t count,size_t active,
    size_t line,size_t column,WorkspaceCodeKind kind,void *source,
    WorkspaceCodeReceiver receiver,char *error,size_t capacity)
{
    const char *failure="Cannot prepare code lookup: out of memory";
    if (!code || !root || !*root || strlen(root)>=CODE_PATH || !documents ||
        !count || count>CODE_DOCUMENT_LIMIT || active>=count || !receiver ||
        kind<CODE_DEFINITION || kind>CODE_HOVER) {
        snprintf(error,capacity,"Code navigation is unavailable for this file"); return false;
    }
    if (code->waiting) { snprintf(error,capacity,"A code lookup is still running"); return false; }
    for (size_t i=0;i<count;++i) {
        if (!documents[i].path || !documents[i].text || strlen(documents[i].path)>=CODE_PATH) {
            failure="Invalid C source buffer"; goto failed;
        }
        size_t bytes=strlen(documents[i].text);
        if (bytes>CODE_FILE_LIMIT || bytes>CODE_TOTAL_LIMIT-code->bytes) {
            failure="Open C buffers exceed navigation size limit"; goto failed;
        }
        CodeDocument *document=&code->documents[code->document_count++];
        document->path=code_copy(documents[i].path); document->text=code_copy(documents[i].text);
        if (!document->path || !document->text) goto failed;
        code->bytes+=bytes;
    }
    code->active=active;
    const char *text=code->documents[active].text;
    size_t offset=0,row=0;
    while (text[offset] && row<line) { if (text[offset++]=='\n') ++row; }
    if (row!=line) { failure="Cursor is outside the C source"; goto failed; }
    size_t start=offset;
    while (text[offset] && text[offset]!='\n' && offset-start<column) ++offset;
    CodeTokens tokens={0};
    if (!code_lex(text,&tokens)) { free(tokens.items); failure="Cannot parse C source for navigation"; goto failed; }
    size_t selected=SIZE_MAX;
    for (size_t i=0;i<tokens.count;++i) {
        CodeToken token=tokens.items[i];
        if (token.kind=='I' && token.start<=offset && offset<token.end) { selected=i; break; }
    }
    if (selected!=SIZE_MAX) {
        CodeToken token=tokens.items[selected];
        size_t bytes=token.end-token.start;
        if (bytes<sizeof(code->name) && selected+1<tokens.count && tokens.items[selected+1].kind=='(' &&
            (!selected || tokens.items[selected-1].kind!='.') &&
            !(selected>=2 && tokens.items[selected-1].kind=='>' && tokens.items[selected-2].kind=='-') &&
            !code_keyword(text,token)) {
            memcpy(code->name,text+token.start,bytes); code->name[bytes]='\0';
        }
    }
    bool shadow=selected!=SIZE_MAX && code_local_shadow(text,&tokens,selected);
    free(tokens.items);
    if (shadow) { failure="Local variable or function pointer: this index cannot resolve its target"; goto failed; }
    if (!code->name[0]) { failure="Place the cursor on a C function name"; goto failed; }
    snprintf(code->root,sizeof(code->root),"%s",root);
    if (!code_task(code,root,true)) goto failed;
    code->kind=kind; code->source=source; code->receiver=receiver; code->waiting=true;
    return true;
failed:
    code_reset(code); snprintf(error,capacity,"%s",failure); return false;
}
