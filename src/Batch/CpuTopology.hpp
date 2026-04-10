#pragma once

#include "BatchCpu/CpuOptimization.hpp"

namespace Batch {

using CpuTopology = BatchCpu::CpuTopology;

CpuTopology DetectCpuTopology();

} // namespace Batch

