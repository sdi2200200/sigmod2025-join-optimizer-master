#include <catch2/catch_test_macros.hpp>
#include <Uhashtable.h>
#include <vector>
#include <tuple>

TEST_CASE("Hashtable same kay insert and probe", "[hashtable]") {
    UnchainedHashTable ht;
    
    ht.insertTempKey(1, 0);
    ht.insertTempKey(2, 1);
    ht.insertTempKey(1, 2);
    ht.insertTempKey(3, 3);
    
    ht.build(ht.temp_keys_pos.size());
    
    std::vector<uint32_t> result;
    ht.probe(1, result);
    REQUIRE(result.size() == 2);
    REQUIRE(result[0] == 0 );
    REQUIRE(result[1] == 2);
    
    result.clear();
    ht.probe(2, result);
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 1);
    
    result.clear();
    ht.probe(24, result);
    REQUIRE(result.size() == 0);
}

TEST_CASE("Hashtable multiple keys probe and build test", "[hashtable]") {
    UnchainedHashTable ht;
    
    for (int32_t i = 0; i < 100; i++) {
        ht.insertTempKey(i * 7, i);
    }
    
    ht.build(ht.temp_keys_pos.size());
    
    for (int32_t i = 0; i < 100; i++) {
        std::vector<uint32_t> result;
        ht.probe(i * 7, result);
        REQUIRE(result.size() == 1);
        REQUIRE(result[0] == i);
    }
    
    std::vector<uint32_t> result;
    ht.probe(1, result);
    REQUIRE(result.size() == 0);
}