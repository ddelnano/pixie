// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "src/common/base/base.h"
#include "src/common/base/histo/indexers.h"

namespace px
{

// TODO: Document why 20 is the max scale.
constexpr static int kMaxHistoScale = 20;

struct BucketBounds {
  int32_t low;
  int32_t high;
};

class Buckets {
  public:
    explicit Buckets(int max_size) : max_size_(max_size), start_index_(0), end_index_(0), base_index_(0) { }

    int32_t Size() {
      return end_index_ - start_index_ + 1;
    }

    int32_t Offset() {
      return start_index_;
    }

    std::vector<uint64_t> GetCounts() {
      LOG(INFO) << "Final buckets: " << buckets_ << " start_index_: " << start_index_ << " end_index_: " << end_index_ << " base_index_: " << base_index_;
      std::vector<uint64_t> counts;
      int index = start_index_ - base_index_;
      for (int i = start_index_; i <= end_index_; i++) {
        if (index < 0) {
          index += buckets_.size();
        } else if (index >= int(buckets_.size())) {
          index -= buckets_.size();
        }
        LOG(INFO) << "Index: " << index;
        counts.push_back(buckets_[index]);
        index++;
      }
      return counts;
    }

    void Rotate() {
      auto bias = base_index_ - start_index_;
      if (bias == 0) {
        return;
      }
      DCHECK_LT(bias, buckets_.size());
      base_index_ = start_index_;

      auto begin = buckets_.begin();
      std::reverse(begin, buckets_.end());
      std::reverse(begin, begin + bias);
      std::reverse(begin + bias, buckets_.end());
    }

    std::pair<BucketBounds, bool> Increment(int32_t index) {
      // The first bucket is always at index 0.
      if (buckets_.size() == 0) {
        start_index_ = index;
        base_index_ = start_index_;
        end_index_ = start_index_;

        // On first observation resize to max_size
        buckets_.resize(max_size_);
      } else if (index < start_index_) {
        // Rescale is needed since the index is out of bounds
        if (end_index_ - index >= max_size_) {
          return {{index, end_index_}, false};
        }

        start_index_ = index;
      } else if (index > end_index_) {
        // Rescale is needed since the index is out of bounds
        if (index - start_index_ >= max_size_) {
          return {{start_index_, index}, false};
        }

        end_index_ = index;
      }

      DCHECK_LE(end_index_ - start_index_, max_size_);
      auto bucket_index = index - base_index_;
      if (bucket_index < 0) {
        bucket_index += buckets_.size();
      }
      LOG(INFO) << "Incrementing buckets_ index: " << bucket_index << " bucket idx: " << index;
      buckets_[bucket_index] += 1;
      LOG(INFO) << buckets_;
      return {{}, true};
    }

    void Downscale(int change) {
      LOG(INFO) << "Downscale: " << change;
      LOG(INFO) << "start_index_: " << start_index_ << " end_index_: " << end_index_ << " base_index_: " << base_index_;
      Rotate();

      LOG(INFO) << buckets_;
      auto size = 1 + end_index_ - start_index_;
      int32_t inpos = 1;
      int32_t outpos = 0;
      auto shifts = 1 << change;
      for (auto down_pos = start_index_; down_pos < end_index_; ) {

        auto mod = down_pos % shifts;
        if (mod < 0) {
          mod += shifts;
        }
        for (auto i = mod; i < shifts && inpos < size; i++) {
          if (inpos == outpos) {
            inpos++;
            down_pos++;
            continue;
          }
          buckets_[outpos] += buckets_[inpos];
          buckets_[inpos] = 0;

          inpos++;
          down_pos++;
        }
        /* if (outpos == 0 && inpos == 0) { */
        /*   continue; */
        /* } */
        outpos++;
      }

      LOG(INFO) << buckets_;
      LOG(INFO) << "Modifying indexes PREVIOUS: start_index_: " << start_index_ << " end_index_: " << end_index_ << " base_index_: " << base_index_;
      start_index_ >>= change;
      end_index_ >>= change;
      base_index_ = start_index_;
      LOG(INFO) << "AFTER: start_index_: " << start_index_ << " end_index_: " << end_index_ << " base_index_: " << base_index_;
    }
  
  private:
    int32_t max_size_;
    int32_t start_index_;
    int32_t end_index_;
    int32_t base_index_;
    std::vector<uint64_t> buckets_;
};

class RescalingBase2ExponentialHistogram {

  public:
    explicit RescalingBase2ExponentialHistogram(int max_size) : max_size_(max_size), pos_buckets_(max_size), neg_buckets_(max_size) {
      indexer_ = std::make_unique<Base2LogIndexer>(kMaxHistoScale);
    }

    void Increment(double value);

    Buckets GetPositiveBuckets() {
      return pos_buckets_;
    }

    Buckets GetNegativeBuckets() {
      return neg_buckets_;
    }

    int Scale() {
      return indexer_->Scale();
    }

  private:
    void Downscale(int change);
    void IncrementBucket(Buckets& buckets, double value);
    int ChangeScale(int32_t low, int32_t high);

    // TODO: decide if this should be a reference
    std::unique_ptr<HistogramIndexer> indexer_;
    int32_t max_size_;
    Buckets pos_buckets_;
    Buckets neg_buckets_;
};

/*
 * An indexer for base2 exponential histograms. It is used to calculate index for a given value and
 * scale.
 */
/*class Base2ExponentialHistogramIndexer */
/*{ */
/*public: */
/*  /1* */
/*   * Construct a new indexer for a given scale. */
/*   *1/ */
/*  explicit Base2ExponentialHistogramIndexer(int32_t scale = 0); */
/*  Base2ExponentialHistogramIndexer(const Base2ExponentialHistogramIndexer &other) = default; */
/*  Base2ExponentialHistogramIndexer &operator=(const Base2ExponentialHistogramIndexer &other) = */
/*      default; */

/*  /1** */
/*   * Compute the index for the given value. */
/*   * */
/*   * @param value Measured value (must be non-zero). */
/*   * @return the index of the bucket which the value maps to. */
/*   *1/ */
/*  int32_t ComputeIndex(double value) const; */

/*private: */
/*  int32_t scale_; */
/*  double scale_factor_; */
/*}; */

}  // namespace px
