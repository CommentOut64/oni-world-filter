#pragma once

#include <string_view>

inline bool ShouldEmitRecoverableWorldGenDiagnostic(std::string_view message)
{
    return message != "compute child node pd failed, fallback to compute node." &&
           message != "compute node pd failed, fallback to compute node." &&
           message !=
               "compute node pd failed after convert unknown cells, fallback to compute node." &&
           message != "intersection result is empty." &&
           !(message.starts_with("subj: ") && message.contains("clip: "));
}
