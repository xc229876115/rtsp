#include "list.h"
#include "tuya_sm_utils.h"



list_node_t* list_node_new(void *val)
{
    list_node_t *self = NULL;

    self = malloc(sizeof (list_node_t));
    if (self == NULL) {
        TUYA_LOG("no more memory!\n");
        return NULL;
    }

    self->prev = NULL;
    self->next = NULL;
    self->val = val;

    return self;
}

list_t* list_create(void* pcontext, LIST_FREE_FUNC free_func)
{
    list_t *self;

    self = malloc(sizeof (list_t));
    if (self == NULL) {
        return NULL;
    }

    self->head = NULL;
    self->tail = NULL;
    self->free = free_func;
    self->match = NULL;
    self->len = 0;
    self->pcontext = pcontext;

    return self;
}

void list_destroy(list_t **self)
{
    unsigned int len = (*self)->len;
    list_node_t *next;
    list_node_t *curr = (*self)->head;

    while(len--) {
        next = curr->next;
        if ((*self)->free) {
            (*self)->free((*self)->pcontext, curr->val);
        }
        free(curr);
        curr = next;
    }

    free(*self);
    *self = NULL;

    return;
}

int list_rpush(list_t *self, void *value)
{
    list_node_t *node = NULL;

    if (!value) {
        TUYA_LOG("set value error!\n");
        return -1;
    }

    node = list_node_new(value);
    if (!node) {
        TUYA_LOG("can not create new node!\n");
        return -1;
    }

    if (self->len) {
        node->prev = self->tail;
        node->next = NULL;
        self->tail->next = node;
        self->tail = node;
    } else {
        self->head = self->tail = node;
        node->prev = node->next = NULL;
    }

    ++self->len;

    // TUYA_LOG("list len: %d\n", self->len);

    return 0;
}

void *list_rpop(list_t *self)
{
    if (!self->len) {
        return NULL;
    }

    list_node_t *node = self->tail;

    if (--self->len) {
        (self->tail = node->prev)->next = NULL;
    } else {
        self->tail = self->head = NULL;
    }

    node->next = node->prev = NULL;

    return node->val;
}

void *list_lpop(list_t *self)
{
    if (!self->len) {
        // TUYA_LOG("no data in list\n");
        return NULL;
    }

    list_node_t *node = self->head;

    // if(--self->len) {
    //     (self->head = node->next)->prev = NULL;
    // } else {
    //     self->head = self->tail = NULL;
    // }

    // node->next = node->prev = NULL;

    return node->val;
}

int list_lpush(list_t *self, void *value)
{
    list_node_t *node;

    if (!value) {
        return -1;
    }

    node = list_node_new(value);
    if (!node) {
        return -1;
    }

    if (self->len) {
        node->next = self->head;
        node->prev = NULL;
        self->head->prev = node;
        self->head = node;
    } else {
        self->head = self->tail = node;
        node->prev = node->next = NULL;
    }

    ++self->len;

    // TUYA_LOG("list size: %d\n", self->len);

    return 0;
}

void *list_value_exist(list_t *self, void *val)
{
    list_iterator_t *it = list_iterator_create (self, LIST_HEAD);
    list_node_t *node;
    void *ret;

    ret = NULL;

    while ((node = list_iterator_next(it))) {
        if (self->match) {
            if (self->match(val, node->val)) {
                ret = val;
                break;
            }
        } else {
            if (val == node->val) {
                ret = val;
                break;
            }
        }
    }

    list_iterator_destroy(&it);

    return ret;
}

void *list_at(list_t *self, int index)
{
    list_direction_t direction = LIST_HEAD;

    if (index < 0) {
        direction = LIST_TAIL;
        index = ~index;
    }

    if ((unsigned)index < self->len) {
        list_iterator_t *it = list_iterator_create (self, direction);
        list_node_t *node = list_iterator_next (it);
        while (index--) node = list_iterator_next (it);
        list_iterator_destroy (&it);
        return node->val;
    }

    return NULL;
}

int list_remove(list_t *self, void *value)
{
    list_node_t *node = NULL;

    if(self == NULL) {
        return -1;
    }

    node = self->head;
    while(node != NULL) {
        if(node->val == value) {
            break;
        }

        node = node->next;
    }

    if (!node) {
        TUYA_LOG("error, can not find!\n");
        return -1;
    }

    node->prev
    ? (node->prev->next = node->next)
    : (self->head = node->next);

    node->next
    ? (node->next->prev = node->prev)
    : (self->tail = node->prev);

    if(self->free) {
        self->free(self->pcontext, node->val);
    }

    free(node);
    --self->len;

    // TUYA_LOG("list size: %d\n", self->len);

    return 0;
}

static list_iterator_t *list_iterator_new_from_node(list_node_t *node, list_direction_t direction)
{
    list_iterator_t *self;
    if (!(self = malloc(sizeof(list_iterator_t)))) {
        return NULL;
    }

    self->next = node;
    self->direction = direction;

    return self;
}

list_iterator_t *list_iterator_create(list_t *list, list_direction_t direction)
{
    list_node_t *node = direction == LIST_HEAD
                        ? list->head
                        : list->tail;
    return list_iterator_new_from_node(node, direction);
}

void *list_iterator_next(list_iterator_t *self)
{
    list_node_t *curr = self->next;

    if (curr) {
        self->next = self->direction == LIST_HEAD
                     ? curr->next
                     : curr->prev;
        return curr->val;
    }

    return NULL;
}

void list_iterator_destroy(list_iterator_t **self)
{
    free(*self);
    *self = NULL;

    return;
}

