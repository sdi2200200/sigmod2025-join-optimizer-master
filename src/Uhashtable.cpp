#include "Uhashtable.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <nmmintrin.h>

UnchainedHashTable::UnchainedHashTable() {
    dir_size = DIRECTORY_SIZE;
    directory.resize(dir_size, 0);
}

uint64_t UnchainedHashTable::hashKey(int32_t key) const {
    uint32_t k = static_cast<uint32_t>(key);
    uint32_t lo = _mm_crc32_u32(0xffffffffu, k);
    uint32_t hi = _mm_crc32_u32(0xfeedbeefu, k);
    uint64_t h = ((uint64_t)hi << 32) | (uint64_t)lo;
    return h;
}

uint16_t UnchainedHashTable::dirHashId(uint64_t hash) const {
    return (uint16_t)(hash >> (64 - DIRECTORY_SIZE_BITS));  //Keep the last 48 bits to hash inside directory.
}

uint64_t UnchainedHashTable::makeDirectoryEntry(uint64_t pointer, uint16_t bloom) const {
    return (pointer << 16) | (uint64_t)bloom;
}

uint64_t UnchainedHashTable::extractPointer(uint64_t entry) const {
    return (entry >> 16);
}

uint16_t UnchainedHashTable::extractBloom(uint64_t entry) const {
    return (uint16_t)(entry & BLOOM_MASK);
}

uint16_t UnchainedHashTable::bloomFilter(size_t start, size_t end) const {
    uint16_t filter = 0;

    for (size_t i = start; i < end; i++) {
        //Using 4 hash functions to utilize all 16 bits of bloom filter.
        uint16_t b1 = entries[i].key & 0xF;
        uint16_t b2 = (entries[i].key >> 4) & 0xF;
        uint16_t b3 = (entries[i].key >> 8) & 0xF;
        uint16_t b4 = (entries[i].key >> 12) & 0xF;
        filter |= (1u << b1);
        filter |= (1u << b2);
        filter |= (1u << b3);
        filter |= (1u << b4);
    }

    return filter;
}

void UnchainedHashTable::build(size_t count) {
    //Create a temporary struct to sort entries by dir_hash_id.
    struct TempVec {
        uint16_t dir_hash_id;
        HTEntry entry;
    };
    //Temporary vector to sort entries by dir_hash_id.
    std::vector<TempVec> tmp;
    tmp.reserve(count);

    for (size_t i = 0; i < count; i++) {
        int32_t key = std::get<0>(temp_keys_pos[i]);
        uint32_t row = std::get<1>(temp_keys_pos[i]);
        uint64_t h = hashKey(key);
        
        HTEntry e;
        e.key = key;
        e.key_pos = row;
        
        tmp.push_back({ dirHashId(h), e });
    }

    std::sort(tmp.begin(), tmp.end(),
              [](const TempVec& a, const TempVec& b){
                  return a.dir_hash_id < b.dir_hash_id;
              });

    entries.clear();
    entries.reserve(count);
    for (size_t i = 0; i < count; i++) {
        entries.push_back(tmp[i].entry);
    }
    //Initialize directory with pointers to point at the end of entries vector.
    std::fill(directory.begin(), directory.end(), makeDirectoryEntry(count, 0));

    size_t idx = 0;
    while (idx < count) {

        uint16_t p = tmp[idx].dir_hash_id;
        
        size_t start = idx;
        //Find the range of entries that the directory will point to.
        while (idx < count && tmp[idx].dir_hash_id == p) {
            idx++;
        }
        size_t end = idx;

        uint16_t bloom = bloomFilter(start, end);

        uint64_t pointer = start;
        directory[p] = makeDirectoryEntry(pointer, bloom);
    }
    //Fill the empty directory entries to point to the next non-empty entry.
    uint64_t next = makeDirectoryEntry(count, 0);
    for (size_t p = dir_size; p-- > 0;) {
        if ((directory[p] >> 16) == count) {
            directory[p] = next;
        } else {
            next = directory[p];
        }
    }
}

struct PartitionThreadArgs {
    UnchainedHashTable* ht;
    ThreadLocalData* thread_data;
    int start;
    int end;
};

void* partition_worker(void* arg) {
    PartitionThreadArgs* args = (PartitionThreadArgs*)arg;
    UnchainedHashTable* ht = args->ht;
    ThreadLocalData* data = args->thread_data;

    for (int i = args->start; i < args->end; i++) {

        auto [key, pos] = ht->temp_keys_pos[i];
        int partition = ht->getPartition(key);
        Partitions* part = &data->partitions[partition];

        if (part->chunk_list == nullptr) {
            part->chunk_list = new Chunk();
        }

        Chunk* current = part->chunk_list;

        if (current->isFull()) {
            Chunk* new_chunk = new Chunk();
            new_chunk->next = current;
            part->chunk_list = new_chunk;
            current = new_chunk;
        }
        current->insert(key, pos);
        part->total_count++;
    }
    return nullptr;
}

struct MergeWorkerArgs {
    int partition_id;
    ThreadLocalData* all_thread_data;
    int num_threads;
    Partitions* global_partitions;
    int start;
    int end;
};

static void* merge_worker(void* arg) {
    MergeWorkerArgs* args = (MergeWorkerArgs*)arg;
    
    for (int i = args->start; i < args->end; i++) {
        int partition_id = i;
        Partitions* global_part = &args->global_partitions[partition_id];
        for (int j = 0; j < args->num_threads; j++) {

            Partitions* thread_part = &args->all_thread_data[j].partitions[partition_id];

            if (thread_part->chunk_list != nullptr) {
                if (global_part->chunk_list == nullptr) {
                    global_part->chunk_list = thread_part->chunk_list;
                } else {
                    Chunk* new_chunk = global_part->chunk_list;
                    while (new_chunk->next != nullptr) {
                        new_chunk = new_chunk->next;
                    }
                    new_chunk->next = thread_part->chunk_list;
                }
                global_part->total_count += thread_part->total_count;
                thread_part->chunk_list = nullptr;
                thread_part->total_count = 0;
            }
        }
    }
    return nullptr;
}

void UnchainedHashTable::build_parallel(size_t count) {
    //Phase 1(each thread have its own local partitions).
    ThreadLocalData* thread_data = new ThreadLocalData[NUM_THREADS];
    pthread_t* threads = new pthread_t[NUM_THREADS];
    PartitionThreadArgs* args = new PartitionThreadArgs[NUM_THREADS];

    size_t keys_per_thread = count / NUM_THREADS;
    size_t remainder = count % NUM_THREADS;

    size_t start = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        size_t thread_count = keys_per_thread + (i < remainder ? 1 : 0);
        args[i].ht = this;
        args[i].thread_data = &thread_data[i];
        args[i].start = start;
        args[i].end = start + thread_count;

        pthread_create(&threads[i], NULL, partition_worker, &args[i]);

        start += thread_count;
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    };
    
    //Phase 2(merge thread local partitions to a single global partition array).
    Partitions* global_partitions = new Partitions[NUM_PARTITIONS];
    MergeWorkerArgs* merge_args = new MergeWorkerArgs[NUM_PARTITIONS];

    size_t num_partitions_per_thread = NUM_PARTITIONS / NUM_THREADS;
    size_t partition_remainder = NUM_PARTITIONS % NUM_THREADS;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        if (i < partition_remainder) {
            merge_args[i].start = i * (num_partitions_per_thread + 1);
            merge_args[i].end = merge_args[i].start + (num_partitions_per_thread + 1);
        } else {
            merge_args[i].start = i * num_partitions_per_thread + partition_remainder;
            merge_args[i].end = merge_args[i].start + num_partitions_per_thread;
        }
        merge_args[i].all_thread_data = thread_data;
        merge_args[i].num_threads = NUM_THREADS;
        merge_args[i].global_partitions = global_partitions;
        
        pthread_create(&threads[i], NULL, merge_worker, &merge_args[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    //Phase 3(build hash table).
    std::fill(directory.begin(), directory.end(), makeDirectoryEntry(count, 0));
    std::vector<uint64_t> counts(dir_size, 0);

    for (int i = 0; i < NUM_PARTITIONS; i++) {
        Chunk* chunk = global_partitions[i].chunk_list;
        while (chunk != nullptr) {
            for (size_t j = 0; j < chunk->count; j++) {
                int32_t key = chunk->data[j].key;
                uint64_t hash = hashKey(chunk->data[j].key);
                uint16_t slot = dirHashId(hash);
                counts[slot]++;
                uint16_t bloom_mask = (1u << (key & 0xF)) |
                                     (1u << ((key >> 4) & 0xF)) |
                                     (1u << ((key >> 8) & 0xF)) |
                                     (1u << ((key >> 12) & 0xF));
                directory[slot] |= bloom_mask;
            }
            chunk = chunk->next;
        }
    }

    entries.resize(count);
    uint64_t pos = 0;
    for (size_t i = 0; i < dir_size; i++) {
        uint16_t bloom = directory[i] & BLOOM_MASK;
        directory[i] = makeDirectoryEntry(pos, bloom);
        pos += counts[i];
        counts[i] = 0;
    }

    for (int i = 0; i < NUM_PARTITIONS; i++) {
        Chunk* chunk = global_partitions[i].chunk_list;
        while (chunk != nullptr) {
            for (size_t j = 0; j < chunk->count; j++) {
                uint64_t hash = hashKey(chunk->data[j].key);
                uint16_t slot = dirHashId(hash);
                uint64_t start = directory[slot] >> 16;
                
                entries[start + counts[slot]] = chunk->data[j];
                counts[slot]++;
            }
            chunk = chunk->next;
        }
    }

    //Fill the empty directory entries to point to the next non-empty entry.
    uint64_t next = makeDirectoryEntry(count, 0);
    for (size_t p = dir_size; p-- > 0;) {
        if ((directory[p] >> 16) == count) {
            directory[p] = next;
        } else {
            next = directory[p];
        }
    }

    for (int i = 0; i < NUM_PARTITIONS; i++) {
        Chunk* chunk = global_partitions[i].chunk_list;
        while (chunk) {
            Chunk* next_chunk = chunk->next;
            delete chunk;
            chunk = next_chunk;
        }
    }
    delete[] thread_data;
    delete[] threads;
    delete[] args;
    delete[] merge_args;
    delete[] global_partitions;
}

void UnchainedHashTable::probe(int32_t probe_key, std::vector<uint32_t>& out) const {
    uint64_t h = hashKey(probe_key);
    uint16_t p = dirHashId(h);
    uint64_t entry = directory[p];
    uint64_t pointer = entry >> 16;
    uint16_t bloom = entry & BLOOM_MASK;

    if (pointer >= entries.size()) return;

    uint16_t bloom_mask = (1u << (probe_key & 0xF)) |
                         (1u << ((probe_key >> 4) & 0xF)) |
                         (1u << ((probe_key >> 8) & 0xF)) |
                         (1u << ((probe_key >> 12) & 0xF));
    if ((bloom & bloom_mask) != bloom_mask) {
        return;
    }

    size_t start = pointer;
    size_t end;
    if (p == (dir_size - 1)) {
        end = entries.size();
    } else {
        end = directory[p + 1] >> 16;
    }

    for (size_t i = start; i < end; i++) {
        if (entries[i].key == probe_key) {
            out.push_back(entries[i].key_pos);
        }
    }
}