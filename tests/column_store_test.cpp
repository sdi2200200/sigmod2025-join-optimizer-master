#include <catch2/catch_test_macros.hpp>

#include <table.h>
#include <plan.h>
#include <late.h>
#include <column_store.h>

TEST_CASE("Long string check", "[late]") {
    Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{1, DataType::VARCHAR}, {0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 1, {{0, DataType::INT32}, {2, DataType::INT32}, {1, DataType::VARCHAR}});
    using namespace std::string_literals;

    std::string long_xxxx(2.5*PAGE_SIZE, 'x');
    std::vector<std::vector<Data>> data1{
        {1               , long_xxxx,},
        {1               , "yyy"s,},
        {std::monostate{}, "zzz"s,},
        {2               , "uuu"s,},
        {3               , "vvv"s,},
    };
    std::vector<DataType> types{DataType::INT32, DataType::VARCHAR};
    Table table1(std::move(data1), std::move(types));
    ColumnarTable input1 = table1.to_columnar();
    ColumnarTable input2 = table1.to_columnar();
    plan.inputs.emplace_back(std::move(input1));
    plan.inputs.emplace_back(std::move(input2));
    plan.root = 2;

    auto y = column_t_copy_scan(1, plan.inputs[1], plan.nodes[1].output_attrs);
    REQUIRE(y.size() == 2);
    REQUIRE(y[0].pages[0]->count == 5);
    REQUIRE(y[0].pages[0]->value_buffer[0].is_varchar());
    REQUIRE(print_varchar(plan, y[0].pages[0]->value_buffer[0]) == long_xxxx);
    
    
}

TEST_CASE("copy_scan int_32 check", "[late]") {
	Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{1, DataType::VARCHAR}, {0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 1, {{0, DataType::INT32}, {2, DataType::INT32}, {1, DataType::VARCHAR}});
    using namespace std::string_literals;
    std::vector<std::vector<Data>> data1{
        {1               , "xxx"s,},
        {1               , "yyy"s,},
        {std::monostate{}, "zzz"s,},
        {2               , "uuu"s,},
        {3               , "vvv"s,},
    };
    std::vector<DataType> types{DataType::INT32, DataType::VARCHAR};
    Table table1(std::move(data1), std::move(types));
    ColumnarTable input1 = table1.to_columnar();
    ColumnarTable input2 = table1.to_columnar();
    plan.inputs.emplace_back(std::move(input1));
    plan.inputs.emplace_back(std::move(input2));
    plan.root = 2;
    
    auto x = column_t_copy_scan(0, plan.inputs[0], plan.nodes[0].output_attrs);
    REQUIRE(x.size() == 1);
    REQUIRE(x[0].pages[0]->count == 5);
    REQUIRE(x[0].pages[0]->value_buffer[0].is_int32());
    REQUIRE(x[0].pages[0]->value_buffer[0].get_int32() == 1);
    REQUIRE(x[0].pages[0]->value_buffer[1].get_int32() == 1);
    REQUIRE(x[0].pages[0]->value_buffer[2].is_null());
    REQUIRE(x[0].pages[0]->value_buffer[3].get_int32() == 2);
    REQUIRE(x[0].pages[0]->value_buffer[4].get_int32() == 3);
}

TEST_CASE("copy_scan int_32 and varchar check", "[late]") {
	Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{1, DataType::VARCHAR}, {0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 1, {{0, DataType::INT32}, {2, DataType::INT32}, {1, DataType::VARCHAR}});
    using namespace std::string_literals;
    std::vector<std::vector<Data>> data1{
        {1               , "xxx"s,},
        {1               , "yyy"s,},
        {std::monostate{}, "zzz"s,},
        {2               , "uuu"s,},
        {3               , "vvv"s,},
    };
    std::vector<DataType> types{DataType::INT32, DataType::VARCHAR};
    Table table1(std::move(data1), std::move(types));
    ColumnarTable input1 = table1.to_columnar();
    ColumnarTable input2 = table1.to_columnar();
    plan.inputs.emplace_back(std::move(input1));
    plan.inputs.emplace_back(std::move(input2));
    plan.root = 2;

    auto y = column_t_copy_scan(1, plan.inputs[1], plan.nodes[1].output_attrs);
    REQUIRE(y.size() == 2);
    REQUIRE(y[0].pages[0]->count == 5);
    REQUIRE(y[0].pages[0]->value_buffer[0].is_varchar());
    REQUIRE(print_varchar(plan, y[0].pages[0]->value_buffer[0]) == "xxx");
    REQUIRE(y[0].pages[0]->value_buffer[1].is_varchar());
    REQUIRE(print_varchar(plan, y[0].pages[0]->value_buffer[1]) == "yyy");
    REQUIRE(y[0].pages[0]->value_buffer[2].is_varchar());
    REQUIRE(print_varchar(plan, y[0].pages[0]->value_buffer[2]) == "zzz");
    REQUIRE(print_varchar(plan, y[0].pages[0]->value_buffer[3]) == "uuu");
      
}

TEST_CASE("to_columnar_from_column_t check", "[late]") {
    Plan plan;
    plan.new_scan_node(0, {{0, DataType::INT32}});
    plan.new_scan_node(1, {{1, DataType::VARCHAR}, {0, DataType::INT32}});
    plan.new_join_node(true, 0, 1, 0, 1, {{0, DataType::INT32}, {2, DataType::INT32}, {1, DataType::VARCHAR}});
    using namespace std::string_literals;
    std::vector<std::vector<Data>> data1{
        {1               , "xxx"s,},
        {1               , "yyy"s,},
        {std::monostate{}, "zzz"s,},
        {2               , "uuu"s,},
        {3               , "vvv"s,},
    };
    std::vector<DataType> types{DataType::INT32, DataType::VARCHAR};
    Table table1(std::move(data1), std::move(types));
    ColumnarTable input1 = table1.to_columnar();
    ColumnarTable input2 = table1.to_columnar();
    plan.inputs.emplace_back(std::move(input1));
    plan.inputs.emplace_back(std::move(input2));
    plan.root = 2;

    auto x = column_t_copy_scan(0, plan.inputs[0], plan.nodes[0].output_attrs);
    ColumnarTable col_table_x = to_columnar_from_column(x, plan);

    REQUIRE(col_table_x.num_rows == 5);
    REQUIRE(col_table_x.columns.size() == 1);
    REQUIRE(col_table_x.columns[0].type == DataType::INT32);
    auto table_x = Table::from_columnar(col_table_x);
    std::vector<std::vector<Data>> ground_truth_x{
        {1, },
        {1, },
        {std::monostate{}, },   
        {2, },
        {3, },
    };
    REQUIRE(table_x.table() == ground_truth_x);
}