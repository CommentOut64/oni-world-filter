#pragma once

#include <string>
#include <vector>

struct ClusterLayout;
struct MixingConfig;
struct WorldMixingSettings;
struct SubworldMixingSettings;

struct ActiveContentSet {
    bool expansion1 = false;
    bool dlc2 = false;
    bool dlc3 = false;
    bool dlc4 = false;
    std::vector<std::string> ids;

    bool HasContent(const std::string &id) const;
};

ActiveContentSet BuildActiveContentSet(const ClusterLayout *cluster,
                                       const std::vector<MixingConfig> &mixConfigs);

bool HasAnyClusterTag(const ClusterLayout *cluster,
                      const std::vector<std::string> &forbiddenClusterTags);

std::vector<std::string> GetRequiredContentIds(const MixingConfig &config,
                                               const WorldMixingSettings *worldSetting,
                                               const SubworldMixingSettings *subworldSetting);

bool IsMixingConfigAllowed(const MixingConfig &config,
                           const ClusterLayout *cluster,
                           const ActiveContentSet &activeContent,
                           const WorldMixingSettings *worldSetting,
                           const SubworldMixingSettings *subworldSetting);
