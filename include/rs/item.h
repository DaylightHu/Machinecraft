#ifndef RS_ITEM_H
#define RS_ITEM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ITEM_NONE = 0,
    ITEM_POISONOUS_POTATO = 1,
    ITEM_COUNT
} ItemId;

enum { POISONOUS_POTATO_STACK_LIMIT = 64 };

typedef struct {
    uint32_t item;         /* Built-in ItemId or a caller-defined identity. */
    uint8_t count, limit;  /* Empty: {0,0,0}; stack limits: 1, 16 or 64. */
} ItemStack;

const char *item_name(uint32_t item);
uint8_t item_stack_limit(uint32_t item);
bool item_stack_valid(ItemStack stack);
/* Move up to amount, limited by the source count and destination capacity. */
uint8_t item_stack_transfer(ItemStack *destination, ItemStack *source,
                            unsigned amount);

#endif
