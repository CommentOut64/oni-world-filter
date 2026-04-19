#include "Utils/RecoverableDiagnostics.hpp"

#include <iostream>

namespace {

bool Expect(bool condition, const char *message, int &failures)
{
    if (condition) {
        return true;
    }
    std::cerr << "[FAIL] " << message << std::endl;
    ++failures;
    return false;
}

} // namespace

int RunAllTests()
{
    int failures = 0;

    Expect(!ShouldEmitRecoverableWorldGenDiagnostic(
               "compute child node pd failed, fallback to compute node."),
           "child pd fallback should be suppressed",
           failures);
    Expect(!ShouldEmitRecoverableWorldGenDiagnostic(
               "compute node pd failed, fallback to compute node."),
           "overworld pd fallback should be suppressed",
           failures);
    Expect(!ShouldEmitRecoverableWorldGenDiagnostic(
               "compute node pd failed after convert unknown cells, fallback to compute node."),
           "post-convert pd fallback should be suppressed",
           failures);
    Expect(!ShouldEmitRecoverableWorldGenDiagnostic("intersection result is empty."),
           "empty intersection should be suppressed",
           failures);
    Expect(!ShouldEmitRecoverableWorldGenDiagnostic("subj: 1,2; clip: 3,4;"),
           "intersection geometry dump should be suppressed",
           failures);
    Expect(ShouldEmitRecoverableWorldGenDiagnostic("fallback compute child node failed."),
           "real child fallback failure should still log",
           failures);
    Expect(ShouldEmitRecoverableWorldGenDiagnostic("generate overworld failed."),
           "real overworld failure should still log",
           failures);

    if (failures != 0) {
        std::cerr << "[FAIL] recoverable diagnostics regression count=" << failures
                  << std::endl;
        return 1;
    }

    std::cout << "[PASS] recoverable diagnostics" << std::endl;
    return 0;
}
