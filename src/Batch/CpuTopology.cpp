#include "Batch/CpuTopology.hpp"

namespace Batch {

CpuTopology DetectCpuTopology()
{
    return BatchCpu::CpuTopologyDetector::Detect();
}

} // namespace Batch

