#pragma once

#include "BatchCpu/CpuOptimization.hpp"

namespace Batch {

using CpuTopologyFacts = BatchCpu::CpuTopologyFacts;
using CpuTopology = BatchCpu::CpuTopology;

CpuTopologyFacts DetectCpuTopologyFacts();
CpuTopology DetectCpuTopology();

} // namespace Batch

