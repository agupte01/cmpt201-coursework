#define _POSIX_C_SOURCE 200102L
#include "blockchain.h"
#include <openssl/sha.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

int bc_init(struct blockchain *bc,
            unsigned char difficulty[SHA256_DIGEST_LENGTH]) {
  if (!bc)
    return -1;
  bc->count = 0;
  if (difficulty)
    memcpy(bc->difficulty, difficulty, SHA256_DIGEST_LENGTH);
  else
    memset(bc->difficulty, 0xff, SHA256_DIGEST_LENGTH);
  return 0;
}

static void hash_block_core(const struct block_core *core,
                            unsigned char hash_out[SHA256_DIGEST_LENGTH]) {
  SHA256((const unsigned char *)core, sizeof(struct block_core), hash_out);
}

int bc_add_block(struct blockchain *bc, const unsigned char data[DATA_SIZE]) {
  if (!bc || bc->count >= BLOCKCHAIN_SIZE || !data)
    return -1;

  struct block *new_block = &bc->blocks[bc->count];
  struct block_core *core = &new_block->core;

  core->index = bc->count;
  clock_gettime(CLOCK_REALTIME, &core->timestamp);
  memcpy(core->data, data, DATA_SIZE);

  if (bc->count == 0)
    memset(core->p_hash, 0, SHA256_DIGEST_LENGTH);
  else
    memcpy(core->p_hash, bc->blocks[bc->count - 1].hash, SHA256_DIGEST_LENGTH);

  for (uint32_t nonce = 0;; nonce++) {
    core->nonce = nonce;
    hash_block_core(core, new_block->hash);
    if (memcmp(new_block->hash, bc->difficulty, SHA256_DIGEST_LENGTH) <= 0)
      break;
  }
  size_t added_index = bc->count;
  bc->count++;
  return added_index;
}

int bc_verify(struct blockchain *bc) {
  if (!bc)
    return -1;

  for (size_t i = 0; i < bc->count; i++) {

    unsigned char hash[SHA256_DIGEST_LENGTH];
    hash_block_core(&bc->blocks[i].core, hash);

    if (memcmp(hash, bc->blocks[i].hash, SHA256_DIGEST_LENGTH) != 0)
      return -1;

    if (memcmp(bc->blocks[i].hash, bc->difficulty, SHA256_DIGEST_LENGTH) > 0)
      return -1;

    if (i + 1 < bc->count) {
      if (memcmp(bc->blocks[i].hash, bc->blocks[i + 1].core.p_hash,
                 SHA256_DIGEST_LENGTH) != 0)
        return -1;
    }
  }
  return 0;
}
