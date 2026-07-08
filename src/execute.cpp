#include <hardware.h>
#include <plan.h>

#include <late.h>
#include <iostream>
#include <column_store.h>
#include <Uhashtable.h>
#include <atomic>
#include <pthread.h>


    namespace Contest {

    using ExecuteResultColumn = std::vector<column_t>;

    ExecuteResultColumn execute_impl(const Plan& plan, size_t node_idx);

    struct JoinAlgorithm {
        bool                                             build_left;
        ExecuteResultColumn&                                   left;
        ExecuteResultColumn&                                   right;
        ExecuteResultColumn&                                   results;
        size_t                                           left_col, right_col;
        const std::vector<std::tuple<size_t, DataType>>& output_attrs;

        template <class T>
        auto run() {

            const size_t BOXES = ColumnPage::BUF_SIZE;  //Number of boxes per page.

            if constexpr (std::is_same_v<T, int32_t>) {
                UnchainedHashTable ht;
                auto collect_build = [&](const column_t &col) {
                    const std::vector<Page*>* orig_pages = &col.original_pages;
                    for (size_t p = 0; p < col.pages.size(); ++p) {
                        ColumnPage *pg = col.pages[p];
                        for (size_t s = 0; s < pg->count; ++s) {
                            const value_t &vt = pg->value_buffer[s];
                            if (vt.is_int32()) {
                                ht.insertTempKey(vt.get_int32(), p * BOXES + s);
                            } else if (vt.is_int32_no_null()) {
                                int32_t value = get_int32_value_from_page(vt, orig_pages);
                                ht.insertTempKey(value, p * BOXES + s);
                            } else if (vt.is_null()) {
                                continue;
                            } else {
                                throw std::runtime_error("wrong type of field");
                            }
                        }
                    }
                };

                if (build_left) {
                    collect_build(left[left_col]);
                } else {
                    collect_build(right[right_col]);
                }

                size_t count = ht.temp_keys_pos.size();
                ht.build_parallel(count);
                
                struct ProbeWorkerArgs {
                    const column_t* probe_col;
                    const UnchainedHashTable* ht;
                    std::atomic<size_t>* page_counter;
                    std::vector<column_t>* thread_results;
                    const ExecuteResultColumn* left;
                    const ExecuteResultColumn* right;
                    const std::vector<std::tuple<size_t, DataType>>* output_attrs;
                    bool build_left;
                    size_t left_col;
                    size_t right_col;
                };
                
                auto probe_worker = [](void* arg) -> void* {
                    ProbeWorkerArgs* args = (ProbeWorkerArgs*)arg;
                    const column_t& probe_col = *args->probe_col;
                    const std::vector<Page*>* orig_pages = &probe_col.original_pages;
                    std::vector<uint32_t> matches;
                    const size_t BOXES = ColumnPage::BUF_SIZE;
                    //Work stealing implementation(each thread take one page at the time).
                    while (true) {
                        size_t p = args->page_counter->fetch_add(1);
                        if (p >= probe_col.pages.size()) {
                            //End if there are no more pages to give to the thread.
                            break;
                        }
                        ColumnPage *pg = probe_col.pages[p];
                        for (size_t s = 0; s < pg->count; ++s) {
                            const value_t &vt = pg->value_buffer[s];
                            if (vt.is_int32() || vt.is_int32_no_null()) {
                                int32_t value;
                                if (vt.is_int32()) {
                                    value = vt.get_int32();
                                } else if (vt.is_int32_no_null()) {
                                    value = get_int32_value_from_page(vt, orig_pages);
                                }
                                matches.clear();
                                args->ht->probe(value, matches);
                                if (matches.empty()) continue;
                                size_t probe_pos = p * BOXES + s;
                                size_t probe_pidx = probe_pos / BOXES;
                                size_t probe_posidx = probe_pos % BOXES;
                                for (uint32_t build_pos : matches) {
                                    size_t pidx = build_pos / BOXES;
                                    size_t posidx = build_pos % BOXES;
                                    for (size_t res_col_id = 0; res_col_id < args->output_attrs->size(); ++res_col_id) {
                                        auto [col_idx, _] = (*args->output_attrs)[res_col_id];
                                        if (col_idx < args->left->size()) {
                                            const column_t &src_col = (*args->left)[col_idx];
                                            if (args->build_left) {
                                                (*args->thread_results)[res_col_id].append(src_col.pages[pidx]->value_buffer[posidx]);
                                            } else {
                                                (*args->thread_results)[res_col_id].append(src_col.pages[probe_pidx]->value_buffer[probe_posidx]);
                                            }
                                        } else {
                                            size_t right_col_idx = col_idx - args->left->size();
                                            const column_t &src_col = (*args->right)[right_col_idx];
                                            if (args->build_left) {
                                                (*args->thread_results)[res_col_id].append(src_col.pages[probe_pidx]->value_buffer[probe_posidx]);
                                            } else {
                                                (*args->thread_results)[res_col_id].append(src_col.pages[pidx]->value_buffer[posidx]);
                                            }
                                        }
                                    }
                                }
                            } else if (vt.is_null()) {
                                continue;
                            } else {
                                throw std::runtime_error("wrong type of field");
                            }
                        }
                    }
                    return nullptr;
                };
                //Probe parallel.
                auto probe_with_column_ht = [&](const column_t &probe_col) {
                    //atomic counter for work stealing.
                    std::atomic<size_t> page_counter{0};
                    std::vector<std::vector<column_t>> thread_results(NUM_THREADS);

                    for (int i = 0; i < NUM_THREADS; ++i) {
                        //Initialize result columns with the datatypes and if they have no nulls.
                        thread_results[i].resize(output_attrs.size());
                        for (size_t j = 0; j < output_attrs.size(); ++j) {
                            auto [col_idx, dtype] = output_attrs[j];
                            thread_results[i][j].type = dtype;
                            if (col_idx < left.size() && left[col_idx].is_no_null) {
                                thread_results[i][j].is_no_null = true;
                                thread_results[i][j].original_pages = left[col_idx].original_pages;
                            } else {
                                size_t right_col_idx = col_idx - left.size();
                                if (right_col_idx < right.size() && right[right_col_idx].is_no_null) {
                                    thread_results[i][j].is_no_null = true;
                                    thread_results[i][j].original_pages = right[right_col_idx].original_pages;
                                }
                            }
                        }
                    }
                    pthread_t threads[NUM_THREADS];
                    ProbeWorkerArgs thread_args[NUM_THREADS];

                    for (int i = 0; i < NUM_THREADS; i++) {
                        thread_args[i].probe_col = &probe_col;
                        thread_args[i].ht = &ht;
                        thread_args[i].page_counter = &page_counter;
                        thread_args[i].thread_results = &thread_results[i];
                        thread_args[i].left = &left;
                        thread_args[i].right = &right;
                        thread_args[i].output_attrs = &output_attrs;
                        thread_args[i].build_left = build_left;
                        thread_args[i].left_col = left_col;
                        thread_args[i].right_col = right_col;

                        pthread_create(&threads[i], NULL, probe_worker, &thread_args[i]);
                    }

                    for (int i = 0; i < NUM_THREADS; ++i) {
                        pthread_join(threads[i], NULL);
                    }

                    for (int i = 0; i < NUM_THREADS; ++i) {
                        for (size_t res_col_id = 0; res_col_id < output_attrs.size(); ++res_col_id) {
                            for (size_t p = 0; p < thread_results[i][res_col_id].pages.size(); ++p) {
                                results[res_col_id].pages.push_back(thread_results[i][res_col_id].pages[p]);
                            }
                            thread_results[i][res_col_id].pages.clear();
                        }
                    }
                };

                if (build_left) {
                    probe_with_column_ht(right[right_col]);
                } else {
                    probe_with_column_ht(left[left_col]);
                }
            }
        }
    };

    std::vector<column_t> execute_hash_join(const Plan&          plan,
        const JoinNode&                                  join,
        const std::vector<std::tuple<size_t, DataType>>& output_attrs) {
        auto                           left_idx    = join.left;
        auto                           right_idx   = join.right;
        auto&                          left_node   = plan.nodes[left_idx];
        auto&                          right_node  = plan.nodes[right_idx];
        auto&                          left_types  = left_node.output_attrs;
        auto&                          right_types = right_node.output_attrs;
        auto                           left        = execute_impl(plan, left_idx);
        auto                           right       = execute_impl(plan, right_idx);
        std::vector<column_t> results;
        
        //Initialize result columns with the datatypes and if they have no nulls.
        results.resize(output_attrs.size());
        for (size_t i = 0; i < output_attrs.size(); ++i) {
            auto [col_idx, dtype] = output_attrs[i];
            results[i].type = dtype;
            
            if (col_idx < left.size() && left[col_idx].is_no_null) {
                results[i].is_no_null = true;
                results[i].original_pages = left[col_idx].original_pages;
            } else {
                size_t right_col_idx = col_idx - left.size();
                if (right_col_idx < right.size() && right[right_col_idx].is_no_null) {
                    results[i].is_no_null = true;
                    results[i].original_pages = right[right_col_idx].original_pages;
                }
            }
        }

        JoinAlgorithm join_algorithm{.build_left = join.build_left,
            .left                                = left,
            .right                               = right,
            .results                             = results,
            .left_col                            = join.left_attr,
            .right_col                           = join.right_attr,
            .output_attrs                        = output_attrs};
        if (join.build_left) {
            switch (std::get<1>(left_types[join.left_attr])) {
            case DataType::INT32:   join_algorithm.run<int32_t>(); break;
            case DataType::INT64:   join_algorithm.run<int64_t>(); break;
            case DataType::FP64:    join_algorithm.run<double>(); break;
            case DataType::VARCHAR: join_algorithm.run<std::string>(); break;
            }
        } else {
            switch (std::get<1>(right_types[join.right_attr])) {
            case DataType::INT32:   join_algorithm.run<int32_t>(); break;
            case DataType::INT64:   join_algorithm.run<int64_t>(); break;
            case DataType::FP64:    join_algorithm.run<double>(); break;
            case DataType::VARCHAR: join_algorithm.run<std::string>(); break;
            }
        }

        return results;
    }

    std::vector<column_t> execute_scan(const Plan&               plan,
        const ScanNode&                                  scan,
        const std::vector<std::tuple<size_t, DataType>>& output_attrs) {
        auto                           table_id = scan.base_table_id;
        auto&                          input    = plan.inputs[table_id];

        auto result = column_t_copy_scan(table_id, input, output_attrs);
        return result;
    }

    std::vector<column_t> execute_impl(const Plan& plan, size_t node_idx) {
        auto& node = plan.nodes[node_idx];
        return std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, JoinNode>) {
                    return execute_hash_join(plan, value, node.output_attrs);
                } else {
                    return execute_scan(plan, value, node.output_attrs);
                }
            },
            node.data);
    }

    ColumnarTable execute(const Plan& plan, [[maybe_unused]] void* context) {
        namespace views = ranges::views;
        auto ret        = execute_impl(plan, plan.root);
        auto ret_types  = plan.nodes[plan.root].output_attrs
                    | views::transform([](const auto& v) { return std::get<1>(v); })
                    | ranges::to<std::vector<DataType>>();

        ColumnarTable col_table = to_columnar_from_column(ret, plan);
        return col_table;
    }

    void* build_context() {
        return nullptr;
    }

    void destroy_context([[maybe_unused]] void* context) {}

    } // namespace Contest
