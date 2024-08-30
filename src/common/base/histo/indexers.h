#pragma once

#include <cstdint>

        // MapToIndex maps positive floating point values to indexes
        // corresponding to Scale().  Implementations are not expected
        // to handle zeros, +Inf, NaN, or negative values.
        /* MapToIndex(value float64) int32 */

        // LowerBoundary returns the lower boundary of a given bucket
        // index.  The index is expected to map onto a range that is
        // at least partially inside the range of normalized floating
        // point values.  If the corresponding bucket's upper boundary
        // is less than or equal to 0x1p-1022, ErrUnderflow will be
        // returned.  If the corresponding bucket's lower boundary is
        // greater than math.MaxFloat64, ErrOverflow will be returned.
        /* LowerBoundary(index int32) (float64, error) */

        // Scale returns the parameter that controls the resolution of
        // this mapping.  For details see:
        // https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/metrics/datamodel.md#exponential-scale
        /* Scale() int32 */

namespace px
{

class HistogramIndexer {
  public:
    virtual ~HistogramIndexer() = default;
    virtual int32_t ComputeIndex(double value) const = 0;
    int32_t Scale() { return scale_; }

  protected:
    HistogramIndexer(int32_t scale, double scale_factor) : scale_(scale), scale_factor_(scale_factor) {}
    int32_t scale_;
    double scale_factor_;
};

class Base2LogIndexer : public HistogramIndexer {
  public:
    Base2LogIndexer(int32_t scale) : HistogramIndexer(scale, ComputeScaleFactor(scale)) {}

    double ComputeScaleFactor(int32_t scale);
    int32_t ComputeIndex(double value) const override;
};

class Base2ExpIndexer : public HistogramIndexer {
  public:
    Base2ExpIndexer(int32_t scale) : HistogramIndexer(scale, 0) {}
    int32_t ComputeIndex(double value) const override;
};

}
