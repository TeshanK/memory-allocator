#include "myalloc.h"

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static pthread_mutex_t heap_lock = PTHREAD_MUTEX_INITIALIZER;

// Head of the sorted free-list (by address)
static free_block *head = NULL;

// The initial program break when allocator starts
static void *heap_start = NULL;

// Whether heap_start has been captured
static int heap_initialized = 0;

// Internal helper functions
static void init_heap_if_needed(void);
static void insert_to_list(free_block *new);
static void remove_from_list(free_block *block);
static free_block *coalesce(free_block *block);
static free_block *split_block(free_block *block, size_t size);

// Capture initial program break lazily
static void init_heap_if_needed(void) {
  if (!heap_initialized) {
    heap_start = sbrk(0);
    heap_initialized = 1;
  }
}

// Insert `new` into the free list and mark unallocated
static void insert_to_list(free_block *new) {
  new->prev = new->next = NULL;

  if (head == NULL) {
    head = new;
  } else {
    free_block *curr = head;
    free_block *prev = NULL;

    while (curr && (char *)curr < (char *)new) {
      prev = curr;
      curr = curr->next;
    }

    if (prev == NULL) {
      // Insert at head
      new->next = head;
      head->prev = new;
      head = new;
    } else if (curr == NULL) {
      // Insert at tail
      prev->next = new;
      new->prev = prev;
    } else {
      // Insert in middle
      new->prev = prev;
      new->next = curr;
      prev->next = new;
      curr->prev = new;
    }
  }

  new = coalesce(new);
  new->is_allocated = 0;
}

// Remove `block` from the free list and mark allocated
static void remove_from_list(free_block *block) {
  if (head == NULL || block == NULL) {
    return;
  }

  if (block == head) {
    head = block->next;
    if (head)
      head->prev = NULL;
  } else {
    if (block->next)
      block->next->prev = block->prev;
    if (block->prev)
      block->prev->next = block->next;
  }

  block->prev = block->next = NULL;
  block->is_allocated = 1;
}

// Try to merge adjacent free blocks around `block` and return the resulting
// block
static free_block *coalesce(free_block *block) {
  free_block *next_block = block->next;
  free_block *prev_block = block->prev;

  if (next_block &&
      (char *)block + FBLOCKSIZE + block->size == (char *)next_block) {
    block->next = next_block->next;
    block->size += next_block->size + FBLOCKSIZE;
    if (next_block->next) {
      next_block->next->prev = block;
    }
  }

  if (prev_block &&
      (char *)prev_block + FBLOCKSIZE + prev_block->size == (char *)block) {
    prev_block->next = block->next;
    prev_block->size += block->size + FBLOCKSIZE;
    if (next_block) {
      next_block->prev = prev_block;
    }
    block = prev_block;
  }

  return block;
}

// Split `block` into an allocated-sized prefix of `size` and a leftover free
// block; insert the leftover into the free list. Returns the block to use for
// allocation (the prefix).
static free_block *split_block(free_block *block, size_t size) {
  size_t leftover_size = block->size - size - FBLOCKSIZE;
  if (leftover_size < MIN_SPLIT_REMAINDER) {
    return block;
  }

  block->size = size;

  // Shrink the original block to the requested size
  free_block *new = (free_block *)((char *)block + (size + FBLOCKSIZE));
  new->size = leftover_size;

  new->prev = new->next = NULL;
  insert_to_list(new);

  return block;
}

// Allocate `size` bytes (returns NULL on failure)
static void *internal_myalloc(size_t size) {
  if (size <= 0) {
    return NULL;
  }

  init_heap_if_needed();

  // Round the size to nearest alignment bytes
  size = (size + ALIGNMENT_MASK) & ~ALIGNMENT_MASK;

  free_block *curr = head;
  free_block *prev = NULL;

  // Check if a block of suitable size already exists in the free list
  while (curr) {
    prev = curr;
    if (curr->size >= size && curr->size < size + MIN_SPLIT_REMAINDER) {
      remove_from_list(curr);
      return PAYLOAD(curr);
    } else if (curr->size >= size) {
      remove_from_list(curr);
      free_block *new = split_block(curr, size);
      return PAYLOAD(new);
    }
    curr = curr->next;
  }

  // If no suitable block exits allocate a new block
  // If there is a free block in the list extend it
  void *region = NULL;
  free_block *new = NULL;

  void *prog_break = sbrk(0);

  if (prev) {
    char *prev_end = (char *)prev + prev->size + FBLOCKSIZE;
    // Extend only if touching program break
    if (prev_end == (char *)prog_break) {
      if (sbrk(size - prev->size) == (void *)-1)
        return NULL;
      prev->size = size;
      remove_from_list(prev);
      new = prev;
    } else {
      if (size + FBLOCKSIZE > INTPTR_MAX) {
        return NULL;
      }
      region = sbrk(size + FBLOCKSIZE);
      if (region == (void *)-1) {
        return NULL;
      }
      new = (free_block *)region;
      new->size = size;
      new->is_allocated = 1;
    }
  } else {
    if (size + FBLOCKSIZE > INTPTR_MAX) {
      return NULL;
    }
    region = sbrk(size + FBLOCKSIZE);
    if (region == (void *)-1) {
      return NULL;
    }
    new = (free_block *)region;
    new->size = size;
    new->is_allocated = 1;
  }

  return PAYLOAD(new);
}

void *myalloc(size_t size) {
  pthread_mutex_lock(&heap_lock);
  void *ptr = internal_myalloc(size);
  pthread_mutex_unlock(&heap_lock);
  return ptr;
}

// Free a block and insert into the free list
void myfree(void *ptr) {
  if (ptr == NULL)
    return;

  pthread_mutex_lock(&heap_lock);
  free_block *blk = (free_block *)ptr;
  if (!blk->is_allocated) {
    pthread_mutex_unlock(&heap_lock);
    fprintf(stderr, "double free not allowed\n");
    exit(1);
  }
  insert_to_list(blk);
  pthread_mutex_unlock(&heap_lock);
}

// Debug: print addresses of free-list blocks
void print_list() {
  free_block *curr = head;
  if (head == NULL) {
    return;
  }

  while (curr) {
    printf("%p: -> ", curr);
    curr = curr->next;
  }
  printf("\n");
}
