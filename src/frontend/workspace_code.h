#ifndef MC_WORKSPACE_CODE_H
#define MC_WORKSPACE_CODE_H
#include <stdbool.h>
#include <stddef.h>

typedef struct WorkspaceCode WorkspaceCode;
typedef enum { CODE_DEFINITION, CODE_DECLARATION, CODE_HOVER } WorkspaceCodeKind;
typedef struct { const char *path,*text; } WorkspaceCodeDocument;
typedef struct {
    char path[4096],text[16384];
    size_t line,column; /* LSP UTF-16 positions, zero based. */
    bool error;
} WorkspaceCodeReply;
typedef void (*WorkspaceCodeReceiver)(void *source,const WorkspaceCodeReply *reply);

WorkspaceCode *workspace_code_create(void);
void workspace_code_destroy(WorkspaceCode *code);
void workspace_code_detach(WorkspaceCode *code,void *source);
void workspace_code_poll(WorkspaceCode *code);
/* Text is copied before returning; unsaved buffers need not be written to disk. */
bool workspace_code_query(WorkspaceCode *code,const char *root,
    const WorkspaceCodeDocument *documents,size_t document_count,size_t active,
    size_t line,size_t column,WorkspaceCodeKind kind,void *source,
    WorkspaceCodeReceiver receiver,char *error,size_t error_capacity);
#endif
