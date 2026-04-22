#pragma once

#include "BatchCpu/CpuOptimization.hpp"

namespace Batch {

using CpuTopologyFacts = BatchCpu::CpuTopologyFacts;

CpuTopologyFacts DetectCpuTopologyFacts();

} // namespace Batch

