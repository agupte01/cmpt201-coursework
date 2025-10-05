#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BLOCK_SIZE 128
#define EXTRA_SIZE (2 * BLOCK_SIZE)
#define BUF_SIZE 100

struct header {
  uint64_t size;
  struct header *next;
};

void handle_error(const char *msg) { write(STDERR_FILENO, msg, strlen(msg)); }

void print_out(char *format, void *data, size_t data_size) {
  char buf[BUF_SIZE];
  ssize_t len = snprintf(buf, BUF_SIZE, format,
                         data_size == sizeof(uint64_t) ? *(uint64_t *)data
                                                       : *(void **)data);
  if (len < 0) {
    handle_error("snprintf error\n");
  }
  write(STDOUT_FILENO, buf, len);
}

extern void *sbrk(intptr_t increment);

int main() {

  void *new_space = sbrk(EXTRA_SIZE);
  if (new_space == (void *)-1) {
    handle_error("sbrk error\n");
  }

  struct header *first_block = (struct header *)new_space;
  struct header *second_block =
      (struct header *)((char *)new_space + BLOCK_SIZE);

  first_block->size = BLOCK_SIZE;
  first_block->next = NULL;
  second_block->size = BLOCK_SIZE;
  second_block->next = first_block;

  char *first_data = (char *)first_block + sizeof(struct header);
  char *second_data = (char *)second_block + sizeof(struct header);
  size_t data_size = BLOCK_SIZE - sizeof(struct header);

  memset(first_data, 0, data_size);
  memset(second_data, 1, data_size);

  print_out("first block:         %p\n", (void *)&first_block, sizeof(void *));
  print_out("second block:        %p\n", (void *)&second_block, sizeof(void *));
  print_out("first block size:    %lu\n", &first_block->size, sizeof(uint64_t));
  print_out("first block next:    %p\n", &first_block->next, sizeof(void *));
  print_out("second block size:   %lu\n", &second_block->size,
            sizeof(uint64_t));
  print_out("second block next:   %p\n", &second_block->next, sizeof(void *));

  for (size_t i = 0; i < data_size; ++i) {
    uint64_t v = (uint64_t)(unsigned char)first_data[i];
    print_out("%lu\n", &v, sizeof(uint64_t));
  }
  for (size_t i = 0; i < data_size; ++i) {
    uint64_t v = (uint64_t)(unsigned char)second_data[i];
    print_out("%lu\n", &v, sizeof(uint64_t));
  }
  return 0;
}
