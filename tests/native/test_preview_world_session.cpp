#define main sidecar_entry_main_for_test
#include "../../src/entry_sidecar.cpp"
#undef main

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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
        GeyserSeedContext primaryContext;
        GeyserSeedContext secondaryContext;
        std::string errorMessage;
        Expect(ResolveGeyserSeedContext(33,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Primary,
                                        &primaryContext,
                                        &errorMessage),
               "M-BAD-C primary geyser context should resolve",
               &failures);
        Expect(ResolveGeyserSeedContext(33,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Secondary,
                                        &secondaryContext,
                                        &errorMessage),
               "M-BAD-C secondary geyser context should resolve",
               &failures);
        Expect(primaryContext.worldOffsetX == 82,
               "M-BAD-C primary worldOffsetX should stay 82",
               &failures);
        Expect(secondaryContext.worldOffsetX == 212,
               "M-BAD-C secondary worldOffsetX should resolve to opposite column",
               &failures);
    }

    {
        GeyserSeedContext primaryContext;
        GeyserSeedContext secondaryContext;
        std::string errorMessage;
        Expect(ResolveGeyserSeedContext(34,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Primary,
                                        &primaryContext,
                                        &errorMessage),
               "M-FRZ-C primary geyser context should resolve",
               &failures);
        Expect(ResolveGeyserSeedContext(34,
                                        100123,
                                        0,
                                        Batch::PreviewTarget::Secondary,
                                        &secondaryContext,
                                        &errorMessage),
               "M-FRZ-C secondary geyser context should resolve",
               &failures);
        Expect(primaryContext.worldOffsetX == 212,
               "M-FRZ-C primary worldOffsetX should stay 212",
               &failures);
        Expect(secondaryContext.worldOffsetX == 82,
               "M-FRZ-C secondary worldOffsetX should resolve to opposite column",
               &failures);
    }

    {
        GeyserSeedContext primaryContext;
        GeyserSeedContext secondaryContext;
        std::string errorMessage;
        Expect(ResolveGeyserSeedContext(13,
                                        100123,
                                        625,
                                        Batch::PreviewTarget::Primary,
                                        &primaryContext,
                                        &errorMessage),
               "non-moonlet primary geyser context should resolve",
               &failures);
        Expect(primaryContext.worldOffsetX == 0,
               "non-moonlet primary worldOffsetX should stay 0",
               &failures);
        Expect(!ResolveGeyserSeedContext(13,
                                         100123,
                                         625,
                                         Batch::PreviewTarget::Secondary,
                                         &secondaryContext,
                                         &errorMessage),
               "non-moonlet secondary geyser context should fail",
               &failures);
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
        Expect(ResolvePreviewWorldContext(session,
                                          Batch::PreviewTarget::Secondary,
                                          &secondaryContext,
                                          &errorMessage),
               "M-FRZ-C secondary preview context should resolve",
               &failures);

        const auto primaryDetails = GeyserCalc::BuildGeyserDetails(primaryContext.geyserSeed,
                                                                   primaryContext.summary.worldSize.y,
                                                                   primaryContext.summary.geysers,
                                                                   primaryContext.worldOffsetX,
                                                                   primaryContext.worldOffsetY);
        const auto secondaryDetails = GeyserCalc::BuildGeyserDetails(secondaryContext.geyserSeed,
                                                                     secondaryContext.summary.worldSize.y,
                                                                     secondaryContext.summary.geysers,
                                                                     secondaryContext.worldOffsetX,
                                                                     secondaryContext.worldOffsetY);
        const auto secondaryDetailsWithoutOffset =
            GeyserCalc::BuildGeyserDetails(secondaryContext.geyserSeed,
                                          secondaryContext.summary.worldSize.y,
                                          secondaryContext.summary.geysers,
                                          0,
                                          0);

        Expect(!primaryDetails.empty(), "M-FRZ-C primary geyser details should not be empty", &failures);
        Expect(!secondaryDetails.empty(), "M-FRZ-C secondary geyser details should not be empty", &failures);
        Expect(secondaryDetails.size() == secondaryContext.summary.geysers.size(),
               "M-FRZ-C secondary detail count should match summary geysers",
               &failures);
        Expect(secondaryDetailsWithoutOffset.size() == secondaryDetails.size(),
               "M-FRZ-C secondary detail count should stay stable without offset",
               &failures);

        bool foundOffsetSensitiveDetail = false;
        for (size_t index = 0; index < secondaryDetails.size(); ++index) {
            if (!secondaryDetails[index].hasParameters ||
                !secondaryDetailsWithoutOffset[index].hasParameters) {
                continue;
            }
            if (secondaryDetails[index].native.eruptionPeriodSeconds !=
                    secondaryDetailsWithoutOffset[index].native.eruptionPeriodSeconds ||
                secondaryDetails[index].derived.temperatureCelsius !=
                    secondaryDetailsWithoutOffset[index].derived.temperatureCelsius) {
                foundOffsetSensitiveDetail = true;
                break;
            }
        }
        Expect(foundOffsetSensitiveDetail,
               "M-FRZ-C secondary detail should depend on resolved world offset",
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
