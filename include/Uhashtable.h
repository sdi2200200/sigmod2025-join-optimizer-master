/*Unchained hash table*/
#include <cstdint>
#include <vector>
#include <pthread.h>
#include <atomic>

#define NUM_THREADS 16
#define NUM_PARTITIONS 16
#define PARTITION_BITS 4
#define CHUNK_SIZE (64 * 2048)
#define ENTRIES_PER_CHUNK (CHUNK_SIZE / sizeof(HTEntry))  //1024 entries per chunk.

struct HTEntry {
    int32_t key;        //key.
    uint32_t key_pos;   //Key position.
};

struct Chunk {
    HTEntry data[ENTRIES_PER_CHUNK];
    size_t count;
    Chunk* next;
    
    Chunk() : count(0), next(nullptr) {}
    
    bool isFull() const {
        return count == ENTRIES_PER_CHUNK;
    }
    
    void insert(int32_t key, uint32_t key_pos) {
        data[count].key = key;
        data[count].key_pos = key_pos;
        count++;
    }
};

struct Partitions {
    Chunk* chunk_list;    //Linked list of chunks containing the entries.
    size_t total_count;   //Total number of tuples entries.
    
    Partitions() : chunk_list(nullptr), total_count(0) {}
};

struct ThreadLocalData {
    Partitions partitions[NUM_PARTITIONS];

    ~ThreadLocalData() {
        for (int i = 0; i < NUM_PARTITIONS; i++) {
            Chunk* chunk = partitions[i].chunk_list;
            while (chunk) {
                Chunk* next = chunk->next;
                delete chunk;
                chunk = next;
            }
        }
    }
};

class UnchainedHashTable {
public:
    //Temporary tuple vector to store keys and positions before building the hash table.
    std::vector<std::tuple<int32_t, uint32_t>> temp_keys_pos;
    //Insert key and position into temp_keys_pos.
    void insertTempKey(int32_t key, uint32_t key_pos){
        temp_keys_pos.emplace_back(key, key_pos);
    }
    //vector to keep entries of the hash table.
    std::vector<HTEntry> entries;
    //vector to keep directory of the hash table.
    std::vector<uint64_t> directory;
    //size of directory(2^16)
    size_t dir_size;

    //Constructor.
    UnchainedHashTable();

    //Build directory and entries from temp_keys_pos.
    void build(size_t count);

    //Build directory and entries from temp_keys_pos(parallel).
    void build_parallel(size_t count);

    //Probe phase.
    //Return in the out vector the positions of matching keys.
    void probe(int32_t probe_key, std::vector<uint32_t>& out) const;

    //Hash fnction using mm_crc32_u32.
    uint64_t hashKey(int32_t key) const;

    //Get partition number from hash.
    int getPartition(int32_t key) const {
        uint64_t hash = hashKey(key);
        return (hash >> (64 - PARTITION_BITS));
    }

    //dirHashId to hash keys in directory.
    uint16_t dirHashId(uint64_t hash) const;

    //Build bloom filter.
    uint16_t bloomFilter(size_t start, size_t end) const;

    //Make directory entry: fisrt 48 bits = pointer, last 16 bits = bloom filter.
    inline uint64_t makeDirectoryEntry(uint64_t pointer, uint16_t bloom) const;
    //Take pointer(first 48 bits) from directory entry.
    inline uint64_t extractPointer(uint64_t entry) const;
    //Take bloom filter(last 16 bits) from directory entry.
    inline uint16_t extractBloom(uint64_t entry) const;

    //Bits to make directory size to be 2^16
    static constexpr uint32_t DIRECTORY_SIZE_BITS = 16;
    //Directory size = 2^16.
    static constexpr uint32_t DIRECTORY_SIZE = 1ULL << 16;
    //Bloom filter mask to extract last 16 bits.
    static constexpr uint64_t BLOOM_MASK = 0xFFFFULL;
};