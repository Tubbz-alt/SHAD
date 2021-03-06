//===------------------------------------------------------------*- C++ -*-===//
//
//                                     SHAD
//
//      The Scalable High-performance Algorithms and Data Structure Library
//
//===----------------------------------------------------------------------===//
//
// Copyright 2018 Battelle Memorial Institute
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy
// of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.
//
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
#include <unordered_map>

#include <benchmark/benchmark.h>

#include "shad/data_structures/hashmap.h"
#include "shad/runtime/runtime.h"
#include "shad/util/measure.h"

static size_t MAP_SIZE = 100000;
static size_t NUM_ITER = 20;
static std::string FILE_NAME = "results_map_perf.txt";

using MapT = shad::Hashmap<int, int>;
using unit = std::chrono::microseconds;
static double secUnit = 1000000;
static MapT::SharedPtr mapPtr_;
static std::unordered_map<int, int> stdmap_;

/**
 * Create a Google Benchmark test fixture for data initialization.
 */
class TestFixture : public ::benchmark::Fixture {
 public:
  /**
   * Executes before each test function.
   */
  void SetUp(benchmark::State &state) override {
    auto ptr = MapT::Create(MAP_SIZE);
    struct Args {
      MapT::ObjectID oid1;
      size_t as;
    };
    auto propagateLambda = [](const Args &args) {
      MAP_SIZE = args.as;
      mapPtr_ = MapT::GetPtr(args.oid1);
    };
    Args args = {ptr->GetGlobalID(), MAP_SIZE};
    shad::rt::executeOnAll(propagateLambda, args);
  }

  /**
   * Executes after each test function.
   */
  void TearDown(benchmark::State &state) override {
    MapT::Destroy(mapPtr_->GetGlobalID());
  }
};

bool fake;

BENCHMARK_F(TestFixture, test_RawMap)(benchmark::State &state) {
  for (auto _ : state) {
    for (size_t i = 0; i < MAP_SIZE; i++) {
      stdmap_[i] = i;
    }
  }
}

BENCHMARK_F(TestFixture, test_ParallelAsyncRawMap)(benchmark::State &state) {
  auto feLambda = [](shad::rt::Handle &, const bool &, size_t i) {
    stdmap_[i] = i;
  };

  for (auto _ : state) {
    shad::rt::Handle handle;
    shad::rt::asyncForEachAt(handle, shad::rt::thisLocality(), feLambda, fake,
                             MAP_SIZE);
    shad::rt::waitForCompletion(handle);
  }
}

BENCHMARK_F(TestFixture, test_SerialInsert)(benchmark::State &state) {
  for (auto _ : state) {
    for (size_t i = 0; i < MAP_SIZE; i++) {
      mapPtr_->Insert(i, i);
    }
  }
}

void applyFun(const int &key, int &elem) { elem = key; }

BENCHMARK_F(TestFixture, test_AsyncInsert)(benchmark::State &state) {
  for (auto _ : state) {
    shad::rt::Handle handle;
    for (size_t i = 0; i < MAP_SIZE; i++) {
      mapPtr_->AsyncInsert(handle, i, i);
    }
    shad::rt::waitForCompletion(handle);
  }
}

BENCHMARK_F(TestFixture, test_ParallelAsyncInsert)(benchmark::State &state) {
  auto feLambda = [](shad::rt::Handle &handle, const bool &, size_t i) {
    mapPtr_->AsyncInsert(handle, i, i);
  };

  for (auto _ : state) {
    shad::rt::Handle handle;
    shad::rt::asyncForEachOnAll(handle, feLambda, fake, MAP_SIZE);
    shad::rt::waitForCompletion(handle);
  }
}

BENCHMARK_F(TestFixture, test_ParallelAsyncBufferedInsert)
(benchmark::State &state) {
  auto feLambda = [](shad::rt::Handle &handle, const bool &, size_t i) {
    mapPtr_->BufferedAsyncInsert(handle, i, i);
  };

  for (auto _ : state) {
    shad::rt::Handle handle;
    shad::rt::asyncForEachOnAll(handle, feLambda, fake, MAP_SIZE);
    shad::rt::waitForCompletion(handle);
    mapPtr_->WaitForBufferedInsert();
  }
}

BENCHMARK_F(TestFixture, test_AsyncBufferedInsert)(benchmark::State &state) {
  for (auto _ : state) {
    shad::rt::Handle handle;
    for (size_t i = 0; i < MAP_SIZE; i++) {
      mapPtr_->BufferedAsyncInsert(handle, i, i);
    }
    shad::rt::waitForCompletion(handle);
    mapPtr_->WaitForBufferedInsert();
  }
}

static void asyncApplyFun(shad::rt::Handle &, const int &key, int &elem) {
  elem = key;
}

static void asyncFEfun(shad::rt::Handle &, const int &key) {
  // do nothing
}

BENCHMARK_F(TestFixture, test_AsyncUpdateWithApply)(benchmark::State &state) {
  for (auto _ : state) {
    shad::rt::Handle handle;
    for (size_t i = 0; i < MAP_SIZE; i++) {
      mapPtr_->AsyncApply(handle, i, asyncApplyFun);
    }
    shad::rt::waitForCompletion(handle);
  }
}

BENCHMARK_F(TestFixture, test_AsyncUpdateWithFE)(benchmark::State &state) {
  for (auto _ : state) {
    shad::rt::Handle handle;
    mapPtr_->AsyncForEachEntry(handle, asyncApplyFun);
    shad::rt::waitForCompletion(handle);
  }
}

BENCHMARK_F(TestFixture, test_AsyncFEKey)(benchmark::State &state) {
  for (auto _ : state) {
    shad::rt::Handle handle;
    mapPtr_->AsyncForEachKey(handle, asyncFEfun);
    shad::rt::waitForCompletion(handle);
  }
}

/**
 * Custom main() instead of calling BENCHMARK_MAIN()
 */
namespace shad {
int main(int argc, char **argv) {
  // Parse command line args
  for (size_t argIndex = 1; argIndex < argc - 1; argIndex++) {
    std::string arg(argv[argIndex]);
    if (arg == "--Size") {
      ++argIndex;
      MAP_SIZE = strtoul(argv[argIndex], nullptr, 0);
    } else if (arg == "--NumIter") {
      ++argIndex;
      NUM_ITER = strtoul(argv[argIndex], nullptr, 0);
    } else if (arg == "--OutFileName") {
      ++argIndex;
      FILE_NAME = std::string(argv[argIndex]);
    }
  }
  std::cout << "\n MAP_SIZE: " << MAP_SIZE << std::endl;
  std::cout << "\n NUM_ITER: " << NUM_ITER << std::endl;
  std::cout << std::endl;

  ::benchmark::Initialize(&argc, argv);
  ::benchmark::RunSpecifiedBenchmarks();

  return 0;
}
}  // namespace shad
