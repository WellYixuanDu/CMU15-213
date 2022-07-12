/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

#ifdef DEBUG
#define DBG_PRINTF(...) fprintf(stderr, __VA_ARGS__)
#define CHECKHEAP(verbose) mm_checkheap(verbose)
#else
#define DBG_PRINTF(...)
#define CHECKHEAP(verbose)
#endif

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))


#define WORD_SIZE (4)
#define DWORD_SIZE (8)
#define CHUNKSIZE (1 << 12)

#define MAX(x, y) ((x >= y) ? (x) : (y))
#define MIN(x, y) ((x <= y) ? (x) : (y))

#define PACK_SIZE_ALLOC(size, alloc) (size | alloc) 

#define GET_WORD(p) (*((int *)(p)))
#define PUT_WORD(p, val) ((*((int *)(p))) = val)

#define GET_SIZE(blk_ptr) (GET_WORD((void *)blk_ptr - WORD_SIZE) & ~0x7)
#define GET_ALLOC(blk_ptr) (GET_WORD((void *)blk_ptr - WORD_SIZE) & 0x1)  // 0 unused, 1 used
#define GET_VALID_SIZE(blk_ptr) (GET_SIZE(blk_ptr) - 0x8)


#define HEADER_PTR(blk_ptr) ((void *)blk_ptr - WORD_SIZE)
#define FOOTER_PTR(blk_ptr) ((void *)blk_ptr + GET_SIZE(blk_ptr) - DWORD_SIZE)

#define PUT_HEADER_WORD(blk_ptr, size, alloc) PUT_WORD(HEADER_PTR(blk_ptr), PACK_SIZE_ALLOC(size, alloc))
#define PUT_FOOTER_WORD(blk_ptr, size, alloc) PUT_WORD(FOOTER_PTR(blk_ptr), PACK_SIZE_ALLOC(size, alloc))
#define PUT_PREV_PTR_WORD(blk_ptr, address) PUT_WORD(blk_ptr, address)
#define PUT_NEXT_PTR_WORD(blk_ptr, address) PUT_WORD(blk_ptr+WORD_SIZE, address)


#define NEXT_BLOCK_PTR(blk_ptr) ((void *)blk_ptr + GET_SIZE(blk_ptr))
#define PREV_BLOCK_PTR(blk_ptr) ((void *)blk_ptr - GET_SIZE((blk_ptr - WORD_SIZE)))

#define ROUNDUP_EIGHT_BYTES(size) (((size + (DWORD_SIZE - 1)) / 8) * 8)

static void* head_list = NULL;

struct block_node {
    struct block_node* prev_ptr;
    struct block_node* next_ptr;
}block_node;

struct block_node_list
{
    struct block_node block_node_begin;
    struct block_node block_node_end;
}block_node_list[32];

static void init_block_node(struct block_node* tmp_block_node) {
    tmp_block_node->prev_ptr = NULL;
    tmp_block_node->next_ptr = NULL;
}

static void init_block_node_list(struct block_node_list * tmp_block_node_list) {
    init_block_node(&tmp_block_node_list->block_node_begin);
    init_block_node(&tmp_block_node_list->block_node_end);
    tmp_block_node_list->block_node_begin.next_ptr = (&tmp_block_node_list->block_node_end);
    tmp_block_node_list->block_node_end.prev_ptr = (&tmp_block_node_list->block_node_begin);
}

static int32_t search_index(int32_t size) {
    int tmp_size = 1;
    int index = 0;
    while((tmp_size <<= 1) <= size) {
        index++;
    } 
    return index;
}

static void remove_block_ptr(void *blk_ptr) {
    struct block_node* tmp_block_node = (struct block_node *)blk_ptr;
    tmp_block_node->prev_ptr->next_ptr = tmp_block_node->next_ptr;
    tmp_block_node->next_ptr->prev_ptr = tmp_block_node->prev_ptr;
}

static void insert_put_block_ptr(struct block_node_list *tmp_block_node_list, void *blk_ptr) {

    PUT_WORD(blk_ptr, (int)&tmp_block_node_list->block_node_begin);
    PUT_WORD(blk_ptr+WORD_SIZE, (int)tmp_block_node_list->block_node_begin.next_ptr);

    struct block_node *tmp_block_node = (struct block_node *)blk_ptr;
    tmp_block_node->next_ptr = tmp_block_node_list->block_node_begin.next_ptr;
    tmp_block_node->prev_ptr = (&tmp_block_node_list->block_node_begin);
    tmp_block_node->prev_ptr->next_ptr = tmp_block_node;
    tmp_block_node->next_ptr->prev_ptr = tmp_block_node; 
}

static void* merge_free_fragment(void *ptr) {
    void *prev_ptr, *next_ptr, *now_ptr, *heap_hi_address = mem_heap_hi();
    now_ptr = ptr;
    prev_ptr = PREV_BLOCK_PTR(now_ptr);
    while (prev_ptr > head_list && !GET_ALLOC(prev_ptr)) {
        size_t size = GET_SIZE(now_ptr) + GET_SIZE(prev_ptr);
        PUT_FOOTER_WORD(now_ptr, size, 0);
        PUT_HEADER_WORD(prev_ptr, size, 0);
        remove_block_ptr(prev_ptr);
        now_ptr = prev_ptr;
        prev_ptr = PREV_BLOCK_PTR(prev_ptr);
    }
    next_ptr = NEXT_BLOCK_PTR(ptr);
    while(next_ptr < heap_hi_address && !GET_ALLOC(next_ptr)) {
        size_t size = GET_SIZE(now_ptr) + GET_SIZE(next_ptr);
        PUT_HEADER_WORD(now_ptr, size, 0);
        PUT_FOOTER_WORD(next_ptr, size, 0);
        remove_block_ptr(next_ptr);
        next_ptr = NEXT_BLOCK_PTR(next_ptr);
    }
    return now_ptr;
}

static void* extend_heap(size_t extend_word_size) {
    void *blk_ptr = NULL;
    size_t size;
    size = (extend_word_size % 2) ? ((extend_word_size + 1)*WORD_SIZE) : extend_word_size * WORD_SIZE;
    if ((blk_ptr = mem_sbrk(size)) == (void *) - 1) {
        return NULL;
    }
    size_t index = search_index(size);
    PUT_HEADER_WORD(blk_ptr, size, 0);
    PUT_FOOTER_WORD(blk_ptr, size, 0);
    PUT_WORD(FOOTER_PTR(blk_ptr) + WORD_SIZE, PACK_SIZE_ALLOC(0, 1));
    insert_put_block_ptr(block_node_list+index, blk_ptr);
    return blk_ptr;
}

static void* find_fit_block_ptr(size_t index, size_t size) {

    for (int i = index; i < 32; ++i) {
        struct block_node* tmp_node_start = block_node_list[i].block_node_begin.next_ptr;
        struct block_node* tmp_node_end = &block_node_list[i].block_node_end;
        while (tmp_node_start != tmp_node_end) {
            if (GET_SIZE((void *)tmp_node_start) >= size) {
                return (void *)tmp_node_start;
            }
            tmp_node_start = tmp_node_start->next_ptr;
        }
    }
    return NULL;
}

static void place(void * blk_ptr, size_t size) {
    size_t prev_size = GET_SIZE(blk_ptr);
    size_t now_size = prev_size - size;
    remove_block_ptr(blk_ptr);
    if (now_size <= 8) {
        PUT_HEADER_WORD(blk_ptr, prev_size, 1);
        PUT_FOOTER_WORD(blk_ptr, prev_size, 1);
        return;
    }

    PUT_HEADER_WORD(blk_ptr, size, 1);
    PUT_FOOTER_WORD(blk_ptr, size, 1);  

    void *next_ptr = NEXT_BLOCK_PTR(blk_ptr);
    size_t index = search_index(now_size);
    PUT_HEADER_WORD(next_ptr, now_size, 0);
    PUT_FOOTER_WORD(next_ptr, now_size, 0);
    insert_put_block_ptr(block_node_list + index, next_ptr);
}



/* 
 * mm_init - initialize the malloc package.
 */

int mm_init(void)
{
    if ((head_list = mem_sbrk(4 * WORD_SIZE)) == (void *) - 1)
        return -1;
    PUT_WORD(head_list, PACK_SIZE_ALLOC(0, 0));
    PUT_WORD(head_list + WORD_SIZE, PACK_SIZE_ALLOC(DWORD_SIZE, 1));
    PUT_WORD(head_list + WORD_SIZE*2, PACK_SIZE_ALLOC(DWORD_SIZE, 1));
    PUT_WORD(head_list + WORD_SIZE*3, PACK_SIZE_ALLOC(0, 1));
    head_list += 2*WORD_SIZE;

    for(int i = 0;i < 32; ++i) {
        init_block_node_list(block_node_list + i);
    }

    if (extend_heap(CHUNKSIZE / WORD_SIZE) == NULL) return -1;    
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }
    void * blk_ptr = NULL;
    size = ROUNDUP_EIGHT_BYTES(size) + 8;
    size_t index = search_index(size);

    if ((blk_ptr = find_fit_block_ptr(index, size)) != NULL) {
        place(blk_ptr, size);
        return blk_ptr;
    }

    blk_ptr = extend_heap(MAX(CHUNKSIZE, size) / WORD_SIZE);
    if (!blk_ptr)   return NULL;
    place(blk_ptr, size);
    return blk_ptr;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    if (ptr == NULL || ptr <= head_list || ptr >= mem_heap_hi()) {
        return;
    }
    PUT_HEADER_WORD(ptr, GET_SIZE(ptr), 0);
    PUT_FOOTER_WORD(ptr, GET_SIZE(ptr), 0);
    ptr = merge_free_fragment(ptr);

    if (!ptr)   return;
    size_t size = GET_SIZE(ptr);
    size_t index = search_index(size);
    insert_put_block_ptr(block_node_list + index, ptr);
    return;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *newptr;

    if (!size) {
        mm_free(ptr);
        return NULL;
    }

    newptr = mm_malloc(size);
    if (!ptr || !newptr)    return newptr;
    size_t copy_size = MIN(size, GET_VALID_SIZE(ptr));
    memcpy(newptr, ptr, copy_size);
    mm_free(ptr);
    return newptr;
}














