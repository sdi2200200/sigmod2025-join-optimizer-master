#pragma once

#include <filesystem>
#include <fmt/core.h>
#include <range/v3/all.hpp>

#include <attribute.h>
#include <plan.h>
#include <statement.h>

#ifdef TEAMOPT_USE_DUCKDB
#include <duckdb.hpp>
#endif

struct value_t {
    uint64_t raw;   //64bits variable.

    /*Last 62 bits to keep info for the data in value_t format*/
    //VALUE_T_BITS: bits to keep info for the data.
    static constexpr uint64_t VALUE_T_BITS = 62;

    /*First 2 bits represent the type of value_t*/
    //MODE_BITS = (11), to see what type the value_t is, using & operation.
    //VARCHAR_MODE = (00)
    //INT32_MODE = (01)
    //INT32_NO_NULL_MODE = (10)
    //NULL_MODE = (11)
    static constexpr uint64_t MODE_BITS = (uint64_t)3 << VALUE_T_BITS;
    static constexpr uint64_t VARCHAR_MODE = (uint64_t)0 << VALUE_T_BITS;
    static constexpr uint64_t INT32_MODE = (uint64_t)1 << VALUE_T_BITS;
    static constexpr uint64_t INT32_NO_NULL_MODE = (uint64_t)2 << VALUE_T_BITS;
    static constexpr uint64_t NULL_MODE = (uint64_t)3 << VALUE_T_BITS;

    //In case of VARCHAR, we seperate 62 bits to 5,15,26,16 to keep table(5), column(15), page(26), pos(16) info of varchar.
    static constexpr unsigned TABLE_BITS = 5;
    static constexpr unsigned COLUMN_BITS = 15;
    static constexpr unsigned PAGE_BITS = 26;
    static constexpr unsigned POS_BITS = 16;

    //Bits end position of table, column, page, pos.
    static constexpr unsigned POS_SHIFT = 0;
    static constexpr unsigned PAGE_SHIFT = POS_SHIFT + POS_BITS;
    static constexpr unsigned COLUMN_SHIFT = PAGE_SHIFT + PAGE_BITS;
    static constexpr unsigned TABLE_SHIFT = COLUMN_SHIFT + COLUMN_BITS;

    //Isolate and save the position of table, column, page, pos.
    static constexpr uint64_t POS_POSITION = (((uint64_t)1 << POS_BITS) - 1) << POS_SHIFT;
    static constexpr uint64_t PAGE_POSITION = (((uint64_t)1 << PAGE_BITS) - 1) << PAGE_SHIFT;
    static constexpr uint64_t COLUMN_POSITION = (((uint64_t)1 << COLUMN_BITS) - 1) << COLUMN_SHIFT;
    static constexpr uint64_t TABLE_POSITION = (((uint64_t)1 << TABLE_BITS) - 1) << TABLE_SHIFT;

    //Make NULL value_t.
    static value_t make_null() {
        value_t x;
        x.raw = NULL_MODE;
        return x;
    }
    //Check if value_t is NULL.
    bool is_null() const {
        return (raw & MODE_BITS) == NULL_MODE;
    }

    /*In case of INT32 we save the value at the last 32bits(no need to keep track of table, column, page, pos)*/

    //Make int32 value_t and put int32 value in it.
    static value_t put_int32(int32_t v) {
        value_t x;
        x.raw = INT32_MODE | v;
        return x;
    }
    //Check if value_t is int32.
    bool is_int32() const {
        return (raw & MODE_BITS) == INT32_MODE;
    }
    //Return the int32 value from value_t.
    int32_t get_int32() const {
        return (int32_t)raw;
    }

    /*In case of INT32_NO_NULL we keep track of info (like VARCHAR)*/

    //Make int32_no_null value_t using table, column, page, pos info.
    static value_t put_int32_no_null(int32_t table, int32_t column, int32_t page, int32_t pos) {
        if (table >= (1 << TABLE_BITS) || column >= (1 << COLUMN_BITS) || page >= (1 << PAGE_BITS) || pos >= (1 << POS_BITS)) {
            throw std::runtime_error("Table,column,page,pos overflow");
        }
        //Put table, column, page, pos info in the correct bit fields.
        uint64_t p = ((int64_t)table << TABLE_SHIFT)
                   | ((int64_t)column << COLUMN_SHIFT)
                   | ((int64_t)page << PAGE_SHIFT)
                   | ((int64_t)pos << POS_SHIFT);
        value_t x;
        //Put int32_ref mode id.
        x.raw = INT32_NO_NULL_MODE | p;
        return x;
    }
    //Check if value_t is int32_no_null.
    bool is_int32_no_null() const {
        return (raw & MODE_BITS) == INT32_NO_NULL_MODE;
    }
    //Get table, column, page, pos info from int32_no_null value_t.
    void get_int32_no_null(int32_t &table, int32_t &column, int32_t &page, int32_t &pos) const {

        uint64_t p = raw & ~MODE_BITS;  //Ignore mode bits to get only the position info.
        pos = (int32_t)((p & POS_POSITION) >> POS_SHIFT);
        page = (int32_t)((p & PAGE_POSITION) >> PAGE_SHIFT);
        column = (int32_t)((p & COLUMN_POSITION) >> COLUMN_SHIFT);
        table = (int32_t)((p & TABLE_POSITION) >> TABLE_SHIFT);
    }

    //Make varchar value_t using table, column, page, pos info.
    static value_t put_varchar(int32_t table, int32_t column, int32_t page, int32_t pos) {
        if (table >= (1 << TABLE_BITS) || column >= (1 << COLUMN_BITS) || page >= (1 << PAGE_BITS) || pos >= (1 << POS_BITS)) {
            throw std::runtime_error("Table,column,page,pos overflow");
        }
        //Put table, column, page, pos info in the correct bit fields.
        uint64_t p = ((int64_t)table << TABLE_SHIFT)
                   | ((int64_t)column << COLUMN_SHIFT)
                   | ((int64_t)page << PAGE_SHIFT)
                   | ((int64_t)pos << POS_SHIFT);
        value_t x;
        //Put varchar mode id.
        x.raw = VARCHAR_MODE | p;
        return x;
    }
    //Check if value_t is varchar.
    bool is_varchar() const {
        return (raw & MODE_BITS) == VARCHAR_MODE;
    }
    //Get table, column, page, pos info from varchar value_t.
    void get_varchar(int32_t &table, int32_t &column, int32_t &page, int32_t &pos) const {

        uint64_t p = raw & ~MODE_BITS;  //Ignore mode bits to get only the position info.
        pos = (int32_t)((p & POS_POSITION) >> POS_SHIFT);
        page = (int32_t)((p & PAGE_POSITION) >> PAGE_SHIFT);
        column = (int32_t)((p & COLUMN_POSITION) >> COLUMN_SHIFT);
        table = (int32_t)((p & TABLE_POSITION) >> TABLE_SHIFT);
    }

    int32_t get_table() const {
        int32_t t, c, p, pos;
        get_varchar(t, c, p, pos);
        return t;
    }
    int32_t get_column() const {
        int32_t t, c, p, pos;
        get_varchar(t, c, p, pos);
        return c;
    }
    int32_t get_page() const {
        int32_t t, c, p, pos;
        get_varchar(t, c, p, pos);
        return p;
    }
    int32_t get_pos() const {
        int32_t t, c, p, pos;
        get_varchar(t, c, p, pos);
        return pos;
    }
};
static_assert(sizeof(value_t) == 8, "value_t must be 64 bits"); //Assure value_t is 64bits.

inline int32_t get_int32_value_from_page(const value_t& v, const std::vector<Page*>* original_pages) {
    
    int32_t table_id, column_id, page_id, pos;
    v.get_int32_no_null(table_id, column_id, page_id, pos);
    auto* page = (*original_pages)[page_id]->data;
    auto* data_begin = reinterpret_cast<int32_t*>(page + 4);
    return data_begin[pos];
}
//Make vector<std::vector<value_t>> from ColumnarTable.
std::vector<std::vector<value_t>> my_copy_scan(
    int table_id,
    const ColumnarTable& table,
    const std::vector<std::tuple<size_t, DataType>>& output_attrs);

//Return a string using the info from value_t.
std::string print_varchar(const Plan& plan, const value_t& v);

//Make ColumnarTable from vector<std::vector<value_t>>.
ColumnarTable to_columnar_from_value(const std::vector<std::vector<value_t>>& data_vec,
    const std::vector<DataType>& data_types,
    const Plan& plan);