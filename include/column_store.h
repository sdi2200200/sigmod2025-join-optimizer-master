#pragma once

#include <filesystem>
#include <fmt/core.h>
#include <range/v3/all.hpp>

#include <attribute.h>
#include <plan.h>
#include <statement.h>
#include <late.h>

#ifdef TEAMOPT_USE_DUCKDB
#include <duckdb.hpp>
#endif

struct ColumnPage {
    static constexpr size_t BUF_SIZE = PAGE_SIZE / sizeof(value_t);
    value_t value_buffer[BUF_SIZE]; //1024 boxes of 8 bytes.
    uint16_t count = 0; //How many values are stored in the page.
};

struct column_t {
    DataType type;
    std::vector<ColumnPage*> pages;
    bool is_no_null = false;
    std::vector<Page*> original_pages;

    ~column_t() {
        for (auto *p : pages) delete p;
    }

    void append(const value_t &v) {
        if (pages.empty() || pages.back()->count == ColumnPage::BUF_SIZE)
            pages.push_back(new ColumnPage());
        pages.back()->value_buffer[pages.back()->count++] = v;
    }

    void create_pages(size_t total_rows) {
        size_t needed_pages = (total_rows + ColumnPage::BUF_SIZE - 1) / ColumnPage::BUF_SIZE;
        while (pages.size() < needed_pages) {
            pages.push_back(new ColumnPage());
        }
    }
};

//Make std::vector<column_t> from ColumnarTable.
std::vector<column_t> column_t_copy_scan(int table_id, const ColumnarTable& table,
    const std::vector<std::tuple<size_t, DataType>>& output_attrs);

//Make ColumnarTable from std::vector<column_t>.
ColumnarTable to_columnar_from_column(const std::vector<column_t>& column_vec,
    const Plan& plan);
