#include "Batch/CpuTopology.hpp"

namespace Batch {

CpuTopologyFacts DetectCpuTopologyFacts()
{
    return BatchCpu::CpuTopologyDetector::DetectFacts();
}

CpuTopology DetectCpuTopology()
{
    return BatchCpu::CpuTopologyDetector::Detect();
}

} // namespace Batch

