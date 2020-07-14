/*
 * Simple, 32-bit and 64-bit clean allocator based on implicit free
 * lists, first fit placement, and boundary tag coalescing, as described
 * in the CS:APP2e text. Blocks must be aligned to doubleword (8 byte)
 * boundaries. Minimum block size is 16 bytes.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your group information in the following struct.
 ********************************************************/
/* group_t group = {
	
}; */

/* Basic constants and macros */
#define WSIZE 4             /* Word and header/footer size (bytes) */
#define DSIZE 8             /* Doubleword size (bytes) */
#define CHUNKSIZE (1 << 12) /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))

/* Global variables */
static void *heap_listp = 0; /* Pointer to first block */

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void printblock(void *bp);
static void checkheap(int verbose);
static void checkblock(void *bp);

#define OVERHEAD (sizeof(void *))
#define NO_OF_LIST 12
// control the util, more list lead a larger waste on space
#define RANGE 128
// control the thru, a smaller range lead smaller list in each range
// this also enhace the util when a smaller range is unused.
// an almost best fit case will happen in the a first fit search
// NO_OF_LIST * RANGE should be larger to prevent a long last list
#define MIN_SIZE (2 * OVERHEAD + DSIZE)

#define _LEFT(bp) ((void *)(bp))
#define _RIGHT(bp) ((void *)(bp) + OVERHEAD)
// return the address for the pointer
#define GET_LEFT(bp) (*(void **)_LEFT(bp))
#define GET_RIGHT(bp) (*(void **)_RIGHT(bp))

#define SET_LEFT(bp, sub) (*(void **)_LEFT(bp) = sub)
#define SET_RIGHT(bp, right) (*(void **)_RIGHT(bp) = right)

#define GET_CONTENT(bp) (*(void **)(bp))
#define SET_CONTENT(bp, p) (*(void **)(bp) = p)

void *heads;
/*
 * mm_init - Initialize the memory manager
 */

// for calculate the respective head of the size
void *GET_HEAD_OF(size_t size) {
  // make sure the offset > 1
  int offset = 1 + size / RANGE;
  // put all out ranged to the last list
  if (offset > NO_OF_LIST)
    offset = NO_OF_LIST;
  // calculate respective address
  return heads + (offset - 1) * OVERHEAD;
}

void print_block(void *bp) {
  printf("%p (%d), left : %p, right : %p\n", bp, GET_SIZE(HDRP(bp)),
         GET_LEFT(bp), GET_RIGHT(bp));
}

void printAll() {
  if (heads == NULL)
    return;

  void *bp, *temp;
  for (bp = heads; bp < heap_listp - OVERHEAD; bp += OVERHEAD) {
    temp = GET_CONTENT(bp);
    printf("%p : %p\n", bp, temp);
    while (temp) {
      print_block(temp);
      temp = GET_RIGHT(temp);
    }
  }
}

void init_block(void *bp) {
  // prevent misdirect of pointer
  SET_LEFT(bp, NULL);
  SET_RIGHT(bp, NULL);
}

void insert_block(void *bp) {
  // printf("insert_block\t");
  // print_block(bp);

  init_block(bp);

  size_t size = GET_SIZE(HDRP(bp));
  // get the respective head address
  void *head = GET_HEAD_OF(size);
  void *temp;
  // if the list is not empty, assign the first element to temp
  if ((temp = GET_CONTENT(head))) {
    // if temp has right, send its left to bp
    if (GET_RIGHT(temp))
      SET_LEFT(GET_RIGHT(temp), bp);
    // insert the bp
    SET_RIGHT(bp, GET_RIGHT(temp));
    SET_LEFT(bp, temp);
    SET_RIGHT(temp, bp);
  } else {
    // if there is no element in the list, set bp to be the first element
    SET_CONTENT(head, bp);
  }
}

void remove_block(void *bp) {
  // printf("remove_block\t");
  // print_block(bp);

  void *left = GET_LEFT(bp);
  void *right = GET_RIGHT(bp);
  void *head;
  // if bp has right, link its left with its left
  if (right)
    SET_LEFT(right, left);

  // if bp has left, it is not the first element
  if (left)
    // link bp's left with its right
    SET_RIGHT(left, right);
  else {
    // if bp has no left, it is the first element
    // calculate its respective head
    head = GET_HEAD_OF(GET_SIZE(HDRP(bp)));
    /* if the first element is really bp, (as initialized element also has a
    NULL left, second confirmation is needed) */
    if (GET_CONTENT(head) == bp)
      // set the first element be bp's right
      SET_CONTENT(head, right);
  }
}

int mm_init(void) {
  /* Create the initial empty heap */
  // request more space for storing the address per range
  if ((heap_listp = mem_sbrk(NO_OF_LIST * OVERHEAD + 4 * WSIZE)) == (void *)-1)
    return -1;
  // heads is the pointer of the first list
  heads = heap_listp;

  int i = 0;
  // Initialize the lists pointer to NULL (as no first element)
  for (; i < NO_OF_LIST; i++) {
    // also calculate the actual available address
    heap_listp += OVERHEAD;
    SET_CONTENT(heap_listp, NULL);
  }
  //
  // printf("heads : %p\n", heads);
  // printf("heap_list : %p\n", heap_listp);

  // Initialize the the header of the heap
  PUT(heap_listp, 0);                            /* Alignment padding */
  PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* Prologue header */
  PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* Prologue footer */

  heap_listp += (2 * WSIZE);
  PUT(heap_listp + WSIZE, PACK(0, 1)); /* Epilogue header */

  /* Extend the empty heap with a free block of CHUNKSIZE bytes */
  if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
    return -1;
  return 0;
}

/*
 * mm_malloc - Allocate a block with at least size bytes of payload
 */
void *mm_malloc(size_t size) {
  size_t asize;      /* Adjusted block size */
  size_t extendsize; /* Amount to extend heap if no fit */
  char *bp;

  if (heap_listp == 0) {
    mm_init();
  }
  /* Ignore spurious requests */
  if (size == 0)
    return NULL;

  /* Adjust block size to include overhead and alignment reqs. */
  // as 2 pointers are stored in each block, there is a min size of each block
  if (size < MIN_SIZE)
    asize = MIN_SIZE;
  else
    asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

  /* Search the free list for a fit */
  if ((bp = find_fit(asize)) != NULL) {
    place(bp, asize);
    return bp;
  }

  /* No fit found. Get more memory and place the block */
  extendsize = MAX(asize, CHUNKSIZE);
  if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
    return NULL;
  place(bp, asize);
  return bp;
}

/*
 * mm_free - Free a block
 */
void mm_free(void *bp) {
  if (bp == 0)
    return;

  size_t size = GET_SIZE(HDRP(bp));
  if (heap_listp == 0) {
    mm_init();
  }

  PUT(HDRP(bp), PACK(size, 0));
  PUT(FTRP(bp), PACK(size, 0));
  // printf("mm_free %p\n", bp);
  insert_block(coalesce(bp));
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static void *coalesce(void *bp) {
  size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  size_t size = GET_SIZE(HDRP(bp));

  if (prev_alloc && next_alloc) { /* Case 1 */
    // return bp;
  } else if (prev_alloc && !next_alloc) { /* Case 2 */
    remove_block(NEXT_BLKP(bp));

    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  } else if (!prev_alloc && next_alloc) { /* Case 3 */
    // if this is not allocated, it is in the lists. Delete it to update its
    // position
    remove_block(PREV_BLKP(bp));

    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
  } else { /* Case 4 */
    // if this is not allocated, it is in the lists. Delete it to update its
    // position
    remove_block(PREV_BLKP(bp));
    remove_block(NEXT_BLKP(bp));

    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
  }

  return bp;
}

/*
 * checkheap - We don't check anything right now.
 */
void mm_checkheap(int verbose) {}

/*
 * The remaining routines are internal helper routines
 */

/*
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words) {
  char *bp;
  size_t size;

  /* Allocate an even number of words to maintain alignment */
  size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
  if ((long)(bp = mem_sbrk(size)) == -1)
    return NULL;

  /* Initialize free block header/footer and the epilogue header */
  PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
  PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */
  // this is the boundary -> size(bp) = 0

  /* Coalesce if the previous block was free */
  bp = coalesce(bp);
  // insert the block as it is a new free block
  insert_block(bp);
  return bp;
}

/*
 * place - Place block of asize bytes at start of free block bp
 *         and split if remainder would be at least minimum block size
 */
static void place(void *bp, size_t asize) {
  size_t csize = GET_SIZE(HDRP(bp));
  // printf("place \t");
  // print_block(bp);
  remove_block(bp);

  if ((csize - asize) >= MIN_SIZE) {
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(csize - asize, 0));
    PUT(FTRP(bp), PACK(csize - asize, 0));
    // insert the block as it is a new free block
    insert_block(bp);
  } else {
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
  }
}

/*
 * find_fit - Find a fit for a block with asize bytes
 */
static void *find_fit(size_t asize) {
  /* First fit search */
  // printf("find_fit\n");
  // printAll();
  // get the respective head
  void *head = GET_HEAD_OF(asize);
  void *bp;

  do {
    // printf("Size : %ld Head : %p\n", asize, head);
    // if there is no first element in the list, the loop will not be entered
    for (bp = GET_CONTENT(head); bp; bp = GET_RIGHT(bp)) {
      if (asize <= GET_SIZE(HDRP(bp))) {
        return bp;
      }
    }
    // if no fit block found, go to the next list
    head += OVERHEAD;
    // loop untill it reached the last list
  } while (head < heap_listp - DSIZE);

  return NULL; /* No fit */
}

static void printblock(void *bp) {
  size_t hsize, halloc, fsize, falloc;

  checkheap(0);
  hsize = GET_SIZE(HDRP(bp));
  halloc = GET_ALLOC(HDRP(bp));
  fsize = GET_SIZE(FTRP(bp));
  falloc = GET_ALLOC(FTRP(bp));

  if (hsize == 0) {
    printf("%p: EOL\n", bp);
    return;
  }

  printf("%p: header: [%zu:%c] footer: [%zu:%c]\n", bp, hsize,
         (halloc ? 'a' : 'f'), fsize, (falloc ? 'a' : 'f'));
}

static void checkblock(void *bp) {
  if ((size_t)bp % 8)
    printf("Error: %p is not doubleword aligned\n", bp);
  if (GET(HDRP(bp)) != GET(FTRP(bp)))
    printf("Error: header does not match footer\n");
}

/*
 * checkheap - Minimal check of the heap for consistency
 */
void checkheap(int verbose) {
  char *bp = heap_listp;

  if (verbose)
    printf("Heap (%p):\n", heap_listp);

  if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp)))
    printf("Bad prologue header\n");
  checkblock(heap_listp);

  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if (verbose)
      printblock(bp);
    checkblock(bp);
  }

  if (verbose)
    printblock(bp);
  if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp))))
    printf("Bad epilogue header\n");
}
