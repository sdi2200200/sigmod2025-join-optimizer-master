#include <catch2/catch_test_macros.hpp>
#include "robinhood.h"

TEST_CASE("Simple insert and find", "[robinhood]") {
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

TEST_CASE("Rehash check", "[robinhood]") {
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

TEST_CASE("Second key hash in a occupied node at the end of the table", "[robinhood]") {
    HashTable<int, int> ht(8);
    ht.insert(7, 100);
    ht.insert(15, 200);

    REQUIRE(ht.find(7) != nullptr);
    REQUIRE(*ht.find(7) == 100);
    REQUIRE(ht.find(15) != nullptr);
    REQUIRE(*ht.find(15) == 200);
    REQUIRE(ht.find(1) == nullptr);
}

TEST_CASE("Three keys collide to the same index", "[robinhood]") {
    HashTable<int,int> ht(8);
    ht.insert(0, 1);
    ht.insert(8, 2);
    ht.insert(16, 3);

    REQUIRE(ht.buffer[0].psl == 0);
    REQUIRE(ht.buffer[1].psl == 1);
    REQUIRE(ht.buffer[2].psl == 2);
}

TEST_CASE("Check psl correction and nodes swap", "[robinhood]") {
    HashTable<int,int> ht(20);
    ht.insert(0, 1);
    ht.insert(1, 2);
    ht.insert(2, 3);
    ht.insert(21, 4);

    REQUIRE(ht.buffer[0].psl == 0);
    REQUIRE(ht.buffer[1].psl == 0);
    REQUIRE(ht.buffer[2].psl == 1);
    REQUIRE(ht.buffer[3].psl == 1);
    REQUIRE(ht.buffer[2].key == 21);
    REQUIRE(ht.buffer[3].key == 2);
}