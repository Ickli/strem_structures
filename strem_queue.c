#include "strem_queue.h"
#include <string.h>

StremQueue StremQueue_construct(size_t elem_size, size_t capacity) {
    StremQueue q = {0};
    q.v = StremVector_construct(elem_size, capacity ? capacity : 1);
    return q;
}

void StremQueue_free(StremQueue* q) {
    StremVector_free(&q->v);
}

void StremQueue_reserve(StremQueue* q, size_t capacity) {
    if(capacity <= q->v.capacity_elems) {
        return;
    }

    // TODO: newbuf may be null
    char* newbuf = calloc(capacity, q->v.elem_size);
    char* appendptr = newbuf;

    if(q->v.size != 0) {
        if(q->rear >= q->front) {
            size_t cpyat = q->rear*q->v.elem_size;
            size_t cpysize = (q->v.capacity_elems - q->rear)*q->v.elem_size;
            memcpy(appendptr, &StremVectorAt(q->v, char, cpyat), cpysize);
            appendptr += cpysize;

            cpyat = 0;
            cpysize = q->front*q->v.elem_size;
            memcpy(appendptr, &StremVectorAt(q->v, char, cpyat), cpysize);
            appendptr += cpysize;
        } else {
            const size_t cpyat = q->rear*q->v.elem_size;
            const size_t cpysize = q->v.size*q->v.elem_size;
            memcpy(appendptr, &StremVectorAt(q->v, char, cpyat), cpysize);
            appendptr += cpysize;
        }
    }

    free(q->v.content);
    q->v.content = (void*)newbuf;
    q->v.capacity_elems = capacity;
    q->rear = 0;
    q->front = (appendptr - newbuf) / q->v.elem_size;
}

void* StremQueue_insert(StremQueue* q, void const* elem) {
    size_t newfront = (q->front + 1)%q->v.capacity_elems;

    if(newfront == q->rear) {
        StremQueue_reserve(q, q->v.capacity_elems * 2);
        newfront = q->front + 1;
    }

    char* dst = &StremVectorAt(q->v, char, q->front * q->v.elem_size);
    memcpy((void*)dst, elem, q->v.elem_size);

    q->v.size++;
    q->front = newfront;
    return dst;
}

void StremQueue_pop(StremQueue* q) {
    if(q->v.size != 0) {
        q->rear = (q->rear + 1)%q->v.capacity_elems;
        q->v.size--;
    }
}
