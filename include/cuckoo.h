///////////////Cuckoo Hash Table Implementation///////////////
#include <iostream>
#include "statement.h"

template <typename Key, typename Value>
struct HashNode {
    Key key;
    Value value;
    bool occupied = false;
};

template <typename Key, typename Value>
class HashTable {
public:
    size_t size;                                //Size of hash tables.
    std::vector<HashNode<Key, Value>> T1;       //Vectors to hold hash nodes.
    std::vector<HashNode<Key, Value>> T2;
    size_t occupied_count = 0;                  //Number of occupied nodes.

    //Constructor to initialize hash tables with the given size.
    explicit HashTable(size_t s) : size(s) {
        T1.resize(size);
        T2.resize(size);
    }
    //First hash function needed for cuckoo hashing.
    size_t hash_func(const Key &k) const {
        std::hash<Key> hasher;
        return hasher(k) % size;
    }
    //Second hash function needed for cuckoo hashing.
    size_t hash_func2(const Key &k) const {
        std::hash<Key> hasher;
        size_t x = static_cast<size_t>(hasher(k));
        return (x / size) % size;
    }

    //Take argument a key and return a pointer to the value if found, else nullptr.
    Value* find(const Key& key) {
        size_t k = hash_func(key);
        size_t k2 = hash_func2(key);

        if (T1[k].occupied && T1[k].key == key) return &T1[k].value;
        if (T2[k2].occupied && T2[k2].key == key) return &T2[k2].value;

        return nullptr;
    }

    void insert(const Key &key, const Value &value) {  
        if (occupied_count >= static_cast<size_t>(size * 0.5)) {        
            rehash();
        }

        Key cur_key = key;
        Value cur_value = value;
        //Variable to keep track of how many times we swapped keys.
        size_t times_moved = 0;
        size_t k = hash_func(cur_key);

        while (times_moved <= size) {
            if (!T1[k].occupied) {
                T1[k].key = cur_key;
                T1[k].value = cur_value;
                T1[k].occupied = true;
                occupied_count++;
                return;
            }
            //Put the new key and remove the old one.
            times_moved++;
            Key temp_key = T1[k].key;
            Value temp_value = T1[k].value;
            T1[k].key = cur_key;
            T1[k].value = cur_value;

            cur_key = temp_key;
            cur_value = temp_value;

            if (times_moved == size) {
                //Break to rehash if cycle detected.
                break;
            }

            size_t k2 = hash_func2(cur_key);
            if (!T2[k2].occupied) {
                T2[k2].key = cur_key;
                T2[k2].value = cur_value;
                T2[k2].occupied = true;
                occupied_count++;
                return;
            }

            times_moved++;
            temp_key = T2[k2].key;
            temp_value = T2[k2].value;
            T2[k2].key = cur_key;
            T2[k2].value = cur_value;
            cur_key = temp_key;
            cur_value = temp_value;
        }
        //Rehash because cycle detected.
        rehash();
        insert(cur_key, cur_value);
    }

    //Rehash function(puts pair keys,value in a temporary vector, resize and insert again all keys,values).
    void rehash() {
        std::vector<std::pair<Key, Value>> items;
        items.reserve(occupied_count + 1);
        for (size_t i = 0; i < size; ++i) {
            if (T1[i].occupied) items.emplace_back(std::move(T1[i].key), std::move(T1[i].value));
            if (T2[i].occupied) items.emplace_back(std::move(T2[i].key), std::move(T2[i].value));
        }
        
        size *= 2;
        T1.clear();
        T2.clear();
        T1.resize(size);
        T2.resize(size);
        occupied_count = 0;
        
        for (auto &it : items) {
            insert(it.first, it.second);
        }
    }
};