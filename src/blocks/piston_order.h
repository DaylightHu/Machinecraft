#ifndef RS_PISTON_ORDER_H
#define RS_PISTON_ORDER_H

typedef struct PistonOrderNode {
    struct PistonOrderNode *parent, *child[2], *prev, *next;
    uint32_t hash;
    bool red, tree, live;
} PistonOrderNode;

typedef struct {
    PistonOrderNode nodes[PISTON_PUSH_LIMIT], *heads[64];
    unsigned capacity;
} PistonOrder;

static bool order_red(const PistonOrderNode *node)
{
    return node && node->red;
}

static PistonOrderNode *order_rotate(PistonOrderNode *root,
                                     PistonOrderNode *node, unsigned side)
{
    PistonOrderNode *next = node->child[side], *parent = node->parent;
    node->child[side] = next->child[side ^ 1u];
    if (node->child[side]) node->child[side]->parent = node;
    next->parent = parent;
    if (!parent) { root = next; next->red = false; }
    else parent->child[parent->child[1] == node] = next;
    next->child[side ^ 1u] = node;
    node->parent = next;
    return root;
}

static PistonOrderNode *order_balance_insert(PistonOrderNode *root,
                                             PistonOrderNode *node)
{
    node->red = true;
    while (node->parent && node->parent->red) {
        PistonOrderNode *parent = node->parent, *grand = parent->parent;
        if (!grand) return root;
        unsigned side = grand->child[1] == parent;
        PistonOrderNode *uncle = grand->child[side ^ 1u];
        if (order_red(uncle)) {
            parent->red = uncle->red = false;
            grand->red = true;
            node = grand;
        } else {
            if (node == parent->child[side ^ 1u]) {
                node = parent;
                root = order_rotate(root, node, side ^ 1u);
                parent = node->parent;
                grand = parent->parent;
            }
            parent->red = false;
            grand->red = true;
            root = order_rotate(root, grand, side);
        }
    }
    if (!node->parent) { root = node; root->red = false; }
    return root;
}

static PistonOrderNode *order_balance_delete(PistonOrderNode *root,
                                             PistonOrderNode *node,
                                             PistonOrderNode *parent)
{
    while (node != root && !order_red(node)) {
        if (!parent) return root;
        unsigned side = parent->child[1] == node;
        PistonOrderNode *sibling = parent->child[side ^ 1u];
        if (order_red(sibling)) {
            sibling->red = false;
            parent->red = true;
            root = order_rotate(root, parent, side ^ 1u);
            sibling = parent->child[side ^ 1u];
        }
        if (!sibling) {
            node = parent;
            parent = node->parent;
        } else if (!order_red(sibling->child[0]) && !order_red(sibling->child[1])) {
            sibling->red = true;
            node = parent;
            parent = node->parent;
        } else {
            if (!order_red(sibling->child[side ^ 1u])) {
                if (sibling->child[side]) sibling->child[side]->red = false;
                sibling->red = true;
                root = order_rotate(root, sibling, side);
                sibling = parent->child[side ^ 1u];
            }
            sibling->red = parent->red;
            if (sibling->child[side ^ 1u]) sibling->child[side ^ 1u]->red = false;
            parent->red = false;
            root = order_rotate(root, parent, side ^ 1u);
            node = root;
        }
    }
    if (node) node->red = false;
    return root;
}

static void order_root_first(PistonOrder *order, PistonOrderNode *root)
{
    unsigned bucket = root->hash & (order->capacity - 1u);
    PistonOrderNode *first = order->heads[bucket];
    if (root == first) return;
    if (root->next) root->next->prev = root->prev;
    if (root->prev) root->prev->next = root->next;
    if (first) first->prev = root;
    root->next = first;
    root->prev = NULL;
    order->heads[bucket] = root;
}

static PistonOrderNode *order_insert_tree(PistonOrderNode *root,
                                          PistonOrderNode *node, bool link)
{
    PistonOrderNode *parent = root;
    for (;;) {
        unsigned side = (node->hash ^ UINT32_C(0x80000000)) >=
                        (parent->hash ^ UINT32_C(0x80000000));
        if (parent->child[side]) { parent = parent->child[side]; continue; }
        parent->child[side] = node;
        node->parent = parent;
        if (link) {
            node->next = parent->next;
            if (node->next) node->next->prev = node;
            parent->next = node;
            node->prev = parent;
        }
        return order_balance_insert(root, node);
    }
}

static void order_resize(PistonOrder *order, size_t count)
{
    order->capacity *= 2;
    for (unsigned i = 0; i < order->capacity; ++i) order->heads[i] = NULL;
    for (size_t i = 0; i < count; ++i) {
        PistonOrderNode *node = &order->nodes[i];
        PistonOrderNode **tail = &order->heads[node->hash & (order->capacity - 1u)];
        node->prev = node->next = NULL;
        while (*tail) { node->prev = *tail; tail = &(*tail)->next; }
        *tail = node;
    }
}

static void order_put(PistonOrder *order, BlockPos pos, size_t index)
{
    uint32_t hash = ((uint32_t)pos.y + (uint32_t)pos.z*31u)*31u + (uint32_t)pos.x;
    hash ^= hash >> 16;
    PistonOrderNode *node = &order->nodes[index];
    *node = (PistonOrderNode){.hash = hash, .live = true};
    unsigned bucket = hash & (order->capacity - 1u);
    PistonOrderNode *first = order->heads[bucket];
    if (!first) { order->heads[bucket] = node; return; }
    if (first->tree) {
        node->tree = true;
        order_root_first(order, order_insert_tree(first, node, true));
        return;
    }
    unsigned count = 1;
    PistonOrderNode *tail = first;
    while (tail->next) { tail = tail->next; ++count; }
    tail->next = node;
    node->prev = tail;
    if (count < 8) return;
    if (order->capacity < 64) { order_resize(order, index + 1); return; }
    PistonOrderNode *root = first;
    root->tree = true;
    for (PistonOrderNode *next = first->next; next; next = next->next) {
        next->tree = true;
        root = order_insert_tree(root, next, false);
    }
    order_root_first(order, root);
}

static void order_transplant(PistonOrderNode **root, PistonOrderNode *node,
                              PistonOrderNode *replacement)
{
    if (!node->parent) *root = replacement;
    else node->parent->child[node->parent->child[1] == node] = replacement;
    if (replacement) replacement->parent = node->parent;
}

static void order_remove(PistonOrder *order, size_t index)
{
    PistonOrderNode *node = &order->nodes[index];
    if (!node->live) return;
    node->live = false;
    unsigned bucket = node->hash & (order->capacity - 1u);
    PistonOrderNode *root = order->heads[bucket];
    if (node->prev) node->prev->next = node->next;
    else order->heads[bucket] = node->next;
    if (node->next) node->next->prev = node->prev;
    if (!node->tree || !order->heads[bucket]) return;
    if (!root->child[1] || !root->child[0] || !root->child[0]->child[0]) {
        for (PistonOrderNode *next = order->heads[bucket]; next; next = next->next)
            next->tree = false;
        return;
    }
    PistonOrderNode *replacement, *parent, *successor = node;
    bool red = node->red;
    if (!node->child[0] || !node->child[1]) {
        replacement = node->child[node->child[0] == NULL];
        parent = node->parent;
        order_transplant(&root, node, replacement);
    } else {
        successor = node->child[1];
        while (successor->child[0]) successor = successor->child[0];
        red = successor->red;
        replacement = successor->child[1];
        if (successor->parent == node) parent = successor;
        else {
            parent = successor->parent;
            order_transplant(&root, successor, replacement);
            successor->child[1] = node->child[1];
            successor->child[1]->parent = successor;
        }
        order_transplant(&root, node, successor);
        successor->child[0] = node->child[0];
        successor->child[0]->parent = successor;
        successor->red = node->red;
    }
    if (!red) root = order_balance_delete(root, replacement, parent);
    order_root_first(order, root);
}

static size_t piston_vacated_order(const PistonResolver *resolver,
                                    size_t indices[PISTON_PUSH_LIMIT])
{
    PistonOrder order = {.capacity = 16};
    for (size_t i = 0; i < resolver->push_count; ++i)
        order_put(&order, resolver->to_push[i], i);
    for (size_t i = resolver->push_count; i > 0; --i) {
        BlockPos dest = block_offset(resolver->to_push[i - 1], resolver->push_direction);
        int index = piston_position_index(resolver->to_push, resolver->push_count, dest);
        if (index >= 0) order_remove(&order, (size_t)index);
    }
    if (resolver->extending) {
        BlockPos head = block_offset(resolver->piston_position, resolver->piston_direction);
        int index = piston_position_index(resolver->to_push, resolver->push_count, head);
        if (index >= 0) order_remove(&order, (size_t)index);
    }
    size_t count = 0;
    for (unsigned bucket = 0; bucket < order.capacity; ++bucket)
        for (PistonOrderNode *node = order.heads[bucket]; node; node = node->next)
            indices[count++] = (size_t)(node - order.nodes);
    return count;
}
#endif
