#define _POSIX_C_SOURCE 200809L
#include "alloc.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

void *sbrk(intptr_t increment);
static struct header *free_list = NULL;
static enum algs alloc_algo = FIRST_FIT;
static uint64_t heap_limit = 0;
static void *heap_start = NULL;
static uint64_t current_heap_size = 0;

static int align8(int size) { return ((size + 7) / 8) * 8; }

void *alloc(int size) {

  size = align8(size);

  if (size <= 0)
    return NULL;

  if (free_list == NULL) {
    // void *sbrk(intptr_t increment);
    void *block = sbrk(INCREMENT);
    if (block == (void *)-1)
      return NULL;

    heap_start = block;
    current_heap_size = INCREMENT;

    struct header *first = (struct header *)block;
    first->size = INCREMENT;
    first->next = NULL;

    free_list = first;
  }

  struct header *prev = NULL;
  struct header *curr = free_list;
  struct header *best = NULL;
  struct header *best_prev = NULL;

  if (alloc_algo == FIRST_FIT) {
    while (curr) {
      if (curr->size >= size + sizeof(struct header)) {
        break;
      }
      prev = curr;
      curr = curr->next;
    }
  } else if (alloc_algo == BEST_FIT) {

    int64_t best_size = UINT64_MAX;
    while (curr) {
      if (curr->size >= size + sizeof(struct header) &&
          curr->size < best_size) {
        best = curr;
        best_prev = prev;
        best_size = curr->size;
      }
      prev = curr;
      curr = curr->next;
    }
  } else if (alloc_algo == WORST_FIT) {

    uint64_t worst_size = 0;
    while (curr) {
      if (curr->size >= size + sizeof(struct header) &&
          curr->size > worst_size) {
        best = curr;
        best_prev = prev;
        worst_size = curr->size;
      }
      prev = curr;
      curr = curr->next;
    }
    curr = best;
    prev = best_prev;
  }

  while (curr) {
    if (curr->size >= size + sizeof(struct header) + 8) {

      struct header *new_block =
          (struct header *)((char *)curr + sizeof(struct header) + size);
      new_block->size = curr->size - size - sizeof(struct header);
      new_block->next = curr->next;
      curr->size = size + sizeof(struct header);
      curr->next = NULL;

      if (prev)
        prev->next = new_block;
      else
        free_list = new_block;
    } else {

      if (prev)
        prev->next = curr->next;
      else
        free_list = curr->next;
    }
    return (void *)((char *)curr + sizeof(struct header));
  }

  if (heap_limit == 0 || current_heap_size + INCREMENT <= heap_limit) {
    void *new_block = sbrk(INCREMENT);
    if (new_block == (void *)-1)
      return NULL;

    current_heap_size += INCREMENT;

    struct header *new_header = (struct header *)new_block;
    new_header->size = INCREMENT;
    new_header->next = free_list;
    free_list = new_header;

    return alloc(size);
  }
  return NULL;
}

void dealloc(void *ptr) {

  if (ptr == NULL)
    return;

  struct header *block = (struct header *)((char *)ptr - sizeof(struct header));

  block->next = free_list;
  free_list = block;
}

void allocopt(enum algs algorithm, int limit) {
  alloc_algo = algorithm;
  heap_limit = limit;
}

struct allocinfo allocinfo(void) {
  struct allocinfo info;
  info.free_size = 0;
  info.free_chunks = 0;
  info.largest_free_chunk_size = 0;
  info.smallest_free_chunk_size = 0;

  struct header *curr = free_list;
  while (curr) {

    uint64_t chunk_size = curr->size - sizeof(struct header);
    info.free_size = chunk_size;
    info.free_chunks++;

    if (chunk_size > info.largest_free_chunk_size) {
      info.largest_free_chunk_size = chunk_size;
    }
    if (chunk_size < info.smallest_free_chunk_size) {
      info.smallest_free_chunk_size = chunk_size;
    }

    curr = curr->next;
  }

  if (info.free_chunks == 0) {
    info.smallest_free_chunk_size = 0;
  }

  return info;
}
