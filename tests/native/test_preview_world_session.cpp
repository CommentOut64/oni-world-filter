#define main sidecar_entry_main_for_test
#include "../../src/entry_sidecar.cpp"
#undef main

#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <json/json.h>

namespace {

bool Expect(bool condition, const std::string &message, std::vector<std::string> *failures)
{
    if (condition || failures == nullptr) {
        return condition;
    }
    failures->push_back(message);
    return false;
}

class CollectingPreviewSink final : public ResultSink
{
public:
    bool RequestResource(uint32_t expectedSize, std::vector<char> &data) override
    {
        data.assign(expectedSize, 0);
        std::ifstream file(ResolveSettingsAssetPath(), std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        const auto size = file.seekg(0, std::ios::end).tellg();
        if (size != static_cast<std::streamoff>(expectedSize)) {
            return false;
        }
        file.seekg(0).read(data.data(), expectedSize);
        return file.good();
    }

    void OnGeneratedWorldSummary(const GeneratedWorldSummary &summary) override
    {
        summaries.push_back(summary);
    }

    void OnGeneratedWorldPreview(const GeneratedWorldPreview &preview) override
    {
        previews.push_back(preview);
    }

    std::vector<GeneratedWorldSummary> summaries;
    std::vector<GeneratedWorldPreview> previews;
};

bool GeneratePreviewSet(int worldType,
                        int seed,
                        int mixing,
                        CollectingPreviewSink *sink,
                        std::string *code)
{
    if (sink == nullptr) {
        return false;
    }

    if (!BuildWorldCode(worldType, seed, mixing, code)) {
        return false;
    }

    auto *runtime = AppRuntime::Instance();
    runtime->SetResultSink(sink);
    runtime->SetSkipPolygons(false);
    runtime->Initialize(0);
    return runtime->Generate(*code, 0);
}

bool GeneratePreviewSessionForTest(int worldType,
                                   int seed,
                                   int mixing,
                                   PreviewWorldSession *session,
                                   std::string *errorMessage)
{
    Batch::SidecarPreviewRequest request;
    request.worldType = worldType;
    request.seed = seed;
    request.mixing = mixing;
    request.target = Batch::PreviewTarget::Primary;
    return GeneratePreviewSession(request, session, errorMessage);
}

bool HasPreviewWithPrimaryFlag(const std::vector<GeneratedWorldPreview> &previews, bool isPrimary)
{
    for (const auto &preview : previews) {
        if (preview.summary.isPrimary == isPrimary) {
            return true;
        }
    }
    return false;
}

struct PreviewSample {
    int worldType{};
    int seed{};
    int mixing{};
    const char *expectedPrefix{};
};

bool LoadSettingsForWorldType(int worldType,
                              int seed,
                              int mixing,
                              SettingsCache *settings,
                              std::string *code,
                              std::string *errorMessage)
{
    if (settings == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "settings output is null";
        }
        return false;
    }

    if (!BuildWorldCode(worldType, seed, mixing, code)) {
        if (errorMessage != nullptr) {
            *errorMessage = "invalid worldType";
        }
        return false;
    }

    std::string sharedError;
    const auto shared = SharedSettingsCache::GetOrCreate(ReadSettingsBlob, &sharedError);
    if (shared == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = sharedError.empty() ? "failed to load shared settings cache"
                                                : sharedError;
        }
        return false;
    }

    *settings = *shared;
    if (!settings->CoordinateChanged(*code, *settings)) {
        if (errorMessage != nullptr) {
            *errorMessage = "parse world code failed";
        }
        return false;
    }
    return true;
}

int CountWarpWorldPlacements(const SettingsCache &settings)
{
    if (settings.cluster == nullptr) {
        return 0;
    }

    int warpPlacementCount = 0;
    for (const auto &placement : settings.cluster->worldPlacements) {
        const auto itr = settings.worlds.find(placement.world);
        if (itr == settings.worlds.end()) {
            continue;
        }
        if (itr->second.startingBaseTemplate.contains("::bases/warpworld")) {
            ++warpPlacementCount;
        }
    }
    return warpPlacementCount;
}

std::string CaptureStdout(const std::function<void()> &action)
{
    std::ostringstream stream;
    auto *previous = std::cout.rdbuf(stream.rdbuf());
    action();
    std::cout.rdbuf(previous);
    return stream.str();
}

bool ParseSingleJsonLine(const std::string &text,
                         Json::Value *root,
                         std::vector<std::string> *failures,
                         const std::string &message)
{
    if (root == nullptr) {
        return false;
    }
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, root, &errors)) {
        if (failures != nullptr) {
            failures->push_back(message + ": " + errors);
        }
        return false;
    }
    return true;
}

} // namespace

int RunAllTests()
{
    std::vector<std::string> failures;

    const std::vector<PreviewSample> moonletSamples = {
        {32, 100123, 0, "M-SWMP-C-"},
        {33, 100123, 0, "M-BAD-C-"},
        {34, 100123, 0, "M-FRZ-C-"},
        {36, 100123, 0, "M-RAD-C-"},
        {37, 100123, 0, "M-CERS-C-"},
    };
    const std::vector<PreviewSample> classicSpacedOutSamples = {
        {27, 100123, 0, "SNDST-C-"},
        {28, 100123, 0, "PRE-C-"},
        {29, 100123, 0, "CER-C-"},
        {30, 100123, 0, "FRST-C-"},
        {31, 100123, 0, "SWMP-C-"},
    };
    const std::vector<PreviewSample> vanillaStyleSpacedOutSamples = {
        {13, 100123, 0, "V-SNDST-C-"},
        {15, 100123, 0, "V-SWMP-C-"},
        {16, 100123, 0, "V-SFRZ-C-"},
        {23, 100123, 0, "V-CER-C-"},
    };

    for (const auto &sample : moonletSamples) {
        CollectingPreviewSink sink;
        std::string code;
        const bool generated =
            GeneratePreviewSet(sample.worldType, sample.seed, sample.mixing, &sink, &code);
        Expect(generated, std::string("moonlet preview should generate for ") + sample.expectedPrefix, &failures);
        if (!generated) {
            continue;
        }

        Expect(code.starts_with(sample.expectedPrefix),
               std::string("generated code prefix mismatch for ") + sample.expectedPrefix,
               &failures);
        Expect(!sink.previews.empty(),
               std::string("preview list should not be empty for ") + sample.expectedPrefix,
               &failures);
        Expect(HasPreviewWithPrimaryFlag(sink.previews, true),
               std::string("primary preview should exist for ") + sample.expectedPrefix,
               &failures);
        Expect(HasPreviewWithPrimaryFlag(sink.previews, false),
               std::string("secondary preview should exist for ") + sample.expectedPrefix,
               &failures);

        for (const auto &preview : sink.previews) {
            Expect(preview.summary.worldPlacementIndex >= 0,
                   std::string("worldPlacementIndex should be non-negative for ") + sample.expectedPrefix,
                   &failures);
            Expect(preview.summary.seed == sample.seed,
                   std::string("summary seed should stay on base seed for ") + sample.expectedPrefix,
                   &failures);
        }
    }

    {
        for (const auto &sample : classicSpacedOutSamples) {
            SettingsCache settings;
            std::string code;
            std::string errorMessage;
            Expect(LoadSettingsForWorldType(sample.worldType,
                                            sample.seed,
                                            sample.mixing,
                                            &settings,
                                            &code,
                                            &errorMessage),
                   std::string("classic SO settings should load for ") + sample.expectedPrefix,
                   &failures);
            if (!errorMessage.empty()) {
                failures.push_back(std::string("classic SO settings load error for ") +
                                   sample.expectedPrefix + ": " + errorMessage);
            }
            if (!settings.cluster) {
                continue;
            }

            Expect(CountWarpWorldPlacements(settings) == 1,
                   std::string("classic SO cluster should contain exactly one warp placement for ") +
                       sample.expectedPrefix,
                   &failures);

            PreviewWorldSession session;
            errorMessage.clear();
            Expect(GeneratePreviewSessionForTest(sample.worldType,
                                                sample.seed,
                                                sample.mixing,
                                                &session,
                                                &errorMessage),
                   std::string("classic SO preview session should generate for ") + sample.expectedPrefix,
                   &failures);
            if (!errorMessage.empty()) {
                failures.push_back(std::string("classic SO preview session error for ") +
                                   sample.expectedPrefix + ": " + errorMessage);
            }
            Expect(session.secondaryPreview.has_value(),
                   std::string("classic SO preview session should expose secondary preview for ") +
                       sample.expectedPrefix,
                   &failures);
            if (session.primaryPreview.has_value()) {
                Expect(session.primaryPreview->summary.hasSecondaryPreview,
                       std::string("classic SO primary preview should report secondary availability for ") +
                           sample.expectedPrefix,
                       &failures);
            }
        }
    }

    {
        PreviewWorldSession session;
        std::string errorMessage;
        Expect(GeneratePreviewSessionForTest(33, 100123, 0, &session, &errorMessage),
               "M-BAD-C preview session should generate",
               &failures);
        if (session.secondaryPreview.has_value()) {
            Expect(session.secondaryPreview->summary.worldAssetId ==
                       "expansion1::worlds/MiniRadioactiveOceanWarp",
                   "M-BAD-C secondary preview should keep its real world asset id",
                   &failures);
        }
    }

    {
        PreviewWorldSession session;
        std::string errorMessage;
        Expect(GeneratePreviewSessionForTest(36, 100123, 0, &session, &errorMessage),
               "M-RAD-C preview session should generate",
               &failures);
        if (session.secondaryPreview.has_value()) {
            Expect(session.secondaryPreview->summary.worldAssetId ==
                       "expansion1::worlds/MiniFlippedWarp",
                   "M-RAD-C secondary preview should not be locked to another world asset",
                   &failures);
        }
    }

    {
        CollectingPreviewSink sink;
        std::string code;
        const bool generated = GeneratePreviewSet(13, 100123, 625, &sink, &code);
        Expect(generated, "non-moonlet preview should generate", &failures);
        if (generated) {
            Expect(code.starts_with("V-SNDST-C-"),
                   "non-moonlet code prefix mismatch",
                   &failures);
            Expect(HasPreviewWithPrimaryFlag(sink.previews, true),
                   "non-moonlet preview should contain primary world",
                   &failures);
            Expect(!HasPreviewWithPrimaryFlag(sink.previews, false),
                   "non-moonlet preview should not contain secondary world",
                   &failures);
        }
    }

    {
        SettingsCache settings;
        std::string code;
        std::string errorMessage;
        Expect(LoadSettingsForWorldType(27, 100123, 0, &settings, &code, &errorMessage),
               "SNDST-C settings should load for geyser offset checks",
               &failures);
        PreviewPlacementSelection selection;
        Expect(ResolvePreviewPlacementSelection(settings, &selection, &errorMessage),
               "SNDST-C placement selection should resolve for geyser offset checks",
               &failures);
        Expect(settings.cluster != nullptr &&
                   settings.cluster->clusterCategory == ClusterCategory::SpacedOutStyle,
               "SNDST-C should stay SpacedOutStyle after settings load",
               &failures);
        Expect(selection.primaryPlacementIndex == 0,
               "SNDST-C primary placement should stay at index 0",
               &failures);
        Expect(selection.secondaryPlacementIndex.has_value(),
               "SNDST-C should expose a secondary placement candidate",
               &failures);
    }

    {
        GeyserSeedContext primaryContext;
        std::string errorMessage;
        Expect(ResolveGeyserSeedContext(0,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Primary,
                                        &primaryContext,
                                        &errorMessage),
               "SNDST-A primary geyser context should resolve",
               &failures);
        Expect(primaryContext.worldOffsetX == 0,
               "SNDST-A primary worldOffsetX should stay 0",
               &failures);
        Expect(primaryContext.worldOffsetY == 0,
               "SNDST-A primary worldOffsetY should stay 0",
               &failures);
    }

    {
        GeyserSeedContext primaryContext;
        GeyserSeedContext secondaryContext;
        std::string errorMessage;
        PreviewWorldSession session;
        Expect(GeneratePreviewSessionForTest(33, 100123, 0, &session, &errorMessage),
               "M-BAD-C preview session should generate for authoritative offset checks",
               &failures);
        Expect(ResolveGeyserSeedContext(33,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Primary,
                                        &primaryContext,
                                        &errorMessage),
               "M-BAD-C primary geyser context should resolve",
               &failures);
        errorMessage.clear();
        Expect(ResolveGeyserSeedContext(33,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Secondary,
                                        &secondaryContext,
                                        &errorMessage),
               "M-BAD-C secondary geyser context should resolve from preview summary",
               &failures);
        Expect(session.primaryPreview.has_value(),
               "M-BAD-C primary preview should exist for authoritative offset checks",
               &failures);
        Expect(session.secondaryPreview.has_value(),
               "M-BAD-C secondary preview should exist for authoritative offset checks",
               &failures);
        if (session.primaryPreview.has_value()) {
            Expect(primaryContext.worldOffsetX == session.primaryPreview->summary.worldOffsetX,
                   "M-BAD-C primary geyser context should match preview summary worldOffsetX",
                   &failures);
            Expect(primaryContext.worldOffsetY == session.primaryPreview->summary.worldOffsetY,
                   "M-BAD-C primary geyser context should match preview summary worldOffsetY",
                   &failures);
        }
        if (session.secondaryPreview.has_value()) {
            Expect(secondaryContext.worldOffsetX == session.secondaryPreview->summary.worldOffsetX,
                   "M-BAD-C secondary geyser context should match preview summary worldOffsetX",
                   &failures);
            Expect(secondaryContext.worldOffsetY == session.secondaryPreview->summary.worldOffsetY,
                   "M-BAD-C secondary geyser context should match preview summary worldOffsetY",
                   &failures);
        }
        Expect(secondaryContext.worldOffsetX != 82,
               "M-BAD-C secondary worldOffsetX should not collapse onto primary legacy constant",
               &failures);
        Expect(primaryContext.worldOffsetX == 82,
               "M-BAD-C primary worldOffsetX should stay 82",
               &failures);
        Expect(primaryContext.worldOffsetY == 0,
               "M-BAD-C primary worldOffsetY should stay 0",
               &failures);
    }

    {
        GeyserSeedContext primaryContext;
        GeyserSeedContext secondaryContext;
        std::string errorMessage;
        PreviewWorldSession session;
        Expect(GeneratePreviewSessionForTest(34, 100123, 0, &session, &errorMessage),
               "M-FRZ-C preview session should generate for authoritative offset checks",
               &failures);
        Expect(ResolveGeyserSeedContext(34,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Primary,
                                        &primaryContext,
                                        &errorMessage),
               "M-FRZ-C primary geyser context should resolve",
               &failures);
        errorMessage.clear();
        Expect(ResolveGeyserSeedContext(34,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Secondary,
                                        &secondaryContext,
                                        &errorMessage),
               "M-FRZ-C secondary geyser context should resolve from preview summary",
               &failures);
        Expect(session.primaryPreview.has_value(),
               "M-FRZ-C primary preview should exist for authoritative offset checks",
               &failures);
        Expect(session.secondaryPreview.has_value(),
               "M-FRZ-C secondary preview should exist for authoritative offset checks",
               &failures);
        if (session.primaryPreview.has_value()) {
            Expect(primaryContext.worldOffsetX == session.primaryPreview->summary.worldOffsetX,
                   "M-FRZ-C primary geyser context should match preview summary worldOffsetX",
                   &failures);
            Expect(primaryContext.worldOffsetY == session.primaryPreview->summary.worldOffsetY,
                   "M-FRZ-C primary geyser context should match preview summary worldOffsetY",
                   &failures);
        }
        if (session.secondaryPreview.has_value()) {
            Expect(secondaryContext.worldOffsetX == session.secondaryPreview->summary.worldOffsetX,
                   "M-FRZ-C secondary geyser context should match preview summary worldOffsetX",
                   &failures);
            Expect(secondaryContext.worldOffsetY == session.secondaryPreview->summary.worldOffsetY,
                   "M-FRZ-C secondary geyser context should match preview summary worldOffsetY",
                   &failures);
        }
        Expect(secondaryContext.worldOffsetX != 212,
               "M-FRZ-C secondary worldOffsetX should not collapse onto primary legacy constant",
               &failures);
        Expect(primaryContext.worldOffsetX == 212,
               "M-FRZ-C primary worldOffsetX should stay 212",
               &failures);
        Expect(primaryContext.worldOffsetY == 0,
               "M-FRZ-C primary worldOffsetY should stay 0",
               &failures);
    }

    {
        for (const auto &sample : classicSpacedOutSamples) {
            GeyserSeedContext primaryContext;
            GeyserSeedContext secondaryContext;
            std::string errorMessage;
            PreviewWorldSession session;
            Expect(GeneratePreviewSessionForTest(sample.worldType,
                                                sample.seed,
                                                sample.mixing,
                                                &session,
                                                &errorMessage),
                   std::string(sample.expectedPrefix) +
                       " preview session should generate for authoritative offset checks",
                   &failures);
            Expect(ResolveGeyserSeedContext(sample.worldType,
                                            sample.seed,
                                            sample.mixing,
                                            Batch::PreviewTarget::Primary,
                                            &primaryContext,
                                            &errorMessage),
                   std::string(sample.expectedPrefix) +
                       " primary geyser context should resolve from generated summary offset",
                   &failures);
            Expect(primaryContext.worldOffsetX == 0,
                   std::string(sample.expectedPrefix) +
                       " primary worldOffsetX should stay 0 from generated summary offset",
                   &failures);
            Expect(primaryContext.worldOffsetY == 0,
                   std::string(sample.expectedPrefix) +
                       " primary worldOffsetY should stay 0 from generated summary offset",
                   &failures);
            errorMessage.clear();
            Expect(ResolveGeyserSeedContext(sample.worldType,
                                            sample.seed,
                                            sample.mixing,
                                            Batch::PreviewTarget::Secondary,
                                            &secondaryContext,
                                            &errorMessage),
                   std::string(sample.expectedPrefix) +
                       " secondary geyser context should resolve from preview summary",
                   &failures);
            Expect(session.secondaryPreview.has_value(),
                   std::string(sample.expectedPrefix) +
                       " secondary preview should exist for authoritative offset checks",
                   &failures);
            if (session.secondaryPreview.has_value()) {
                Expect(secondaryContext.worldOffsetX == session.secondaryPreview->summary.worldOffsetX,
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should match preview summary worldOffsetX",
                       &failures);
                Expect(secondaryContext.worldOffsetY == session.secondaryPreview->summary.worldOffsetY,
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should match preview summary worldOffsetY",
                       &failures);
            }
        }
    }

    {
        for (const auto &sample : vanillaStyleSpacedOutSamples) {
            GeyserSeedContext primaryContext;
            GeyserSeedContext secondaryContext;
            std::string errorMessage;
            PreviewWorldSession session;
            Expect(GeneratePreviewSessionForTest(sample.worldType,
                                                sample.seed,
                                                sample.mixing,
                                                &session,
                                                &errorMessage),
                   std::string(sample.expectedPrefix) +
                       " preview session should generate for authoritative offset checks",
                   &failures);
            Expect(ResolveGeyserSeedContext(sample.worldType,
                                            sample.seed,
                                            sample.mixing,
                                            Batch::PreviewTarget::Primary,
                                            &primaryContext,
                                            &errorMessage),
                   std::string(sample.expectedPrefix) +
                       " primary geyser context should resolve from generated summary offset",
                   &failures);
            Expect(primaryContext.worldOffsetX == 0,
                   std::string(sample.expectedPrefix) +
                       " primary worldOffsetX should stay 0 from generated summary offset",
                   &failures);
            Expect(primaryContext.worldOffsetY == 0,
                   std::string(sample.expectedPrefix) +
                       " primary worldOffsetY should stay 0 from generated summary offset",
                   &failures);
            errorMessage.clear();
            if (session.secondaryPreview.has_value()) {
                Expect(ResolveGeyserSeedContext(sample.worldType,
                                                sample.seed,
                                                sample.mixing,
                                                Batch::PreviewTarget::Secondary,
                                                &secondaryContext,
                                                &errorMessage),
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should resolve when preview session contains secondary",
                       &failures);
                Expect(secondaryContext.worldOffsetX == session.secondaryPreview->summary.worldOffsetX,
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should match preview summary worldOffsetX",
                       &failures);
                Expect(secondaryContext.worldOffsetY == session.secondaryPreview->summary.worldOffsetY,
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should match preview summary worldOffsetY",
                       &failures);
            } else {
                Expect(!ResolveGeyserSeedContext(sample.worldType,
                                                 sample.seed,
                                                 sample.mixing,
                                                 Batch::PreviewTarget::Secondary,
                                                 &secondaryContext,
                                                 &errorMessage),
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should fail when preview session has no secondary",
                       &failures);
                Expect(errorMessage.find("secondary preview is not available") != std::string::npos,
                       std::string(sample.expectedPrefix) +
                           " secondary geyser context should report missing secondary preview",
                       &failures);
            }
        }
    }

    {
        PreviewWorldSession session;
        std::string errorMessage;
        Expect(GeneratePreviewSessionForTest(34, 100123, 0, &session, &errorMessage),
               "M-FRZ-C preview session should generate",
               &failures);

        PreviewWorldContext primaryContext;
        PreviewWorldContext secondaryContext;
        Expect(ResolvePreviewWorldContext(session,
                                          Batch::PreviewTarget::Primary,
                                          &primaryContext,
                                          &errorMessage),
               "M-FRZ-C primary preview context should resolve",
               &failures);
        errorMessage.clear();
        Expect(ResolvePreviewWorldContext(session,
                                          Batch::PreviewTarget::Secondary,
                                          &secondaryContext,
                                          &errorMessage),
               "M-FRZ-C secondary preview context should resolve from preview summary",
               &failures);
        Expect(primaryContext.worldOffsetX == 212,
               "M-FRZ-C primary preview context should expose generated summary worldOffsetX",
               &failures);
        Expect(primaryContext.worldOffsetY == 0,
               "M-FRZ-C primary preview context should expose generated summary worldOffsetY",
               &failures);
        Expect(session.secondaryPreview.has_value(),
               "M-FRZ-C preview session should expose secondary preview for context checks",
               &failures);
        if (session.secondaryPreview.has_value()) {
            Expect(secondaryContext.worldOffsetX == session.secondaryPreview->summary.worldOffsetX,
                   "M-FRZ-C secondary preview context should match preview summary worldOffsetX",
                   &failures);
            Expect(secondaryContext.worldOffsetY == session.secondaryPreview->summary.worldOffsetY,
                   "M-FRZ-C secondary preview context should match preview summary worldOffsetY",
                   &failures);
        }

        const auto primaryDetails = GeyserCalc::BuildGeyserDetails(primaryContext.geyserSeed,
                                                                   primaryContext.summary.worldSize.y,
                                                                   primaryContext.summary.geysers,
                                                                   primaryContext.worldOffsetX,
                                                                   primaryContext.worldOffsetY);
        Expect(!primaryDetails.empty(),
               "M-FRZ-C primary geyser details should not be empty after restoring primary offset",
               &failures);
        Expect(primaryDetails.size() == primaryContext.summary.geysers.size(),
               "M-FRZ-C primary detail count should match summary geysers after restoring primary offset",
               &failures);
        const auto secondaryDetails = GeyserCalc::BuildGeyserDetails(secondaryContext.geyserSeed,
                                                                     secondaryContext.summary.worldSize.y,
                                                                     secondaryContext.summary.geysers,
                                                                     secondaryContext.worldOffsetX,
                                                                     secondaryContext.worldOffsetY);
        Expect(secondaryDetails.size() == secondaryContext.summary.geysers.size(),
               "M-FRZ-C secondary detail count should match summary geysers with authoritative offset",
               &failures);
    }

    {
        Batch::SidecarPreviewGeyserDetailsRequest request;
        request.jobId = "preview-geyser-details-rebuild-primary";
        request.worldType = 0;
        request.seed = 100123;
        request.mixing = 0;
        request.target = Batch::PreviewTarget::Primary;

        {
            std::lock_guard<std::mutex> lock(g_previewSessionMutex);
            g_previewSession.reset();
        }

        Json::Value root;
        const auto output = CaptureStdout([&]() { RunPreviewGeyserDetailsCommand(request); });
        Expect(ParseSingleJsonLine(output, &root, &failures,
                                   "preview_geyser_details primary output should be valid json"),
               "preview_geyser_details primary output should parse",
               &failures);
        Expect(root["event"].asString() == "preview_geyser_details",
               "preview_geyser_details primary should rebuild preview session instead of failing on missing cache",
               &failures);
    }

    {
        Batch::SidecarPreviewGeyserDetailsRequest request;
        request.jobId = "preview-geyser-details-rebuild-secondary";
        request.worldType = 27;
        request.seed = 100123;
        request.mixing = 0;
        request.target = Batch::PreviewTarget::Secondary;

        {
            std::lock_guard<std::mutex> lock(g_previewSessionMutex);
            g_previewSession.reset();
        }

        Json::Value root;
        const auto output = CaptureStdout([&]() { RunPreviewGeyserDetailsCommand(request); });
        Expect(ParseSingleJsonLine(output, &root, &failures,
                                   "preview_geyser_details secondary output should be valid json"),
               "preview_geyser_details secondary output should parse",
               &failures);
        Expect(root["event"].asString() == "preview_geyser_details",
               "preview_geyser_details secondary should rebuild preview session with authoritative offset",
               &failures);
        Expect(root["geyserDetails"].isArray(),
               "preview_geyser_details secondary should return geyser detail array after rebuilding session",
               &failures);
    }

    if (!failures.empty()) {
        for (const auto &failure : failures) {
            std::cerr << "[FAIL] " << failure << std::endl;
        }
        return 1;
    }

    std::cout << "[PASS] test_preview_world_session" << std::endl;
    return 0;
}
