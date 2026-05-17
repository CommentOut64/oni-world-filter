#include "Setting/ContentActivation.hpp"

#include <ranges>

#include "Setting/ClusterLayout.hpp"
#include "Setting/SettingsCache.hpp"
#include "Setting/WorldGenClasses.hpp"

namespace {

void AddContentId(ActiveContentSet *content, const std::string &id)
{
    if (content == nullptr || id.empty()) {
        return;
    }
    if (std::ranges::find(content->ids, id) == content->ids.end()) {
        content->ids.push_back(id);
    }
    if (id == "EXPANSION1_ID") {
        content->expansion1 = true;
    } else if (id == "DLC2_ID") {
        content->dlc2 = true;
    } else if (id == "DLC3_ID") {
        content->dlc3 = true;
    } else if (id == "DLC4_ID") {
        content->dlc4 = true;
    }
}

std::vector<std::string> InferRequiredContentIdsFromPath(const std::string &path)
{
    if (path.starts_with("dlc2::")) {
        return {"DLC2_ID"};
    }
    if (path.starts_with("dlc3::")) {
        return {"DLC3_ID"};
    }
    if (path.starts_with("dlc4::")) {
        return {"DLC4_ID"};
    }
    return {};
}

} // namespace

bool ActiveContentSet::HasContent(const std::string &id) const
{
    if (id.empty()) {
        return true;
    }
    return std::ranges::find(ids, id) != ids.end();
}

ActiveContentSet BuildActiveContentSet(const ClusterLayout *cluster,
                                       const std::vector<MixingConfig> &mixConfigs)
{
    ActiveContentSet content;
    if (cluster != nullptr) {
        for (const auto &id : cluster->requiredDlcIds) {
            AddContentId(&content, id);
        }
    }

    for (const auto &config : mixConfigs) {
        if (config.type != 0 || config.level == MixingLevel::Disabled) {
            continue;
        }
        AddContentId(&content, config.path);
    }
    return content;
}

bool HasAnyClusterTag(const ClusterLayout *cluster,
                      const std::vector<std::string> &forbiddenClusterTags)
{
    if (cluster == nullptr || forbiddenClusterTags.empty()) {
        return false;
    }
    for (const auto &tag : forbiddenClusterTags) {
        if (std::ranges::find(cluster->clusterTags, tag) != cluster->clusterTags.end()) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> GetRequiredContentIds(const MixingConfig &config,
                                               const WorldMixingSettings *worldSetting,
                                               const SubworldMixingSettings *subworldSetting)
{
    if (config.type == 0) {
        return InferRequiredContentIdsFromPath(config.path);
    }
    if (config.type == 1) {
        if (worldSetting != nullptr && !worldSetting->required_content.empty()) {
            return worldSetting->required_content;
        }
        return InferRequiredContentIdsFromPath(config.path);
    }
    if (config.type == 2) {
        if (subworldSetting != nullptr && !subworldSetting->required_content.empty()) {
            return subworldSetting->required_content;
        }
        return InferRequiredContentIdsFromPath(config.path);
    }
    return {};
}

bool IsMixingConfigAllowed(const MixingConfig &config,
                           const ClusterLayout *cluster,
                           const ActiveContentSet &activeContent,
                           const WorldMixingSettings *worldSetting,
                           const SubworldMixingSettings *subworldSetting)
{
    for (const auto &id : GetRequiredContentIds(config, worldSetting, subworldSetting)) {
        if (!activeContent.HasContent(id)) {
            return false;
        }
    }

    if (config.type == 1) {
        return worldSetting != nullptr &&
               !HasAnyClusterTag(cluster, worldSetting->forbiddenClusterTags);
    }
    if (config.type == 2) {
        return subworldSetting != nullptr &&
               !HasAnyClusterTag(cluster, subworldSetting->forbiddenClusterTags);
    }
    return true;
}
