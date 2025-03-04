/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "flashlight/pkg/speech/runtime/Helpers.h"

#include <numeric>
#include <random>
#include <utility>

#include <glog/logging.h>

#include "flashlight/lib/text/String.h"

#ifdef FL_BUILD_FB_DEPENDENCIES
  #include "deeplearning/projects/flashlight/fb/EverstoreDataset.h"
#endif

using fl::lib::format;
using fl::lib::replaceAll;
using fl::lib::text::DictionaryMap;
using fl::lib::text::LexiconMap;

namespace fl {

namespace pkg {
namespace speech {

template <class T>
std::vector<std::string> tensorMatrixToStrings(
    const Tensor& tensor,
    T terminator) {
  int L = tensor.dim(0); // padded length of string
  int N = tensor.dim(1); // number of strings
  std::vector<std::string> result;
  auto values = tensor.toHostVector<T>();
  for (int i = 0; i < N; ++i) {
    const T* row = &values[i * L];
    int len = 0;
    while (len < L && row[len] != terminator) {
      ++len;
    }
    result.emplace_back(row, row + len);
  }
  return result;
}

std::string
getRunFile(const std::string& name, int runidx, const fs::path& runpath) {
  auto fname = format("%03d_%s", runidx, name.c_str());
  return runpath / fname;
};

std::string cleanFilepath(const fs::path& in) {
  std::string replace = in;
  std::string sep(1, fs::path::preferred_separator);
  replaceAll(replace, sep, "#");
  return replace;
}

std::string serializeGflags(const std::string& separator /* = "\n" */) {
  std::stringstream serialized;
  std::vector<gflags::CommandLineFlagInfo> allFlags;
  gflags::GetAllFlags(&allFlags);
  std::string currVal;
  auto& deprecatedFlags = detail::getDeprecatedFlags();
  for (auto itr = allFlags.begin(); itr != allFlags.end(); ++itr) {
    // Check if the flag is deprecated - if so, skip it
    if (deprecatedFlags.find(itr->name) == deprecatedFlags.end()) {
      gflags::GetCommandLineOption(itr->name.c_str(), &currVal);
      serialized << "--" << itr->name << "=" << currVal << separator;
    }
  }
  return serialized.str();
}

std::unordered_set<int64_t>
getTrainEvalIds(int64_t dsSize, double pctTrainEval, int64_t seed) {
  std::mt19937_64 rng(seed);
  std::bernoulli_distribution dist(pctTrainEval / 100.0);
  std::unordered_set<int64_t> result;
  for (int64_t i = 0; i < dsSize; ++i) {
    if (dist(rng)) {
      result.insert(i);
    }
  }
  return result;
}

std::vector<std::string> readSampleIds(const Tensor& tensor) {
  return tensorMatrixToStrings<char>(tensor, '\0');
}

std::shared_ptr<fl::Dataset> createDataset(
    const std::vector<fs::path>& paths,
    const fs::path& rootDir /* = "" */,
    int batchSize /* = 1 */,
    const fl::Dataset::DataTransformFunction& inputTransform /* = nullptr */,
    const fl::Dataset::DataTransformFunction& targetTransform /* = nullptr */,
    const fl::Dataset::DataTransformFunction& wordTransform /* = nullptr */,
    const std::tuple<int, int, int>& padVal /* = {0, -1, -1} */,
    int worldRank /* = 0 */,
    int worldSize /* = 1 */,
    const bool allowEmpty /* = false */,
    const std::string& batchingStrategy /* kBatchStrategyNone */,
    int maxDurationPerBatch /* = 0 */) {
  std::vector<std::shared_ptr<const fl::Dataset>> allListDs;
  std::vector<float> sizes;
  for (auto& path : paths) {
    std::shared_ptr<ListFileDataset> curListDs;
    if (FLAGS_everstoredb) {
#ifdef FL_BUILD_FB_DEPENDENCIES
      curListDs = std::make_shared<fl::pkg::speech::EverstoreDataset>(
          rootDir / path,
          inputTransform,
          targetTransform,
          wordTransform,
          FLAGS_use_memcache);
#else
      LOG(FATAL) << "EverstoreDataset not supported: "
                 << "build with -DFL_BUILD_FB_DEPENDENCIES";
#endif
    } else {
      curListDs = std::make_shared<ListFileDataset>(
          rootDir / path, inputTransform, targetTransform, wordTransform);
    }

    allListDs.emplace_back(curListDs);
    sizes.reserve(sizes.size() + curListDs->size());
    for (int64_t i = 0; i < curListDs->size(); ++i) {
      sizes.push_back(curListDs->getInputSize(i));
    }
  }

  // Order Dataset
  std::vector<int64_t> sortedIds(sizes.size());
  std::iota(sortedIds.begin(), sortedIds.end(), 0);
  auto cmp = [&sizes](const int64_t& l, const int64_t& r) {
    return sizes[l] > sizes[r];
  };
  if (batchingStrategy == kBatchStrategyRand ||
      batchingStrategy == kBatchStrategyRandDynamic) {
    auto rng = std::mt19937(sizes.size());
    for (int i = sizes.size(); i >= 1; i--) {
      int index = rng() % sizes.size();
      std::swap(sortedIds[i - 1], sortedIds[index]);
      std::swap(sizes[i - 1], sizes[index]);
    }
  } else {
    std::stable_sort(sortedIds.begin(), sortedIds.end(), cmp);
    std::stable_sort(sizes.begin(), sizes.end(), std::greater<float>());
  }

  auto concatListDs = std::make_shared<fl::ConcatDataset>(allListDs);

  auto sortedDs =
      std::make_shared<fl::ResampleDataset>(concatListDs, sortedIds);

  int inPad, tgtPad, wrdPad;
  std::tie(inPad, tgtPad, wrdPad) = padVal;
  auto batchFns = std::vector<fl::Dataset::BatchFunction>{
      [inPad](const std::vector<Tensor>& tensor) {
        return fl::join(tensor, inPad, 3);
      },
      [tgtPad](const std::vector<Tensor>& tensor) {
        return fl::join(tensor, tgtPad, 1);
      },
      [wrdPad](const std::vector<Tensor>& tensor) {
        return fl::join(tensor, wrdPad, 1);
      },
      [](const std::vector<Tensor>& tensor) { return fl::join(tensor, 0, 1); },
      [](const std::vector<Tensor>& tensor) { return fl::join(tensor, 0, 1); },
      [](const std::vector<Tensor>& tensor) { return fl::join(tensor, 0, 1); },
      [](const std::vector<Tensor>& tensor) { return fl::join(tensor, 0, 1); }};
  if (batchingStrategy == kBatchStrategyDynamic ||
      batchingStrategy == kBatchStrategyRandDynamic) {
    // Partition the dataset and distribute
    auto result = fl::dynamicPartitionByRoundRobin(
        sizes, worldRank, worldSize, maxDurationPerBatch, allowEmpty);
    auto partitions = result.first;
    auto batchSizes = result.second;
    auto paritionDs =
        std::make_shared<fl::ResampleDataset>(sortedDs, partitions);
    // Batch the dataset
    return std::make_shared<fl::BatchDataset>(paritionDs, batchSizes, batchFns);
  } else if (
      batchingStrategy == kBatchStrategyNone ||
      batchingStrategy == kBatchStrategyRand) {
    // Partition the dataset and distribute
    auto partitions = fl::partitionByRoundRobin(
        sortedDs->size(), worldRank, worldSize, batchSize, allowEmpty);
    auto paritionDs =
        std::make_shared<fl::ResampleDataset>(sortedDs, partitions);
    // Batch the dataset
    return std::make_shared<fl::BatchDataset>(
        paritionDs, batchSize, fl::BatchDatasetPolicy::INCLUDE_LAST, batchFns);
  } else {
    throw std::runtime_error(
        "Unsupported batching strategy '" + batchingStrategy + "'");
  }
}

std::shared_ptr<fl::Dataset> loadPrefetchDataset(
    std::shared_ptr<fl::Dataset> dataset,
    int prefetchThreads,
    bool shuffle,
    int shuffleSeed /*= 0 */) {
  if (shuffle) {
    dataset = std::make_shared<fl::ShuffleDataset>(dataset, shuffleSeed);
  }
  if (prefetchThreads > 0) {
    dataset = std::make_shared<fl::PrefetchDataset>(
        dataset, prefetchThreads, prefetchThreads /* prefetch size */);
  }
  return dataset;
}

std::vector<std::pair<std::string, std::string>> parseValidSets(
    const std::string& valid) {
  auto validSets = fl::lib::split(',', fl::lib::trim(valid), true);
  std::vector<std::pair<std::string, std::string>> validTagSets;
  for (const auto& s : validSets) {
    // assume the format is tag:filepath
    auto ts = fl::lib::splitOnAnyOf(":", s);
    if (ts.size() == 1) {
      validTagSets.emplace_back(s, s);
    } else {
      validTagSets.emplace_back(ts[0], ts[1]);
    }
  }
  return validTagSets;
}
} // namespace speech
} // namespace pkg
} // namespace fl
