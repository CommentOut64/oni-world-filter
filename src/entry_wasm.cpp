#include <emscripten.h>

#include <string>

#include "App/AppRuntime.hpp"
#include "App/ResultSink.hpp"
#include "Setting/SettingsCache.hpp"

// I defined only one function for exchanging data between c++ and js,
// it get resource from js and set result to js.
extern "C" void jsExchangeData(uint32_t type, uint32_t count, size_t data);

static WasmResultSink g_wasmSink;

static AppRuntime *GetRuntime()
{
    auto *runtime = AppRuntime::Instance();
    if (runtime->GetResultSink() == nullptr) {
        runtime->SetResultSink(&g_wasmSink);
    }
    return runtime;
}

extern "C" void EMSCRIPTEN_KEEPALIVE app_init(int seed)
{
    GetRuntime()->Initialize(seed);
}

extern "C" bool EMSCRIPTEN_KEEPALIVE app_generate(int type, int seed, int mix)
{
    static const char *worlds[] = {
        "SNDST-A-",  "OCAN-A-",    "S-FRZ-",     "LUSH-A-",    "FRST-A-",
        "VOLCA-",    "BAD-A-",     "HTFST-A-",   "OASIS-A-",   "CER-A-",
        "CERS-A-",   "PRE-A-",     "PRES-A-",    "V-SNDST-C-", "V-OCAN-C-",
        "V-SWMP-C-", "V-SFRZ-C-",  "V-LUSH-C-",  "V-FRST-C-",  "V-VOLCA-C-",
        "V-BAD-C-",  "V-HTFST-C-", "V-OASIS-C-", "V-CER-C-",   "V-CERS-C-",
        "V-PRE-C-",  "V-PRES-C-",  "SNDST-C-",   "PRE-C-",     "CER-C-",
        "FRST-C-",   "SWMP-C-",    "M-SWMP-C-",  "M-BAD-C-",   "M-FRZ-C-",
        "M-FLIP-C-", "M-RAD-C-",   "M-CERS-C-"};
    if (type < 0 || static_cast<int>(std::size(worlds)) <= type) {
        return false;
    }
    std::string code = worlds[type];
    int traits = 0;
    if (seed < 0) {
        traits = -seed;
        seed = 0;
    }
    code += std::to_string(seed);
    code += "-0-D3-";
    code += SettingsCache::BinaryToBase36(mix);
    return GetRuntime()->Generate(code, traits);
}
