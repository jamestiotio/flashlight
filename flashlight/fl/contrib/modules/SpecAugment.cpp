/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <sstream>
#include <stdexcept>

#include "flashlight/fl/contrib/modules/SpecAugment.h"

#include "flashlight/fl/tensor/Index.h"

namespace fl {

SpecAugment::SpecAugment(
    int tWarpW,
    int fMaskF,
    int nFMask,
    int tMaskT,
    float tMaskP,
    int nTMask,
    MaskingStrategy mStrategy /* = MaskingStrategy::ZERO */)
    : timeWarpW_(tWarpW),
      freqMaskF_(fMaskF),
      numFreqMask_(nFMask),
      timeMaskT_(tMaskT),
      timeMaskP_(tMaskP),
      numTimeMask_(nTMask),
      maskStrategy_(mStrategy) {
  if (numFreqMask_ > 0 && freqMaskF_ <= 0) {
    throw std::invalid_argument("invalid arguments for frequency masking.");
  }
  if (numTimeMask_ > 0 && timeMaskT_ <= 0) {
    throw std::invalid_argument("invalid arguments for time masking.");
  }
  if (numTimeMask_ > 0 && (timeMaskP_ <= 0 || timeMaskP_ > 1.0)) {
    throw std::invalid_argument("invalid arguments for time masking.");
  }
}

Variable SpecAugment::forward(const Variable& input) {
  if (input.isCalcGrad()) {
    throw std::invalid_argument(
        "input gradient calculation is not supported for SpecAugment.");
  }

  auto output = Variable(input.tensor(), false);
  if (!train_) {
    return output;
  }

  auto& opArr = output.tensor();

  double replaceVal = (maskStrategy_ == MaskingStrategy::GLOBAL_MEAN)
      ? fl::mean(input.tensor()).asScalar<double>()
      : 0.0;

  auto numFreqChans = input.dim(1); // number of frequency channels
  if (numFreqChans < freqMaskF_) {
    throw std::runtime_error("Invalid input frequency channels");
  }
  for (int i = 0; i < numFreqMask_; ++i) {
    auto f = generateRandomInt(0, freqMaskF_);
    auto f0 = generateRandomInt(0, numFreqChans - f);
    opArr(fl::span, fl::range(f0, f0 + f + 1)) = replaceVal;
  }

  auto numTimeSteps = input.dim(0); // number of time steps
  // an upper bound on the time mask
  int T = std::min(timeMaskT_, static_cast<int>(numTimeSteps * timeMaskP_));
  if (T > 0) {
    for (int i = 0; i < numTimeMask_; ++i) {
      auto t = generateRandomInt(0, T);
      auto t0 = generateRandomInt(0, numTimeSteps - t);
      opArr(fl::range(t0, t0 + t + 1)) = replaceVal;
    }
  }

  return output;
}

int SpecAugment::generateRandomInt(int low, int high) {
  std::uniform_int_distribution<int> uniformDist(low, high - 1);
  return uniformDist(eng_);
}

std::string SpecAugment::prettyString() const {
  std::ostringstream ss;
  ss << "SpecAugment ( ";
  ss << "W: " << timeWarpW_ << ", ";
  ss << "F: " << freqMaskF_ << ", ";
  ss << "mF: " << numFreqMask_ << ", ";
  ss << "T: " << timeMaskT_ << ", ";
  ss << "p: " << timeMaskP_ << ", ";
  ss << "mT: " << numTimeMask_;
  ss << " )";
  return ss.str();
}
} // namespace fl
