#include <iostream>
#include "statement.h"

////////////////START DEBUG FUNCTIONS SECTION////////////////
//Needed to print Value values.
template <typename T>
void print_value(const T &v, std::ostream &os);

template <typename... Ts>
void print_value(const std::variant<Ts...> &v, std::ostream &os);
////////////////END DEBUG FUNCTIONS SECTION////////////////

template <typename Key, typename Value>
struct HashNode {
    size_t hop_info = 0;
    Key key;
    Value value;
    bool occupied = false;      //If the node is occupied.
};

template <typename Key, typename Value>
class HashTable {
public:
    size_t size;                                //Size of hash table.
    size_t H = 4;                               
    std::vector<HashNode<Key, Value>> buffer;   //Buffer to hold hash nodes.
    size_t max_bitmap_size = 15;                  //Maximum size of hop info bitmap.
    //Constructor to initialize hash table with the given size.
    explicit HashTable(size_t s) : size(s) {
        buffer.resize(size);
    }

    size_t hash_func(const Key &k) const {      //Hash function to compute the index for a given key.
        std::hash<Key> hasher;
        return hasher(k) % size;
    }
    
   
    Value* find(const Key& key) {           //Find function that returns a pointer to the value associated with the given key.
        size_t k = hash_func(key);
        for (size_t i = k; i < k+H; i++) {
            if (i >= size) {
                break;
            }
            if (!buffer[i].occupied) {
                break;
            }
            if (buffer[i].key == key) {
                return &buffer[i].value;
            }
            
        
        }
        return nullptr;
    }

    
    
    
    int findLeftmostOne(size_t bitmap, size_t limit) {      //Find the leftmost one bit in the bitmap up to the given limit.
        if (bitmap == 0) return -1;
        for (size_t b = 0; b < limit; ++b) {
            if (bitmap & (1 << b)) return b;
        }
        return -1;
    }

    
    void insert(const Key &key, const Value &value) {    //Insert function to add a key-value pair to the hash table using hopscotch hashing.
        size_t k = hash_func(key);

        if (buffer[k].hop_info == max_bitmap_size) {        // if the bitmap is full, rehash
            rehash();
            insert(key, value);
            return;
        }

        for (size_t i = k; i < k+H ; i++) {     //look for an empty slot within neighborhood
            
            if (i >= size) {
                rehash();
                insert(key, value);
                return;
            }
            if (buffer[i].occupied==false) {        //found an empty slot and put the key and value there
                buffer[i].key = key;
                buffer[i].value = value;
                buffer[i].occupied = true;
                buffer[k].hop_info |= (1 << (i-k));
                return;
               
            }

           
        }
        
       
        
        
        
        size_t j=0;
        bool flag=false;

        for (size_t i = k; i < size; i++) {
             
            if (!buffer[i].occupied) {
                j=i;
                flag=true;
                break; 
            }
            
        }
        
        
        if (flag==false) {
            rehash();
            insert(key, value);
            return;
        }

        while (j - k >= H) {      //try to move elements to make space within neighborhood
            bool moved = false;

            for (size_t i = H - 1; i > 0; --i) {        
                size_t neighbor = j - i;
                if (neighbor >= size) continue;

                // find a bit left in neighbor's hop_info with 0 <= left < i
                int left = findLeftmostOne(buffer[neighbor].hop_info, i);
                if (left != -1) {               // in case there is a bit to change
                    size_t src = neighbor + left;
                    // move element from src -> j
                    buffer[j].key = std::move(buffer[src].key);
                    buffer[j].value = std::move(buffer[src].value);
                    buffer[j].occupied = true;
                    buffer[src].occupied = false;

                    // update neighbor bitmap: clear old bit, set new bit at distance i
                    buffer[neighbor].hop_info &= ~(1 << left);
                    buffer[neighbor].hop_info |= (1 << i);

                    j = src; 
                    moved = true;
                    break;
                }
            }

            if (!moved) {
                rehash();
                insert(key, value);
                return;
            }
        }

        //we put the new key-value pair in j
        buffer[j].key = key;
        buffer[j].value = value;
        buffer[j].occupied = true;
        

        // update home bucket hop_info with offset (j - k)
        size_t offset = j - k;
        buffer[k].hop_info |= (1 << offset);
            
        
    }

    

    void rehash() {
        size_t old_size = size;
        size *= 2;
        std::vector<HashNode<Key, Value>> old_buffer = buffer;

        buffer.clear();
        buffer.resize(size);
        int count = 0;

        for (size_t i = 0; i < old_size; i++) {
            if (old_buffer[i].occupied) {
                insert(old_buffer[i].key, old_buffer[i].value);
            }
        }
    }
    
    
};


