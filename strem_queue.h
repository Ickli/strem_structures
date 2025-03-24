#ifndef STREM_QUEUE_H_
#define STREM_QUEUE_H_
#include "strem_vector.h"

typedef struct {
    StremVector v;
    size_t rear;
    size_t front;
} StremQueue;

StremQueue StremQueue_construct(size_t elem_size, size_t capacity);
void StremQueue_free(StremQueue* q);
void StremQueue_reserve(StremQueue* q, size_t capacity);
void* StremQueue_insert(StremQueue* q, void const* elem);
void StremQueue_pop(StremQueue* q);

#define StremQueuePeek(q, type) StremVectorAt((q).v, type, (q).rear)
#define StremQueueDeque(q, type) StremQueuePeek(q, type); StremQueue_pop(&(q))
#define StremQueueSize(q) (q).v.size
#endif // STREM_QUEUE_H_
