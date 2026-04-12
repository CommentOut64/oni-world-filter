#pragma once

#include <map>
#include <string>
#include <vector>

class SettingsCache;

namespace SearchAnalysis {

struct SourceSummary {
    std::string ruleId;
    std::string templateName;
    std::string geyserId;
    int upperBound = 0;
    std::string sourceKind;
    std::string poolId;
};

struct SpatialEnvelope {
    std::string envelopeId;
    std::string confidence;
    std::string method;
};

struct SourcePool {
    std::string poolId;
    std::string sourceKind;
    int capacityUpper = 0;
};

struct WorldEnvelopeProfile {
    bool valid = false;
    int worldType = 0;
    std::string worldCode;
    int width = 0;
    int height = 0;
    double diagonal = 0.0;
    std::vector<int> activeMixingSlots;
    std::vector<int> disabledMixingSlots;
    std::vector<std::string> possibleGeyserTypes;
    std::vector<std::string> impossibleGeyserTypes;
    std::map<std::string, int> possibleMaxCountByType;
    std::map<std::string, double> genericTypeUpperById;
    int genericSlotUpper = 0;
    std::vector<SourceSummary> exactSourceSummary;
    std::vector<SourceSummary> genericSourceSummary;
    std::vector<SourcePool> sourcePools;
    std::vector<SpatialEnvelope> spatialEnvelopes;
};

WorldEnvelopeProfile CompileWorldEnvelopeProfile(const SettingsCache &baseSettings,
                                                 int worldType,
                                                 int mixing,
                                                 std::string *errorMessage = nullptr);

} // namespace SearchAnalysis
