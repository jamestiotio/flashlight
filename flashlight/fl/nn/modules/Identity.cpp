/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "flashlight/fl/nn/modules/Identity.h"

namespace fl {

std::vector<Variable> Identity::forward(const std::vector<Variable>& inputs) {
  return inputs;
};

std::string Identity::prettyString() const {
  return "Identity";
};

} // namespace fl
