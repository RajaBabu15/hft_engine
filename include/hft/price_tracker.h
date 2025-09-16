#pragma once

#include "hft/types.h"

#include <vector>
#include <algorithm>

namespace hft {

    template<bool IsMaxTree>
    class PriceTracker {
    public:
        explicit PriceTracker(size_t price_levels)
            : original_size_(price_levels), n_(1) {
            while (n_ < price_levels) n_ *= 2;
            price_tree_.resize(2 * n_);
            count_tree_.resize(2 * n_);

            const Price neutral_price = IsMaxTree ? 0 : UINT64_MAX;
            std::fill(price_tree_.begin(), price_tree_.end(), neutral_price);
            std::fill(count_tree_.begin(), count_tree_.end(), 0);
        }

        inline void update_level(size_t level_idx, Price price, uint32_t count) noexcept {
            if (level_idx >= original_size_) return;

            size_t tree_idx = n_ + level_idx;
            price_tree_[tree_idx] = price;
            count_tree_[tree_idx] = count;

            for (tree_idx /= 2; tree_idx >= 1; tree_idx /= 2) {
                const size_t left_child = tree_idx * 2;
                const size_t right_child = tree_idx * 2 + 1;

                const uint32_t left_count = count_tree_[left_child];
                const uint32_t right_count = count_tree_[right_child];
                const Price left_price = price_tree_[left_child];
                const Price right_price = price_tree_[right_child];

                if constexpr (IsMaxTree) {
                    if (left_count > 0 && right_count > 0) {
                        price_tree_[tree_idx] = std::max(left_price, right_price);
                        count_tree_[tree_idx] = left_count + right_count;
                    } else if (left_count > 0) {
                        price_tree_[tree_idx] = left_price;
                        count_tree_[tree_idx] = left_count;
                    } else if (right_count > 0) {
                        price_tree_[tree_idx] = right_price;
                        count_tree_[tree_idx] = right_count;
                    } else {
                        price_tree_[tree_idx] = 0;
                        count_tree_[tree_idx] = 0;
                    }
                } else {
                    if (left_count > 0 && right_count > 0) {
                        price_tree_[tree_idx] = std::min(left_price, right_price);
                        count_tree_[tree_idx] = left_count + right_count;
                    } else if (left_count > 0) {
                        price_tree_[tree_idx] = left_price;
                        count_tree_[tree_idx] = left_count;
                    } else if (right_count > 0) {
                        price_tree_[tree_idx] = right_price;  
                        count_tree_[tree_idx] = right_count;
                    } else {
                        price_tree_[tree_idx] = UINT64_MAX;
                        count_tree_[tree_idx] = 0;
                    }
                }
            }
        }

        inline Price get_best_price() const noexcept {
            return (count_tree_[1] > 0) ? price_tree_[1] : (IsMaxTree ? 0 : UINT64_MAX);
        }

        inline bool has_orders() const noexcept {
            return count_tree_[1] > 0;
        }

        inline void clear() noexcept {
            const Price neutral_price = IsMaxTree ? 0 : UINT64_MAX;
            std::fill(price_tree_.begin(), price_tree_.end(), neutral_price);
            std::fill(count_tree_.begin(), count_tree_.end(), 0);
        }

    private:
        const size_t original_size_;
        size_t n_;
        alignas(64) std::vector<Price> price_tree_;
        alignas(64) std::vector<uint32_t> count_tree_;
    };

    using BidTracker = PriceTracker<true>;
    using AskTracker = PriceTracker<false>;

} // namespace hft


