#include <catch2/catch_test_macros.hpp>
#include "cuckoo.h"

TEST_CASE("Simple insert and find", "[cuckoo]") {
	HashTable<int, int> ht(10);
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

TEST_CASE("Two keys collide check if they got swapped right", "[cuckoo]") {
    HashTable<int, int> ht(10);
    ht.insert(1, 100);
    ht.insert(11, 200);

    REQUIRE(ht.T1[1].key == 11);
    REQUIRE(ht.T2[0].key == 1);
}

TEST_CASE("Rehash check", "[cuckoo]") {
    HashTable<int, int> ht(2);
    for (int i = 0; i < 100; ++i) {
        ht.insert(i, i * 10);
    }

    for (int i = 0; i < 100; ++i) {
        int* v = ht.find(i);
        REQUIRE(v != nullptr);
        REQUIRE(*v == i * 10);
    }
}

TEST_CASE("Rehashing cause of circle", "[cuckoo]") {
    HashTable<int, int> ht(10);
    ht.insert(11, 100);
    ht.insert(111, 200);
    ht.insert(1111, 300);

    REQUIRE(ht.size == 20);
    REQUIRE(*ht.find(11) == 100);
    REQUIRE(*ht.find(111) == 200);
    REQUIRE(*ht.find(1111) == 300);
}