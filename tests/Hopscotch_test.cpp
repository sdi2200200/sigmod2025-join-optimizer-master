#include <catch2/catch_test_macros.hpp>
#include "hopscotch.h"

TEST_CASE("Simple insert and find", "[hopscotch]") {
    HashTable<int, int> ht(8);
    ht.insert(1, 100);
    ht.insert(2, 200);

    int* v1 = ht.find(1);
    int* v2 = ht.find(2);
    int* v3 = ht.find(3);

    REQUIRE(v1 != nullptr);
    REQUIRE(*v1 == 100);
    REQUIRE(v2 != nullptr);
    REQUIRE(*v2 == 200);
    REQUIRE(v3 == nullptr);
}

TEST_CASE("Rehash check", "[hopscotch]") {
    HashTable<int, int> ht(8);
    for (int i = 0; i < 100; ++i) {
        ht.insert(i, i * 10);
    }

    for (int i = 0; i < 100; ++i) {
        int* v = ht.find(i);
        REQUIRE(v != nullptr);
        REQUIRE(*v == i * 10);
    }
}

TEST_CASE("Second key hash in a occupied node at the end of the table", "[hopscotch]") {
    HashTable<int, int> ht(8);
    ht.insert(7, 100);
    ht.insert(15, 200);

    REQUIRE(ht.find(7) != nullptr);
    REQUIRE(*ht.find(7) == 100);
    REQUIRE(ht.find(15) != nullptr);
    REQUIRE(*ht.find(15) == 200);
    REQUIRE(ht.find(1) == nullptr);
    REQUIRE(ht.size == 16); //table should have been rehashed
}

TEST_CASE("Bitmap check", "[hopscotch]") {
    HashTable<int,int> ht(8);
    ht.insert(0, 1);
    ht.insert(8, 2);            
    REQUIRE(ht.buffer[0].hop_info == 3); // 1100 in binary but we read right to left
    REQUIRE(ht.buffer[1].hop_info == 0);
    ht.insert(10, 3);
    REQUIRE(ht.buffer[2].hop_info == 1); 
    
}

TEST_CASE("check on rehash", "[hopscotch]") {
    HashTable<int,int> ht(8);
    ht.insert(0, 1);
    ht.insert(7, 2);
    ht.insert(15, 3);


    REQUIRE(ht.buffer[0].hop_info == 1);
    REQUIRE(ht.buffer[7].hop_info == 1);
    REQUIRE(ht.buffer[15].hop_info == 1);

  
}