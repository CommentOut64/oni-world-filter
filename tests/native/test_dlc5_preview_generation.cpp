#define main sidecar_entry_main_for_test
#include "../../src/entry_sidecar.cpp"
#undef main

#include <fstream>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>

namespace {

void Trace(const std::string &message)
{
    const auto logDir = std::filesystem::path("out") / "logs";
    std::error_code error;
    std::filesystem::create_directories(logDir, error);
    std::ofstream stream(logDir / "test_dlc5_preview_generation.trace.log",
                         std::ios::app | std::ios::out);
    stream << message << '\n';
    stream.flush();
}

bool Expect(bool condition, const std::string &message, int &failures)
{
    if (condition) {
        return true;
    }
    std::cerr << "[FAIL] " << message << std::endl;
    ++failures;
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

struct Dlc5PreviewSample {
    int worldType{};
    int seed{};
    int mixing{};
    const char *expectedPrefix{};
    const char *expectedPrimaryWorldAssetId{};
    int expectedPrimaryPlacementIndex{};
    std::optional<const char *> expectedSecondaryWorldAssetId;
    std::optional<int> expectedSecondaryPlacementIndex;
};

bool PreviewContainsZoneType(const GeneratedWorldPreview &preview, int zoneType)
{
    for (const auto &polygon : preview.polygons) {
        if (polygon.zoneType == zoneType) {
            return true;
        }
    }
    return false;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    const std::vector<Dlc5PreviewSample> aquaticSamples = {
        {38, 100123, 0, "AQU-A-", "dlc5::worlds/AquaticBaseGameAsteroid", 0, std::nullopt, std::nullopt},
        {39, 100123, 0, "V-AQU-C-", "dlc5::worlds/AquaticClassicAsteroid", 0,
         "expansion1::worlds/MediumSwampy", 1},
        {40, 100123, 0, "AQU-C-", "dlc5::worlds/AquaticSpacedOutAsteroid", 0,
         "expansion1::worlds/WarpOilySandySwamp", 2},
    };

    for (const auto &sample : aquaticSamples) {
        Trace(std::string("sample begin: ") + sample.expectedPrefix);
        CollectingPreviewSink sink;
        std::string code;
        bool generated = false;
        try {
            Trace(std::string("preview set begin: ") + sample.expectedPrefix);
            generated =
                GeneratePreviewSet(sample.worldType, sample.seed, sample.mixing, &sink, &code);
            Trace(std::string("preview set end: ") + sample.expectedPrefix);
        } catch (const std::bad_alloc &) {
            std::cerr << "[FAIL] bad_alloc during GeneratePreviewSet for "
                      << sample.expectedPrefix << std::endl;
            Trace(std::string("bad_alloc during GeneratePreviewSet: ") + sample.expectedPrefix);
            ++failures;
            continue;
        } catch (const std::exception &ex) {
            std::cerr << "[FAIL] exception during GeneratePreviewSet for "
                      << sample.expectedPrefix << ": " << ex.what() << std::endl;
            Trace(std::string("exception during GeneratePreviewSet: ") + sample.expectedPrefix +
                  " message=" + ex.what());
            ++failures;
            continue;
        }
        Expect(generated,
               std::string("DLC5 preview should generate for ") + sample.expectedPrefix,
               failures);
        if (!generated) {
            continue;
        }

        Expect(code.starts_with(sample.expectedPrefix),
               std::string("DLC5 generated code prefix mismatch for ") + sample.expectedPrefix,
               failures);
        Expect(!sink.previews.empty(),
               std::string("DLC5 preview list should not be empty for ") + sample.expectedPrefix,
               failures);

        const auto *primaryPreview = FindPrimaryGeneratedWorldPreview(sink.previews);
        Expect(primaryPreview != nullptr,
               std::string("DLC5 primary preview should exist for ") + sample.expectedPrefix,
               failures);
        if (primaryPreview == nullptr) {
            continue;
        }

        Expect(primaryPreview->summary.seed == sample.seed,
               std::string("DLC5 preview seed should stay on base seed for ") +
                   sample.expectedPrefix,
               failures);
        Expect(primaryPreview->summary.worldPlacementIndex == sample.expectedPrimaryPlacementIndex,
               std::string("DLC5 primary preview placement index mismatch for ") +
                   sample.expectedPrefix,
               failures);
        Expect(primaryPreview->summary.worldAssetId == sample.expectedPrimaryWorldAssetId,
               std::string("DLC5 primary preview world asset id mismatch for ") +
                   sample.expectedPrefix,
               failures);
        Expect(primaryPreview->summary.hasSecondaryPreview ==
                   sample.expectedSecondaryWorldAssetId.has_value(),
               std::string("DLC5 primary preview secondary-availability mismatch for ") +
                   sample.expectedPrefix,
               failures);
        Expect(PreviewContainsZoneType(*primaryPreview, static_cast<int>(ZoneType::Beach)),
               std::string("DLC5 preview should contain Beach zone for ") +
                   sample.expectedPrefix,
               failures);
        Expect(PreviewContainsZoneType(*primaryPreview, static_cast<int>(ZoneType::Reef)),
               std::string("DLC5 preview should contain Reef zone for ") +
                   sample.expectedPrefix,
               failures);
        Expect(PreviewContainsZoneType(*primaryPreview, static_cast<int>(ZoneType::KelpForest)),
               std::string("DLC5 preview should contain KelpForest zone for ") +
                   sample.expectedPrefix,
               failures);
        Expect(PreviewContainsZoneType(*primaryPreview, static_cast<int>(ZoneType::Abyss)),
               std::string("DLC5 preview should contain Abyss zone for ") +
                   sample.expectedPrefix,
               failures);
        Expect(PreviewContainsZoneType(*primaryPreview, static_cast<int>(ZoneType::Ocean)),
               std::string("DLC5 preview should contain Ocean zone for ") +
                   sample.expectedPrefix,
               failures);

        PreviewWorldSession session;
        std::string errorMessage;
        bool sessionGenerated = false;
        try {
            Trace(std::string("preview session begin: ") + sample.expectedPrefix);
            sessionGenerated = GeneratePreviewSessionForTest(sample.worldType,
                                                             sample.seed,
                                                             sample.mixing,
                                                             &session,
                                                             &errorMessage);
            Trace(std::string("preview session end: ") + sample.expectedPrefix);
        } catch (const std::bad_alloc &) {
            std::cerr << "[FAIL] bad_alloc during GeneratePreviewSession for "
                      << sample.expectedPrefix << std::endl;
            Trace(std::string("bad_alloc during GeneratePreviewSession: ") + sample.expectedPrefix);
            ++failures;
            continue;
        } catch (const std::exception &ex) {
            std::cerr << "[FAIL] exception during GeneratePreviewSession for "
                      << sample.expectedPrefix << ": " << ex.what() << std::endl;
            Trace(std::string("exception during GeneratePreviewSession: ") + sample.expectedPrefix +
                  " message=" + ex.what());
            ++failures;
            continue;
        }
        Expect(sessionGenerated,
               std::string("DLC5 preview session should generate for ") +
                   sample.expectedPrefix,
               failures);
        Expect(errorMessage.empty(),
               std::string("DLC5 preview session should not report error for ") +
                   sample.expectedPrefix,
               failures);
        Expect(session.primaryPreview.has_value(),
               std::string("DLC5 preview session should expose primary preview for ") +
                   sample.expectedPrefix,
               failures);
        Expect(session.secondaryPreview.has_value() ==
                   sample.expectedSecondaryWorldAssetId.has_value(),
               std::string("DLC5 preview session secondary-presence mismatch for ") +
                   sample.expectedPrefix,
               failures);
        Expect(session.secondaryPlacementIndex.has_value() ==
                   sample.expectedSecondaryPlacementIndex.has_value(),
               std::string("DLC5 preview session secondary-placement mismatch for ") +
                   sample.expectedPrefix,
               failures);

        if (session.primaryPreview.has_value()) {
            Expect(session.primaryPreview->summary.worldAssetId ==
                       sample.expectedPrimaryWorldAssetId,
                   std::string("DLC5 preview session primary world asset id mismatch for ") +
                       sample.expectedPrefix,
                   failures);
            Expect(session.primaryPreview->summary.worldPlacementIndex ==
                       sample.expectedPrimaryPlacementIndex,
                   std::string("DLC5 preview session primary placement index mismatch for ") +
                       sample.expectedPrefix,
                   failures);
        }

        if (sample.expectedSecondaryWorldAssetId.has_value()) {
            Expect(session.secondaryPreview.has_value(),
                   std::string("DLC5 preview session should expose secondary preview for ") +
                       sample.expectedPrefix,
                   failures);
            if (session.secondaryPreview.has_value()) {
                Expect(session.secondaryPreview->summary.worldAssetId ==
                           sample.expectedSecondaryWorldAssetId.value(),
                       std::string("DLC5 preview session secondary world asset id mismatch for ") +
                           sample.expectedPrefix,
                       failures);
            }
            if (session.secondaryPlacementIndex.has_value()) {
                Expect(session.secondaryPlacementIndex.value() ==
                           sample.expectedSecondaryPlacementIndex.value(),
                       std::string("DLC5 preview session secondary placement index mismatch for ") +
                           sample.expectedPrefix,
                       failures);
            }
        }
        Trace(std::string("sample end: ") + sample.expectedPrefix);
    }

    return failures;
}
