#pragma once
#include <vector>
#include <cstdint>
#include <cassert>

namespace hft {

// Segment tree that stores per-leaf counts and supports finding
// the first index with count>0 within a closed range [l, r].
class SegmentTree {
    size_t n_ = 0;             // power-of-two base
    std::vector<uint32_t> t_;  // 1-indexed tree array [1..2*n_-1]

public:
    SegmentTree() = default;

    explicit SegmentTree(size_t leaves) { init(leaves); }

    void init(size_t leaves) {
        n_ = 1;
        while (n_ < leaves) n_ <<= 1;
        t_.assign(2 * n_, 0);
    }

    inline size_t size() const noexcept { return n_; }

    // Set leaf idx to val (idx in [0, n_real))
    void set(size_t idx, uint32_t val) {
        size_t pos = idx + n_;
        t_[pos] = val;
        for (pos >>= 1; pos >= 1; pos >>= 1) {
            t_[pos] = t_[pos << 1] + t_[(pos << 1) | 1];
            if (pos == 1) break;
        }
    }

    // Returns true if there exists any leaf in [l, r] with value > 0
    bool any(size_t l, size_t r) const {
        if (l > r) return false;
        size_t L = l + n_, R = r + n_;
        uint32_t acc = 0;
        while (L <= R) {
            if (L & 1) acc += t_[L++];
            if (!(R & 1)) acc += t_[R--];
            if (acc) return true;
            L >>= 1; R >>= 1;
        }
        return false;
    }

    // Find first index i in [l, r] with leaf value > 0, or -1 if none.
    int find_first(size_t l, size_t r) const {
        if (l > r) return -1;
        if (!any(l, r)) return -1;
        // descend from root
        size_t node = 1, left = 0, right = n_ - 1;
        while (left != right) {
            size_t mid = (left + right) >> 1;
            size_t leftNode = node << 1;
            // if left child intersects [l, r] and has sum>0, go left; else go right
            bool left_intersects = (l <= mid);
            bool right_intersects = (r > mid);
            if (left_intersects && t_[leftNode] > 0) {
                node = leftNode;
                right = mid;
            } else {
                node = leftNode | 1;
                left = mid + 1;
            }
        }
        return static_cast<int>((left >= l && left <= r) ? left : -1);
    }
};

} // namespace hft

