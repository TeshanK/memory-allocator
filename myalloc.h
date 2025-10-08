#ifndef MYALLOC_H
#define MYALLOC_H

#include <stddef.h>

// Memory alignment requirement
#define ALIGNMENT 32

typedef char ALIGN[ALIGNMENT]; // Used to align free_block

typedef union free_block {
  struct {
    size_t size;      // Usable space of the block
    int is_allocated; // 1 if allocated, 0 if free
    union free_block *prev;
    union free_block *next;
  };
  ALIGN stub; // Force 32-byte alignment
} free_block;

// Size of the block header
static const size_t FBLOCKSIZE = sizeof(free_block);

// Minimum remainder size after splitting a block
static const size_t MIN_SPLIT_REMAINDER = FBLOCKSIZE + sizeof(void *);

// Useful mask for rounding sizes to ALIGNMENT
static const size_t ALIGNMENT_MASK = ALIGNMENT - 1;

// Helpers to convert between block header and user payload pointer
#define PAYLOAD(b) ((void *)((char *)b + FBLOCKSIZE))
#define HEADER(p) ((free_block *)((void *)(char *)p - FBLOCKSIZE))


// Allocate `size` bytes (returns NULL on failure)
void *myalloc(size_t size);

// Free a pointer returned by myalloc
void myfree(void *ptr);

// Change the size of the memory block pointed by ptr
void *myrealloc(void *ptr, size_t size);

#endif // MYALLOC_H
