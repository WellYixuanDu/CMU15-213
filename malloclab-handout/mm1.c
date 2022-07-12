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

#define NEXT_BLOCK_PTR(blk_ptr) ((void *)blk_ptr + GET_SIZE(blk_ptr))
#define PREV_BLOCK_PTR(blk_ptr) ((void *)blk_ptr - GET_SIZE((blk_ptr - WORD_SIZE)))

#define ROUNDUP_EIGHT_BYTES(size) (((size + (DWORD_SIZE - 1)) / 8) * 8)

static void* head_list = NULL;

static void* merge_free_fragment(void *ptr) {
    size_t prev_alloc = GET_ALLOC(PREV_BLOCK_PTR(ptr));
    size_t next_alloc = GET_ALLOC(NEXT_BLOCK_PTR(ptr));
    size_t size = GET_SIZE(ptr);
    if (prev_alloc && next_alloc) {
        return ptr;
    }
    else if (prev_alloc && !(next_alloc)) {
        size += GET_SIZE(NEXT_BLOCK_PTR(ptr));
        PUT_WORD(HEADER_PTR(ptr), PACK_SIZE_ALLOC(size, 0));
        PUT_WORD(FOOTER_PTR(ptr), PACK_SIZE_ALLOC(size, 0));
    }
    else if ((!prev_alloc) && next_alloc) {
        size += GET_SIZE(PREV_BLOCK_PTR(ptr));
        PUT_WORD(FOOTER_PTR(ptr), PACK_SIZE_ALLOC(size, 0));
        PUT_WORD(HEADER_PTR(PREV_BLOCK_PTR(ptr)), PACK_SIZE_ALLOC(size, 0));
        ptr = PREV_BLOCK_PTR(ptr);
    }
    else {
        size += GET_SIZE(NEXT_BLOCK_PTR(ptr)) + GET_SIZE(PREV_BLOCK_PTR(ptr));
        PUT_WORD(HEADER_PTR(PREV_BLOCK_PTR(ptr)), PACK_SIZE_ALLOC(size, 0));
        PUT_WORD(FOOTER_PTR(NEXT_BLOCK_PTR(ptr)), PACK_SIZE_ALLOC(size, 0));
        ptr = PREV_BLOCK_PTR(ptr);
    }
    return ptr;
}

static void* extend_heap(size_t extend_word_size) {
    void *blk_ptr = NULL;
    size_t size;
    size = (extend_word_size % 2) ? ((extend_word_size + 1)*WORD_SIZE) : extend_word_size * WORD_SIZE;
    if ((blk_ptr = mem_sbrk(size)) == (void *) - 1) {
        return NULL;
    }
    PUT_HEADER_WORD(blk_ptr, size, 0);
    PUT_FOOTER_WORD(blk_ptr, size, 0);
    PUT_WORD(FOOTER_PTR(blk_ptr) + WORD_SIZE, PACK_SIZE_ALLOC(0, 1));
    return merge_free_fragment(blk_ptr);
}

static void* find_fit_block_ptr(size_t size) {
    void *blk_ptr = head_list + DWORD_SIZE, *heap_hi_address = mem_heap_hi();
    while (blk_ptr < heap_hi_address) {
        if (!GET_ALLOC(blk_ptr) && GET_SIZE(blk_ptr) >= size) {
            return blk_ptr;
        }
        blk_ptr = NEXT_BLOCK_PTR(blk_ptr);
    }
    return NULL;
}

static void place(void * blk_ptr, size_t size) {
    size_t prev_size = GET_SIZE(blk_ptr);
    size_t now_size = prev_size - size;
    PUT_HEADER_WORD(blk_ptr, size, 1);
    PUT_FOOTER_WORD(blk_ptr, size, 1);
    if (!now_size)  return;
    void *next_ptr = NEXT_BLOCK_PTR(blk_ptr);
    PUT_HEADER_WORD(next_ptr, now_size, 0);
    PUT_FOOTER_WORD(next_ptr, now_size, 0);

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
    if ((blk_ptr = find_fit_block_ptr(size)) != NULL) {
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
    merge_free_fragment(ptr);
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














