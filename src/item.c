#include "rs/item.h"

const char *item_name(uint32_t item)
{
    switch (item) {
    case ITEM_NONE: return "Empty";
    case ITEM_POISONOUS_POTATO: return "Poisonous potato";
    default: return "Unknown item";
    }
}

uint8_t item_stack_limit(uint32_t item)
{
    return item == ITEM_POISONOUS_POTATO ? POISONOUS_POTATO_STACK_LIMIT : 0;
}

bool item_stack_valid(ItemStack stack)
{
    if (!stack.count) return !stack.item && !stack.limit;
    return stack.item && (stack.limit == 1 || stack.limit == 16 ||
                          stack.limit == 64) && stack.count <= stack.limit;
}

uint8_t item_stack_transfer(ItemStack *destination, ItemStack *source,
                            unsigned amount)
{
    if (!destination || !source || destination == source ||
        !item_stack_valid(*destination) || !item_stack_valid(*source) ||
        !source->count || !amount) return 0;
    if (destination->count && (destination->item != source->item ||
                               destination->limit != source->limit)) return 0;
    unsigned space = source->limit - destination->count;
    if (amount > source->count) amount = source->count;
    if (amount > space) amount = space;
    if (!amount) return 0;
    if (!destination->count) {
        *destination = *source;
        destination->count = 0;
    }
    destination->count += (uint8_t)amount;
    source->count -= (uint8_t)amount;
    if (!source->count) *source = (ItemStack){0};
    return (uint8_t)amount;
}
