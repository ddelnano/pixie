#include <cmath>

#include "src/common/base/histo/indexers.h"

namespace px
{

const double kLogBase2E = 1.0 / std::log(2.0);

double Base2LogIndexer::ComputeScaleFactor(int32_t scale)
{
  return std::scalbn(kLogBase2E, scale);
}

int32_t Base2LogIndexer::ComputeIndex(double value) const
{
  auto abs_value = std::fabs(value);
  if (abs_value <= 0)
  {
    return -1;
  }

  return static_cast<int32_t>(std::ceil(std::log(abs_value) * scale_factor_)) - 1;
}

int32_t Base2ExpIndexer::ComputeIndex(double value) const
{
  const double abs_value = std::fabs(value);
  // Note: std::frexp() rounds submnormal values to the smallest normal value and returns an
  // exponent corresponding to fractions in the range [0.5, 1), whereas an exponent for the range
  // [1, 2), so subtract 1 from the exponent immediately.
  int exp;
  double frac = std::frexp(abs_value, &exp);
  exp--;

  if (frac == 0.5)
  {
    // Special case for powers of two: they fall into the bucket numbered one less.
    exp--;
  }
  return exp >> -scale_;
}

} // namespace px
