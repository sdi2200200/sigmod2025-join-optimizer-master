#include <atomic>
#include <charconv>
#include <iostream>
#include <common.h>
#include <csv_parser.h>
#include <inner_column.h>
#include <plan.h>
#include <late.h>
#include <column_store.h>

#if !defined(TEAMOPT_USE_DUCKDB) || defined(TEAMOPT_BUILD_CACHE)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

std::vector<column_t> column_t_copy_scan(int table_id, const ColumnarTable& table,
    const std::vector<std::tuple<size_t, DataType>>& output_attrs) {
    
    namespace views = ranges::views;
    std::vector<column_t> results(output_attrs.size());

    auto task = [&](size_t begin, size_t end) {
        for (size_t column_idx = begin; column_idx < end; ++column_idx) {

            size_t in_col_idx = std::get<0>(output_attrs[column_idx]);
            auto& column = table.columns[in_col_idx];
            
            size_t row_idx = 0;
            size_t page_idx = 0;    //page pos.

            //Check if column has no null values.
            bool no_nulls = false;
            if (column.type == DataType::INT32) {
                no_nulls = true;
                for (auto* page_ptr : column.pages) {
                    auto* page = page_ptr->data;
                    auto num_rows = *reinterpret_cast<uint16_t*>(page);
                    auto num_non_null = *reinterpret_cast<uint16_t*>(page + 2);
                    if (num_rows != num_non_null) {
                        no_nulls = false;
                        break;
                    }
                }
            }
            if (column.type == DataType::INT32 && no_nulls) {
                results[column_idx].type = DataType::INT32;
                results[column_idx].is_no_null = true;
                results[column_idx].original_pages = column.pages;
                //Allocate neccessary pages.
                size_t total_rows = 0;
                for (auto* page_ptr : column.pages) {
                    auto* page = page_ptr->data;
                    total_rows += *reinterpret_cast<uint16_t*>(page);
                }
                //In case we do not have data.
                if (total_rows == 0) {
                    continue;
                }
                results[column_idx].create_pages(total_rows);
                
                //Put value_t in pages.
                size_t current_page_idx = 0;
                size_t current_pos_in_page = 0;
                ColumnPage* current_page = results[column_idx].pages[0];
                
                for (auto* page_ptr : column.pages) {

                    auto* page = page_ptr->data;
                    auto num_rows = *reinterpret_cast<uint16_t*>(page);
                    for (uint16_t i = 0; i < num_rows; ++i) {
                        //Check if we need to move to next page.
                        if (current_pos_in_page == ColumnPage::BUF_SIZE) {
                            current_page->count = current_pos_in_page;
                            current_page_idx++;
                            current_pos_in_page = 0;
                            current_page = results[column_idx].pages[current_page_idx];
                        }
                        current_page->value_buffer[current_pos_in_page] = value_t::put_int32_no_null(
                            (int32_t)table_id, (int32_t)in_col_idx, (int32_t)page_idx, (int32_t)i);
                        current_pos_in_page++;
                    }
                    page_idx++;
                }
                current_page->count = current_pos_in_page;
                continue;
            }

            for (auto* page:column.pages | views::transform([](auto* page) { return page->data; })) {
                switch (column.type) {
                    case DataType::INT32: {
                        auto  num_rows   = *reinterpret_cast<uint16_t*>(page);
                        auto* data_begin = reinterpret_cast<int32_t*>(page + 4);
                        auto* bitmap = reinterpret_cast<uint8_t*>(page + PAGE_SIZE - (num_rows + 7) / 8);
                        uint16_t data_idx = 0;
                        results[column_idx].type = DataType::INT32;
                        for (uint16_t i = 0; i < num_rows; ++i) {
                            if (bitmap[i >> 3] & (1u << (i & 7))) {
                                auto value = data_begin[data_idx++];
                                if (row_idx >= table.num_rows) {
                                    throw std::runtime_error("row_idx");
                                }
                                results[column_idx].append(value_t::put_int32(value)); 
                                ++row_idx;
                            } else {
                                results[column_idx].append(value_t::make_null());
                                ++row_idx;
                            }
                        }
                        break;
                    }
                    case DataType::VARCHAR: {
                        auto num_rows = *reinterpret_cast<uint16_t*>(page);
                        results[column_idx].type = DataType::VARCHAR;
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
                            results[column_idx].append(value_t::put_varchar((int32_t)table_id, 
                                    (int32_t)in_col_idx, (int32_t)page_idx, (int32_t)pos));
                            ++row_idx;
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
                                if (bitmap[i >> 3] & (1u << (i & 7))) {
                                    auto offset = offset_begin[data_idx++];
                                    uint16_t pos = prev_offset;
                                    prev_offset = offset;
                                    if (row_idx >= table.num_rows) {
                                        throw std::runtime_error("row_idx");
                                    }
                                    ++row_idx;
                                    results[column_idx].append(value_t::put_varchar((int32_t)table_id, 
                                        (int32_t)in_col_idx, (int32_t)page_idx, (int32_t)pos));
                                } else {
                                    results[column_idx].append(value_t::make_null());
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

ColumnarTable to_columnar_from_column(const std::vector<column_t>& column_vec,
    const Plan& plan){

    namespace views = ranges::views;
    ColumnarTable ret;
    size_t num_rows = 0;
    size_t pages_num = column_vec[0].pages.size();
    for (size_t i = 0; i < pages_num; ++i) {
        num_rows += column_vec[0].pages[i]->count;
    }

    ret.num_rows = num_rows;
    for (size_t i = 0; i < column_vec.size(); ++i) {
        DataType dt = column_vec[i].type;
        ret.columns.emplace_back(dt);
        auto& column = ret.columns.back();

        switch (dt) {
            case DataType::INT32: {
                ColumnInserter<int32_t> inserter(column);
                const std::vector<Page*>* orig_pages = &column_vec[i].original_pages;
                for (size_t pg = 0; pg < column_vec[i].pages.size(); ++pg) {
                    for (size_t c = 0; c < column_vec[i].pages[pg]->count; ++c) {
                        const value_t& v = column_vec[i].pages[pg]->value_buffer[c];
                        if (v.is_null()) {
                            inserter.insert_null();
                        } else if (v.is_int32()) {
                            inserter.insert(v.get_int32());
                        } else if (v.is_int32_no_null()) {
                            int32_t value = get_int32_value_from_page(v, orig_pages);                        
                            inserter.insert(value);
                        }
                    }
                    
                }
                inserter.finalize();
                break;
            }
            case DataType::VARCHAR: {
                ColumnInserter<std::string> inserter(column);
                for (size_t pg = 0; pg < column_vec[i].pages.size(); ++pg) {
                    for (size_t c = 0; c < column_vec[i].pages[pg]->count; ++c) {
                        const value_t& v = column_vec[i].pages[pg]->value_buffer[c];
                        if (v.is_null()) {
                            inserter.insert_null();
                        } else if (v.is_varchar()) {
                            //Handle here normal strings.
                            int32_t t, col, p, pos;
                            v.get_varchar(t, col, p, pos);

                            const auto& src_column = plan.inputs[t].columns[col];
                            auto* page = src_column.pages[p]->data;
                            auto num_rows_page = *reinterpret_cast<uint16_t*>(page);

                            if (num_rows_page == 0xffff) {
                                inserter.insert(print_varchar(plan, v));
                            } else {
                                uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page + 2);
                                auto* offset_begin = reinterpret_cast<uint16_t*>(page + 4);
                                auto* char_begin = reinterpret_cast<char*>(page + 4 + num_non_null * 2);
                                
                                uint16_t pos_uint = static_cast<uint16_t>(pos);
                                uint16_t length = 0;
                                int left = 0, right = num_non_null - 1;
                                while (left <= right) {
                                    int mid = (left + right) / 2;
                                    if (offset_begin[mid] > pos_uint) {
                                        length = offset_begin[mid] - pos_uint;
                                        right = mid - 1;
                                    } else {
                                        left = mid + 1;
                                    }
                                }
                                inserter.insert(std::string_view(char_begin + pos, length));
                            }
                        }
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