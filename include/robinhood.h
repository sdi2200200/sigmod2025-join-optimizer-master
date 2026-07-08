///////////////Robinhood Hash Table Implementation///////////////
#include <iostream>
#include "statement.h"

template <typename Key, typename Value>
struct HashNode {
    Key key;
    Value value;
    bool occupied = false;      //If the node is occupied.
    size_t psl = 0;             //PSL required for Robinhood hashing.
};

template <typename Key, typename Value>
class HashTable {
public:
    size_t size;                                //Size of hash table.
    std::vector<HashNode<Key, Value>> buffer;   //Vector to hold hash nodes.
    size_t occupied_count = 0;                  //Number of occupied nodes.

    //Constructor to initialize hash table with the given size.
    explicit HashTable(size_t s) : size(s) {
        buffer.resize(size);
    }

    size_t hash_func(const Key &k) const {
        std::hash<Key> hasher;
        return hasher(k) % size;
    }

    //Take argument a key and return pointer to value if found, else nullptr.
    Value* find(const Key& key) {
        size_t k = hash_func(key);
        for (size_t i = k; i < size; i++) {
            if (!buffer[i].occupied) return nullptr;    //Empty slot found, key can not exist.
            if (buffer[i].key == key) return &buffer[i].value;
            if (buffer[i].psl < (i - k)) return nullptr;
        }
        return nullptr;
    }

    //Insert key-value pair into hash table.
    void insert(const Key &key, const Value &value) {
        if (occupied_count >= static_cast<size_t>(size * 0.7)) {
            rehash();
        }

        size_t k = hash_func(key);
        size_t psl = 0;
        Key cur_key = key;
        Value cur_value = value;

        for (size_t i = k; i < size; ++i, ++psl) {
            //Empty slot found, insert key-value pair.
            if (!buffer[i].occupied) {
                buffer[i].key = cur_key;
                buffer[i].value = cur_value;
                buffer[i].occupied = true;
                buffer[i].psl = psl;
                ++occupied_count;
                return;
            }
            //Key already exists, do not insert.
            if (buffer[i].key == cur_key) {
                return;
            }
            //Robinhood swapping.
            if (psl > buffer[i].psl) {
                std::swap(cur_key, buffer[i].key);
                std::swap(cur_value, buffer[i].value);
                std::swap(psl, buffer[i].psl);
            }
            //Key hash outside table bounds, rehash and retry insertion.
            if (i == size - 1) {
                rehash();
                return insert(cur_key, cur_value);
            }
        }
        return;
    }

    //Rehash function(puts pair keys,value in a temporary vector, resize and insert again all keys,values).
    void rehash() {
        std::vector<std::pair<Key, Value>> items;
        items.reserve(occupied_count + 1);
        for (size_t i = 0; i < size; ++i) {
            if (buffer[i].occupied) items.emplace_back(buffer[i].key, buffer[i].value);   
        }

        size *= 2;
        buffer.clear();
        buffer.resize(size);
        occupied_count = 0;

        for (auto& it : items) {
            insert(it.first, it.second);
        }
    }
};