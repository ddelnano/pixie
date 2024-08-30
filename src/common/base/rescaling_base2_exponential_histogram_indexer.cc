// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "src/common/base/rescaling_base2_exponential_histogram_indexer.h"

#include <cmath>

namespace px
{

const double kLogBase2E = 1.0 / std::log(2.0);

int RescalingBase2ExponentialHistogram::ChangeScale(int32_t low, int32_t high) {
  int change = 0;

  while (high - low >= max_size_) {
    low >>= 1;
    high >>= 1;
    change++;
  }
  return change;
}

void RescalingBase2ExponentialHistogram::Downscale(int change) {
  if (change == 0) {
    return;
  }
  auto scale = indexer_->Scale();
  auto new_scale = scale - change;
  LOG(INFO) << absl::Substitute("Downscaling by $0 $1->$2", change, scale, new_scale);

  if (new_scale <= 0) {
    indexer_ = std::make_unique<Base2ExpIndexer>(new_scale);
  } else {
    indexer_ = std::make_unique<Base2LogIndexer>(new_scale);
  }

  pos_buckets_.Downscale(change);
  /* neg_buckets_.Downscale(change); */
}

void RescalingBase2ExponentialHistogram::IncrementBucket(Buckets& buckets, double value) {
  auto index = indexer_->ComputeIndex(value);

  auto [bounds, success] = buckets.Increment(index);
  if (success) {
    return;
  }
  Downscale(ChangeScale(bounds.low, bounds.high));

  // Index needs to be recomputed since the scale has changed
  index = indexer_->ComputeIndex(value);
  // Figure out how to handle any error here. The go version panic's if this fails.
  buckets.Increment(index);
}

void RescalingBase2ExponentialHistogram::Increment(double value) {

  auto& bucket = value > 0 ? pos_buckets_ : neg_buckets_;

  IncrementBucket(bucket, value);
};

}  // namespace px
