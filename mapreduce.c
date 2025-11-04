#include "interface.h"
#include "tests.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INTERMEDIATE_KV (MAX_DATA_SIZE * MAX_THREADS)

static struct {
  char key[MAX_KEY_SIZE];
  char value[MAX_VALUE_SIZE];
} intermediate_buffer[MAX_INTERMEDIATE_KV];
static size_t intermediate_count = 0;
static pthread_mutex_t intermediate_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_FINAL_KV (MAX_DATA_SIZE * MAX_THREADS)

static struct {
  char key[MAX_KEY_SIZE];
  char value[MAX_VALUE_SIZE];
} final_buffer[MAX_FINAL_KV];
static size_t final_count = 0;
static pthread_mutex_t final_mutex = PTHREAD_MUTEX_INITIALIZER;

struct map_task_arg {
  const struct mr_in_kv *kv_lst;
  size_t start_idx;
  size_t end_idx;
  void (*map)(const struct mr_in_kv *);
  ;
};

typedef struct {
  char key[MAX_KEY_SIZE];
  char **values;
  size_t value_count;
} key_group;

struct reduce_task_arg {
  key_group *groups;
  size_t start_idx;
  size_t end_idx;
  void (*reduce)(const struct mr_out_kv *);
};

void *map_thread(void *arg) {
  struct map_task_arg *task = (struct map_task_arg *)arg;
  for (size_t i = task->start_idx; i < task->end_idx; ++i) {
    task->map(&(task->kv_lst[i]));
  }
  return NULL;
}

void *reduce_thread(void *arg) {
  struct reduce_task_arg *task = (struct reduce_task_arg *)arg;

  for (size_t i = task->start_idx; i < task->end_idx; ++i) {
    struct mr_out_kv outkv;
    strncpy(outkv.key, task->groups[i].key, MAX_KEY_SIZE);
    outkv.key[MAX_KEY_SIZE - 1] = '\0';
    outkv.count = task->groups[i].value_count;
    outkv.value = malloc(outkv.count * MAX_VALUE_SIZE);
    for (size_t k = 0; k < outkv.count; ++k) {
      strncpy(outkv.value[k], task->groups[i].values[k], MAX_VALUE_SIZE);
      outkv.value[k][MAX_VALUE_SIZE - 1] = '\0';
    }
    task->reduce(&outkv);
    free(outkv.value);
  }
  return NULL;
}

int mr_emit_i(const char *key, const char *value) {
  pthread_mutex_lock(&intermediate_mutex);
  if (intermediate_count >= MAX_INTERMEDIATE_KV) {
    pthread_mutex_unlock(&intermediate_mutex);
    return -1;
  }
  strncpy(intermediate_buffer[intermediate_count].key, key, MAX_KEY_SIZE);
  intermediate_buffer[intermediate_count].key[MAX_KEY_SIZE - 1] = '\0';
  strncpy(intermediate_buffer[intermediate_count].value, value, MAX_VALUE_SIZE);
  intermediate_buffer[intermediate_count].value[MAX_VALUE_SIZE - 1] = '\0';
  intermediate_count++;
  pthread_mutex_unlock(&intermediate_mutex);
  return 0;
}

int mr_emit_f(const char *key, const char *value) {
  pthread_mutex_lock(&final_mutex);
  if (final_count >= MAX_FINAL_KV) {
    pthread_mutex_unlock(&final_mutex);
    return -1;
  }
  strncpy(final_buffer[final_count].key, key, MAX_KEY_SIZE);
  final_buffer[final_count].key[MAX_KEY_SIZE - 1] = '\0';
  strncpy(final_buffer[final_count].value, value, MAX_VALUE_SIZE);
  final_buffer[final_count].value[MAX_VALUE_SIZE - 1] = '\0';
  final_count++;
  pthread_mutex_unlock(&final_mutex);
  return 0;
}

int compare_kv(const void *a, const void *b) {
  const char *ka = ((const char *)a);
  const char *kb = ((const char *)b);
  return strncmp(ka, kb, MAX_KEY_SIZE);
}
int outcompare(const void *a, const void *b) {
  const struct mr_out_kv *kvA = a;
  const struct mr_out_kv *kvB = b;
  return strcmp(kvA->key, kvB->key);
}
int mr_exec(const struct mr_input *input, void (*map)(const struct mr_in_kv *),
            size_t mapper_count, void (*reduce)(const struct mr_out_kv *),
            size_t reducer_count, struct mr_output *output) {

  intermediate_count = 0;
  final_count = 0;

  size_t total_input = input->count;
  size_t base_chunk = total_input / mapper_count;
  size_t remainder = total_input % mapper_count;

  size_t map_task_starts[mapper_count];
  size_t map_task_ends[mapper_count];

  size_t idx = 0;
  for (size_t i = 0; i < mapper_count; i++) {
    map_task_starts[i] = idx;
    size_t chunk_size = base_chunk + (i < remainder ? 1 : 0);
    map_task_ends[i] = idx + chunk_size;
    idx += chunk_size;
  }

  pthread_t mapper_threads[mapper_count];
  struct map_task_arg map_args[mapper_count];

  for (size_t i = 0; i < mapper_count; ++i) {
    map_args[i].kv_lst = input->kv_lst;
    map_args[i].start_idx = map_task_starts[i];
    map_args[i].end_idx = map_task_ends[i];
    map_args[i].map = map;
    if (pthread_create(&mapper_threads[i], NULL, map_thread, &map_args[i]) !=
        0) {
      return -1;
    }
  }

  for (size_t i = 0; i < mapper_count; ++i) {
    pthread_join(mapper_threads[i], NULL);
  }

  qsort(intermediate_buffer, intermediate_count, sizeof(intermediate_buffer[0]),
        compare_kv);

  key_group *groups = malloc(intermediate_count * sizeof(key_group));
  size_t group_count = 0;

  idx = 0;
  while (idx < intermediate_count) {
    size_t j = idx + 1;
    while (j < intermediate_count &&
           strncmp(intermediate_buffer[idx].key, intermediate_buffer[j].key,
                   MAX_KEY_SIZE) == 0) {
      j++;
    }

    groups[group_count].value_count = j - idx;
    strncpy(groups[group_count].key, intermediate_buffer[idx].key,
            MAX_KEY_SIZE);
    groups[group_count].key[MAX_KEY_SIZE - 1] = '\0';
    groups[group_count].values =
        malloc(groups[group_count].value_count * sizeof(char *));
    for (size_t k = 0; k < groups[group_count].value_count; ++k) {
      groups[group_count].values[k] = malloc(MAX_VALUE_SIZE);
      strncpy(groups[group_count].values[k], intermediate_buffer[idx + k].value,
              MAX_VALUE_SIZE);
      groups[group_count].values[k][MAX_VALUE_SIZE - 1] = '\0';
      /*size_t len = strlen(intermediate_buffer[idx + k].value) + 1;
      groups[group_count].values[k] = malloc(len);
      strncpy(groups[group_count].values[k], intermediate_buffer[idx + k].value,
              len);
      */
    }
    group_count++;
    idx = j;
  }

  size_t reduce_base_chunk = group_count / reducer_count;
  size_t reduce_remainder = group_count % reducer_count;

  size_t reduce_starts[reducer_count];
  size_t reduce_ends[reducer_count];

  size_t reduce_idx = 0;
  for (size_t i = 0; i < reducer_count; ++i) {
    reduce_starts[i] = reduce_idx;
    size_t chunk_size = reduce_base_chunk + (i < reduce_remainder ? 1 : 0);
    reduce_ends[i] = reduce_idx + chunk_size;
    reduce_idx += chunk_size;
  }

  pthread_t reducer_threads[reducer_count];
  struct reduce_task_arg reduce_args[reducer_count];
  for (size_t i = 0; i < reducer_count; ++i) {
    reduce_args[i].groups = groups;
    reduce_args[i].start_idx = reduce_starts[i];
    reduce_args[i].end_idx = reduce_ends[i];
    reduce_args[i].reduce = reduce;
    if (pthread_create(&reducer_threads[i], NULL, reduce_thread,
                       &reduce_args[i]) != 0)
      return -1;
  }
  for (size_t i = 0; i < reducer_count; ++i) {
    pthread_join(reducer_threads[i], NULL);
  }

  output->count = final_count;
  output->kv_lst = calloc(final_count, sizeof(struct mr_out_kv));
  for (size_t i = 0; i < final_count; ++i) {
    strncpy(output->kv_lst[i].key, final_buffer[i].key, MAX_KEY_SIZE);
    output->kv_lst[i].key[MAX_KEY_SIZE - 1] = '\0';
    output->kv_lst[i].count = 1; // groups[i].value_count;
    output->kv_lst[i].value = malloc(MAX_VALUE_SIZE);
    strncpy(output->kv_lst[i].value[0], final_buffer[i].value, MAX_VALUE_SIZE);
    output->kv_lst[i].value[0][MAX_VALUE_SIZE - 1] = '\0';
    /*output->kv_lst[i].value = malloc(groups[i].value_count * MAX_VALUE_SIZE);
    for (size_t j = 0; j < groups[i].value_count; ++j) {
      strncpy(output->kv_lst[i].value[j], groups[i].values[j], MAX_VALUE_SIZE);
      output->kv_lst[i].value[j][MAX_VALUE_SIZE - 1] = '\0';
    }*/
  }

  qsort(output->kv_lst, output->count, sizeof(struct mr_out_kv), outcompare);

  for (size_t i = 0; i < group_count; ++i) {
    for (size_t k = 0; k < groups[i].value_count; ++k) {
      free(groups[i].values[k]);
    }
    free(groups[i].values);
  }
  return 0;
}
