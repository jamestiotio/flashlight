/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "flashlight/fl/tensor/TensorBackend.h"

namespace fl {
namespace detail {

bool areBackendsEqual(const Tensor& a, const Tensor& b) {
  return a.backendType() == b.backendType();
}

} // namespace detail

bool TensorBackend::isDataTypeSupported(const fl::dtype& dtype) const {
  bool supported = this->supportsDataType(dtype);
  for (auto& p : extensions_) {
    supported &= p.second->isDataTypeSupported(dtype);
  }
  return supported;
}

} // namespace fl
