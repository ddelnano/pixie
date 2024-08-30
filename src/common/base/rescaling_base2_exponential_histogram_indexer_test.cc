// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "src/common/base/rescaling_base2_exponential_histogram_indexer.h"

#include <gtest/gtest.h>

#include "src/common/testing/testing.h"
using namespace px;

using ::testing::ElementsAre;

TEST(BucketsTest, IncrementBasic) {
    auto buckets = Buckets(4);
    buckets.Increment(2.0);
    EXPECT_EQ(buckets.Size(), 1);
}

TEST(RescalingExponentialHistogramTest, TestAlternatingGrowth) {
  auto histo = RescalingBase2ExponentialHistogram(4);
  histo.Increment(2.0);
  histo.Increment(4.0);
  histo.Increment(1.0);

  EXPECT_EQ(histo.GetPositiveBuckets().Size(), 3);
  EXPECT_EQ(histo.GetPositiveBuckets().Offset(), -1);
  EXPECT_EQ(histo.Scale(), 0);
  EXPECT_THAT(histo.GetPositiveBuckets().GetCounts(), ElementsAre(1, 1, 1));
}

TEST(RescalingExponentialHistogramTest, TestAlternatingGrowth2) {
  auto histo = RescalingBase2ExponentialHistogram(4);
  histo.Increment(2.0);
  histo.Increment(2.0);
  histo.Increment(2.0);
  histo.Increment(1.0);
  histo.Increment(8.0);
  histo.Increment(0.5);

  EXPECT_EQ(histo.GetPositiveBuckets().Offset(), -1);
  EXPECT_EQ(histo.GetPositiveBuckets().Size(), 3);
  EXPECT_EQ(histo.Scale(), -1);
  EXPECT_THAT(histo.GetPositiveBuckets().GetCounts(), ElementsAre(2, 3, 1));
}

class RescalingBase2ExpHistogramNegScaleTest :
    public ::testing::Test,
    public ::testing::WithParamInterface<std::vector<double>> {
};

TEST_P(RescalingBase2ExpHistogramNegScaleTest, TestNegOne) {
  auto values = GetParam();
  auto histo = RescalingBase2ExponentialHistogram(2);
  for (auto value : values) {
    histo.Increment(value);
  }
  EXPECT_EQ(histo.GetPositiveBuckets().Offset(), -1);
  EXPECT_EQ(histo.GetPositiveBuckets().Size(), 2);
  EXPECT_EQ(histo.Scale(), -1);
  EXPECT_THAT(histo.GetPositiveBuckets().GetCounts(), ElementsAre(2, 1));
}

INSTANTIATE_TEST_SUITE_P(RescalingBase2ExpHistogramNegScaleTestSuite,
                         RescalingBase2ExpHistogramNegScaleTest,
                         ::testing::ValuesIn(std::vector<std::vector<double>>{
                             {1, 0.5, 2},
                             {1, 2, 0.5},
                             {2, 0.5, 1},
                             {2, 1, 0.5},
                             {0.5, 1, 2},
                             {0.5, 2, 1},
                         }));

class RescalingBase2ExpHistogramNegOnePosTest :
    public ::testing::Test,
    public ::testing::WithParamInterface<std::vector<double>> {
};

TEST_P(RescalingBase2ExpHistogramNegOnePosTest, TestNegOnePos) {
  auto values = GetParam();
  auto histo = RescalingBase2ExponentialHistogram(2);
  for (auto value : values) {
    histo.Increment(value);
  }
  EXPECT_EQ(histo.Scale(), -1);
  EXPECT_EQ(histo.GetPositiveBuckets().Offset(), -1);
  EXPECT_EQ(histo.GetPositiveBuckets().Size(), 2);
  EXPECT_THAT(histo.GetPositiveBuckets().GetCounts(), ElementsAre(1, 2));
}

INSTANTIATE_TEST_SUITE_P(RescalingBase2ExpHistogramNegScaleTestSuite,
                         RescalingBase2ExpHistogramNegOnePosTest,
                         ::testing::ValuesIn(std::vector<std::vector<double>>{
                             {1, 2, 4},
                             {1, 4, 2},
                             {2, 4, 1},
                             {2, 1, 4},
                             {4, 1, 2},
                             {4, 2, 1},
                         }));

class RescalingBase2ExpHistogramNegOneNegTest :
    public ::testing::Test,
    public ::testing::WithParamInterface<std::vector<double>> {
};

TEST_P(RescalingBase2ExpHistogramNegOneNegTest, TestNegOneNeg) {
  auto values = GetParam();
  auto histo = RescalingBase2ExponentialHistogram(2);
  for (auto value : values) {
    histo.Increment(value);
  }
  EXPECT_EQ(histo.Scale(), -1);
  EXPECT_EQ(histo.GetPositiveBuckets().Offset(), -2);
  EXPECT_EQ(histo.GetPositiveBuckets().Size(), 2);
  EXPECT_THAT(histo.GetPositiveBuckets().GetCounts(), ElementsAre(1, 2));
}

INSTANTIATE_TEST_SUITE_P(RescalingBase2ExpHistogramNegOneNegTestSuite,
                         RescalingBase2ExpHistogramNegOneNegTest,
                         ::testing::ValuesIn(std::vector<std::vector<double>>{
                             {1, 0.5, 0.25},
                             {1, 0.25, 0.5},
                             {0.5, 0.25, 1},
                             {0.5, 1, 0.25},
                             {0.25, 1, 0.5},
                             {0.25, 0.5, 1},
                         }));

