#pragma once

#include <limits>
#include <string>
#include <vector>

#include "SearchAnalysis/WorldEnvelopeProfile.hpp"

namespace SearchAnalysis {

struct DistanceConstraint {
    std::string geyserId;
    double minDist = 0.0;
    double maxDist = 0.0;
};

struct CountConstraint {
    std::string geyserId;
    int minCount = 0;
    int maxCount = 0;
};

struct SearchCpuConfig {
    bool hasValue = false;
    std::string mode = "balanced";
    int workers = 0;
    bool allowSmt = true;
    bool allowLowPerf = false;
    std::string placement = "preferred";
    bool enableWarmup = true;
    bool enableAdaptiveDown = true;
    int chunkSize = 64;
    int progressInterval = 1000;
    int sampleWindowMs = 2000;
    int adaptiveMinWorkers = 1;
    double adaptiveDropThreshold = 0.12;
    int adaptiveDropWindows = 3;
    int adaptiveCooldownMs = 8000;
};

struct SearchConstraints {
    std::vector<std::string> required;
    std::vector<std::string> forbidden;
    std::vector<DistanceConstraint> distance;
    std::vector<CountConstraint> count;
};

struct SearchAnalysisRequest {
    std::string jobId;
    int worldType = 0;
    int seedStart = 0;
    int seedEnd = 0;
    int mixing = 0;
    int threads = 0;
    SearchCpuConfig cpu;
    SearchConstraints constraints;
};

struct ConstraintGroup {
    std::string geyserId;
    int geyserIndex = -1;
    int minCount = 0;
    int maxCount = std::numeric_limits<int>::max();
    bool hasRequired = false;
    bool hasForbidden = false;
    bool hasExplicitCount = false;
    std::vector<DistanceConstraint> distanceRules;
};

struct NormalizedSearchRequest {
    int worldType = 0;
    int seedStart = 0;
    int seedEnd = 0;
    int mixing = 0;
    int threads = 0;
    SearchCpuConfig cpu;
    std::vector<ConstraintGroup> groups;
};

struct ValidationIssue {
    std::string layer;
    std::string code;
    std::string field;
    std::string message;
};

struct SearchAnalysisResult {
    WorldEnvelopeProfile worldProfile;
    NormalizedSearchRequest normalizedRequest;
    std::vector<ValidationIssue> errors;
    std::vector<ValidationIssue> warnings;
    std::vector<std::string> bottlenecks;
    double predictedBottleneckProbability = 1.0;
};

} // namespace SearchAnalysis
