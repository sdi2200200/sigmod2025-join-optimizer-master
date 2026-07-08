#include <atomic>
#include <charconv>

#include <common.h>
#include <csv_parser.h>
#include <inner_column.h>
#include <plan.h>
#include <late.h>

#if !defined(TEAMOPT_USE_DUCKDB) || defined(TEAMOPT_BUILD_CACHE)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

bool get_my_bitmap(const uint8_t* bitmap, uint16_t idx) {
    auto byte_idx = idx / 8;
    auto bit      = idx % 8;
    return bitmap[byte_idx] & (1u << bit);
}

std::vector<std::vector<value_t>> my_copy_scan( int table_id, const ColumnarTable& table,
    const std::vector<std::tuple<size_t, DataType>>& output_attrs) {
    
    namespace views = ranges::views;
    std::vector<std::vector<value_t>> results(table.num_rows, 
        std::vector<value_t>(output_attrs.size(), value_t::make_null()));
    std::vector<DataType> types(table.columns.size());

    auto task = [&](size_t begin, size_t end) {
        for (size_t column_idx = begin; column_idx < end; ++column_idx) {

            size_t in_col_idx = std::get<0>(output_attrs[column_idx]);
            auto& column = table.columns[in_col_idx];
            types[in_col_idx] = column.type;
            size_t row_idx = 0;
            size_t page_idx = 0;    //page pos.

            for (auto* page:column.pages | views::transform([](auto* page) { return page->data; })) {
                switch (column.type) {
                    case DataType::INT32: {
                        auto  num_rows   = *reinterpret_cast<uint16_t*>(page);
                        auto* data_begin = reinterpret_cast<int32_t*>(page + 4);
                        auto* bitmap = reinterpret_cast<uint8_t*>(page + PAGE_SIZE - (num_rows + 7) / 8);
                        uint16_t data_idx = 0;

                        for (uint16_t i = 0; i < num_rows; ++i) {
                            if (get_my_bitmap(bitmap, i)) {
                                auto value = data_begin[data_idx++];
                                if (row_idx >= table.num_rows) {
                                    throw std::runtime_error("row_idx");
                                }
                                results[row_idx++][column_idx] = value_t::put_int32(value); 
                            } else {
                                ++row_idx;
                            }
                        }
                        break;
                    }
                    case DataType::VARCHAR: {
                        auto num_rows = *reinterpret_cast<uint16_t*>(page);
                        //if num_rows is 0xffff, the page represent a long string.
                        if (num_rows == 0xffff) {
                            int32_t pos = 0;
                            auto* next_page = column.pages[page_idx + 1];
                            auto npage_rows = *reinterpret_cast<uint16_t*>(next_page);
                            while (npage_rows == 0xfffe) {
                                pos++;  //position after the start page that the long string ends.
                                next_page = column.pages[pos + 1];
                                npage_rows = *reinterpret_cast<uint16_t*>(next_page);
                            }
                            results[row_idx++][column_idx] = value_t::put_varchar((int32_t)table_id, 
                                    (int32_t)in_col_idx, (int32_t)page_idx, (int32_t)pos);
                        } else if (num_rows == 0xfffe) {
                            //Handled in the previous page.
                        } else {
                            //Normal varchar case.
                            auto  num_non_null = *reinterpret_cast<uint16_t*>(page + 2);
                            auto* offset_begin = reinterpret_cast<uint16_t*>(page + 4);
                            auto* data_begin   = reinterpret_cast<char*>(page + 4 + num_non_null * 2);
                            auto* bitmap = reinterpret_cast<uint8_t*>(page + PAGE_SIZE - (num_rows + 7) / 8);
                            uint16_t data_idx = 0;
                            //previous offset, keep where the string starts.
                            uint16_t prev_offset = 0;        

                            for (uint16_t i = 0; i < num_rows; ++i) {
                                if (get_my_bitmap(bitmap, i)) {
                                    auto offset = offset_begin[data_idx++];
                                    uint16_t pos = prev_offset;
                                    prev_offset = offset;
                                    if (row_idx >= table.num_rows) {
                                        throw std::runtime_error("row_idx");
                                    }
                                    results[row_idx++][column_idx] = value_t::put_varchar((int32_t)table_id, 
                                        (int32_t)in_col_idx, (int32_t)page_idx, (int32_t)pos);
                                } else {
                                    ++row_idx;
                                }
                            }
                        }
                        break;
                    }
                }
                page_idx++;
            }
        }
    };
    filter_tp.run(task, output_attrs.size());
    return results;
}

//Return the string that value_t represents.
std::string print_varchar(const Plan& plan, const value_t& v) {

    int32_t t, c, p, pos;
    v.get_varchar(t, c, p, pos);

    const auto& column_table = plan.inputs[t];
    const auto& column = column_table.columns[c];
    auto* page = column.pages[p]->data;

    auto        num_chars  = *reinterpret_cast<uint16_t*>(page + 2);
    auto*       data_begin = reinterpret_cast<char*>(page + 4);
    //Put the first part of the long string.
    std::string value{data_begin, data_begin + num_chars};
    int32_t next_page_id = p + 1;

    while (next_page_id < column.pages.size()) {
        auto* next_page_data = column.pages[next_page_id]->data;
        uint16_t npage_rows = *reinterpret_cast<uint16_t*>(next_page_data);
        if (npage_rows != 0xfffe) { //This page does not contain the next part of the long string.
            break;
        }
        auto next_num_chars = *reinterpret_cast<uint16_t*>(next_page_data + 2);
        auto* next_data_begin = reinterpret_cast<char*>(next_page_data + 4);
        value.append(next_data_begin, next_data_begin + next_num_chars);
        next_page_id++;
    }
    return value;
}

ColumnarTable to_columnar_from_value(const std::vector<std::vector<value_t>>& data_vec,
    const std::vector<DataType>& data_types,
    const Plan& plan) {

    namespace views = ranges::views;
    ColumnarTable ret;
    ret.num_rows = data_vec.size();
    ret.columns.reserve(data_types.size());

    for (auto [col_idx, data_type] : data_types | views::enumerate) {
        ret.columns.emplace_back(data_type);
        auto& column = ret.columns.back();

        switch (data_type) {
            case DataType::INT32: {
                ColumnInserter<int32_t> inserter(column);
                for (size_t row = 0; row < data_vec.size(); ++row) {
                    const value_t& v = data_vec[row][col_idx];
                    if (v.is_null()) {
                        inserter.insert_null();
                    } else if (v.is_int32()) {
                        inserter.insert(v.get_int32());
                    }
                }
                inserter.finalize();
                break;
            }
            case DataType::VARCHAR: {
                ColumnInserter<std::string> inserter(column);
                for (size_t row = 0; row < data_vec.size(); ++row) {
                    const value_t& v = data_vec[row][col_idx];
                    if (v.is_null()) {
                        inserter.insert_null();
                    } else if (v.is_varchar()) {
                        std::string s = print_varchar(plan, v);
                        inserter.insert(s);
                    }
                }
                inserter.finalize();
                break;
            }
            default:
                throw std::runtime_error("OTHER TYPE FOUND!!");
        }
    }

    return ret;
}