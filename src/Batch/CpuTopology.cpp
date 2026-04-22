#include "Batch/CpuTopology.hpp"

namespace Batch {

CpuTopologyFacts DetectCpuTopologyFacts()
{
    return BatchCpu::CpuTopologyDetector::DetectFacts();
}

} // namespace Batch

