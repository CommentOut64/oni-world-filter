set(SETTING_HEADER_FILES
    "Setting/ClusterLayout.hpp"
    "Setting/DefaultSettings.hpp"
    "Setting/SampleDescriber.hpp"
    "Setting/SimHashes.hpp"
    "Setting/TemplateContainer.hpp"
    "Setting/World.hpp"
    "Setting/WorldGenClasses.hpp"
)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/JsonDeserializeGen.hpp"
    COMMAND ${PYTHON_CMD} "${CMAKE_CURRENT_SOURCE_DIR}/JsonDeserializeGen.py"
                          "${CMAKE_CURRENT_SOURCE_DIR}/Setting"
    DEPENDS ${SETTING_HEADER_FILES} "JsonDeserializeGen.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)

if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/data.bin")
execute_process(COMMAND ${CMAKE_COMMAND} -E echo_append "keep"
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/keep.txt"
)
execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar cf "${CMAKE_CURRENT_BINARY_DIR}/data.zip"
                --format=zip dlc templates worldgen Asubworlds.json
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/asset"
)
execute_process(COMMAND ${CMAKE_COMMAND} -E cat "keep.txt" "data.zip"
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/data.bin"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
endif()

file(SIZE "${CMAKE_CURRENT_BINARY_DIR}/data.zip" SETTING_ASSET_FILESIZE)

if(EMSCRIPTEN)
set(ONI_ENTRY_SOURCE "entry_wasm.cpp")
else()
set(ONI_ENTRY_SOURCE "entry_cli.cpp")
endif()

set(ONI_BASE_SOURCES
    ${SETTING_HEADER_FILES}
    "${CMAKE_CURRENT_BINARY_DIR}/JsonDeserializeGen.hpp"
    "App/ResultModels.hpp"
    "App/ResultSink.hpp"
    "App/AppRuntime.hpp"
    "App/AppRuntime.cpp"
    "App/BatchCaptureSink.cpp"
    "Batch/FilterConfig.hpp"
    "Batch/FilterConfig.cpp"
    "Batch/CpuTopology.hpp"
    "Batch/CpuTopology.cpp"
    "Batch/ThreadPolicy.hpp"
    "Batch/ThreadPolicy.cpp"
    "Batch/DesktopSearchPolicy.hpp"
    "Batch/DesktopSearchPolicy.cpp"
    "Batch/ThroughputCalibration.hpp"
    "Batch/ThroughputCalibration.cpp"
    "Batch/SessionWarmupPlanner.hpp"
    "Batch/SessionWarmupPlanner.cpp"
    "Batch/BatchMatcher.hpp"
    "Batch/BatchMatcher.cpp"
    "Batch/SearchRequest.hpp"
    "Batch/SearchEvents.hpp"
    "Batch/BatchSearchService.hpp"
    "Batch/BatchSearchService.cpp"
    "Batch/SidecarProtocol.hpp"
    "Batch/SidecarProtocol.cpp"
    "SearchAnalysis/SearchCatalog.hpp"
    "SearchAnalysis/SearchCatalog.cpp"
    "SearchAnalysis/TraitCatalog.hpp"
    "SearchAnalysis/TraitCatalog.cpp"
    "SearchAnalysis/SearchConstraintModel.hpp"
    "SearchAnalysis/SearchConstraintModel.cpp"
    "SearchAnalysis/SearchConstraintNormalizer.hpp"
    "SearchAnalysis/SearchConstraintNormalizer.cpp"
    "SearchAnalysis/HardValidator.hpp"
    "SearchAnalysis/HardValidator.cpp"
    "SearchAnalysis/PoissonBinomial.hpp"
    "SearchAnalysis/PoissonBinomial.cpp"
    "SearchAnalysis/ExplainabilityBuilder.hpp"
    "SearchAnalysis/ExplainabilityBuilder.cpp"
    "SearchAnalysis/BottleneckSelectivityPredictor.hpp"
    "SearchAnalysis/BottleneckSelectivityPredictor.cpp"
    "SearchAnalysis/WorldEnvelopeProfile.hpp"
    "SearchAnalysis/WorldEnvelopeProfile.cpp"
    "Setting/ComposableDictionary.hpp"
    "Setting/JsonDeserialize.hpp"
    "Setting/SettingsCache.hpp"
    "Setting/WorldGenClasses.cpp"
    "Setting/SettingsCache.cpp"
    "Utils/ConvexHull/Constants.hpp"
    "Utils/ConvexHull/Algorithm.hpp"
    "Utils/ConvexHull/Algorithm.cpp"
    "Utils/ConvexHull/DataModel.hpp"
    "Utils/ConvexHull/MathHelper.hpp"
    "Utils/ConvexHull/MathHelper.cpp"
    "Utils/ConvexHull.hpp"
    "Utils/ConvexHull.cpp"
    "Utils/KRandom.hpp"
    "Utils/KRandom.cpp"
    "Utils/PointGenerator.hpp"
    "Utils/PointGenerator.cpp"
    "Utils/Vector2f.hpp"
    "Utils/Polygon.hpp"
    "Utils/Polygon.cpp"
    "Utils/Halfedge.hpp"
    "Utils/Halfedge.cpp"
    "Utils/Voronoi.hpp"
    "Utils/Voronoi.cpp"
    "Utils/Diagram.hpp"
    "Utils/Diagram.cpp"
    "WorldGen.hpp"
    "WorldGen.cpp"
    "TemplateSpawning.cpp"
    "BatchCpu/CpuOptimization.hpp"
    "BatchCpu/CpuOptimization.cpp"
)

add_executable(oniWorldApp
    ${ONI_BASE_SOURCES}
    "App/WasmResultSink.cpp"
    "${ONI_ENTRY_SOURCE}"
)
if(EMSCRIPTEN)
target_compile_options(oniWorldApp PUBLIC "-Wall" "-Wextra" "-Werror")
target_link_options(oniWorldApp
    PUBLIC "-sALLOW_MEMORY_GROWTH" "-sINITIAL_HEAP=33554432"
    PUBLIC "-sEXPORTED_FUNCTIONS=[_app_init,_app_generate]"
    PUBLIC "-sMINIMAL_RUNTIME=1" "-sEXPORTED_RUNTIME_METHODS=[HEAP32,HEAPU8]"
    PUBLIC "--pre-js" "${CMAKE_CURRENT_SOURCE_DIR}/jsUtils/prefix.js"
    PUBLIC "--js-library" "${CMAKE_CURRENT_SOURCE_DIR}/jsUtils/getresult.js")
add_custom_command(TARGET oniWorldApp POST_BUILD
    COMMAND ${PYTHON_CMD} "${CMAKE_CURRENT_SOURCE_DIR}/JoinWasmFilesHash.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM
)
list(APPEND WASM_LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/jsUtils/prefix.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/jsUtils/getresult.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/JoinWasmFilesHash.py"
)
set_target_properties(oniWorldApp PROPERTIES LINK_DEPENDS "${WASM_LINK_DEPENDS}")
elseif(MSVC)
target_compile_options(oniWorldApp
    PUBLIC "/source-charset:utf-8" "/execution-charset:GBK" /MP /W4 /WX /wd4244 /wd4267)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
target_compile_options(oniWorldApp
    PUBLIC "-Wall" "-Wextra" "-Wno-conversion"
    PUBLIC "-finput-charset=UTF-8" "-fexec-charset=UTF-8")
find_package(Threads REQUIRED)
target_link_libraries(oniWorldApp PUBLIC Threads::Threads)
endif()
target_include_directories(oniWorldApp
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC "${CMAKE_CURRENT_BINARY_DIR}"
    PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/clipper"
    PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/jsoncpp"
    PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/miniz"
)
target_link_libraries(oniWorldApp PUBLIC clipper jsoncpp miniz)

if(NOT EMSCRIPTEN AND ONI_BUILD_SIDECAR)
add_executable(oni-sidecar EXCLUDE_FROM_ALL
    ${ONI_BASE_SOURCES}
    "entry_sidecar.cpp"
)
target_include_directories(oni-sidecar
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC "${CMAKE_CURRENT_BINARY_DIR}"
    PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/clipper"
    PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/jsoncpp"
    PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/miniz"
)
target_link_libraries(oni-sidecar PUBLIC clipper jsoncpp miniz)
if(MSVC)
target_compile_options(oni-sidecar
    PUBLIC "/source-charset:utf-8" "/execution-charset:GBK" /MP /W4 /WX /wd4244 /wd4267
    PUBLIC "$<$<CONFIG:Release>:/Zi>")
target_link_options(oni-sidecar
    PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>"
    PRIVATE "$<$<CONFIG:Release>:/MAP:oni-sidecar.map>")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
target_compile_options(oni-sidecar
    PUBLIC "-Wall" "-Wextra" "-Wno-conversion"
    PUBLIC "-finput-charset=UTF-8" "-fexec-charset=UTF-8")
target_link_libraries(oni-sidecar PUBLIC Threads::Threads)
endif()
endif()

if(NOT EMSCRIPTEN)
add_executable(test_filter_config
    "../tests/native/test_main.cpp"
    "../tests/native/test_filter_config.cpp"
    "Batch/FilterConfig.cpp"
)
target_include_directories(test_filter_config
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_filter_config PRIVATE jsoncpp)
add_test(NAME test_filter_config COMMAND test_filter_config)

add_executable(test_batch_matcher
    "../tests/native/test_main.cpp"
    "../tests/native/test_batch_matcher.cpp"
    "Batch/BatchMatcher.cpp"
    "Batch/FilterConfig.cpp"
)
target_include_directories(test_batch_matcher
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_batch_matcher PRIVATE jsoncpp)
add_test(NAME test_batch_matcher COMMAND test_batch_matcher)

add_executable(test_batch_search_smoke
    "../tests/native/test_main.cpp"
    "../tests/native/test_batch_search_smoke.cpp"
    "Batch/BatchSearchService.cpp"
    "BatchCpu/CpuOptimization.cpp"
)
target_include_directories(test_batch_search_smoke
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(TARGET Threads::Threads)
target_link_libraries(test_batch_search_smoke PRIVATE Threads::Threads)
endif()
add_test(NAME test_batch_search_smoke COMMAND test_batch_search_smoke)

add_executable(test_cpu_topology
    "../tests/native/test_main.cpp"
    "../tests/native/test_cpu_topology.cpp"
    "Batch/CpuTopology.cpp"
    "Batch/ThreadPolicy.cpp"
    "BatchCpu/CpuOptimization.cpp"
)
target_include_directories(test_cpu_topology
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(TARGET Threads::Threads)
target_link_libraries(test_cpu_topology PRIVATE Threads::Threads)
endif()
add_test(NAME test_cpu_topology COMMAND test_cpu_topology)

add_executable(test_throughput_calibration
    "../tests/native/test_main.cpp"
    "../tests/native/test_throughput_calibration.cpp"
    "Batch/ThroughputCalibration.cpp"
    "BatchCpu/CpuOptimization.cpp"
)
target_include_directories(test_throughput_calibration
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(TARGET Threads::Threads)
target_link_libraries(test_throughput_calibration PRIVATE Threads::Threads)
endif()
add_test(NAME test_throughput_calibration COMMAND test_throughput_calibration)

add_executable(test_session_warmup_planner
    "../tests/native/test_main.cpp"
    "../tests/native/test_session_warmup_planner.cpp"
    "Batch/SessionWarmupPlanner.cpp"
    "Batch/ThroughputCalibration.cpp"
    "BatchCpu/CpuOptimization.cpp"
)
target_include_directories(test_session_warmup_planner
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(TARGET Threads::Threads)
target_link_libraries(test_session_warmup_planner PRIVATE Threads::Threads)
endif()
add_test(NAME test_session_warmup_planner COMMAND test_session_warmup_planner)

add_executable(test_desktop_search_policy
    "../tests/native/test_main.cpp"
    "../tests/native/test_desktop_search_policy.cpp"
    "Batch/DesktopSearchPolicy.cpp"
)
target_include_directories(test_desktop_search_policy
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(TARGET Threads::Threads)
target_link_libraries(test_desktop_search_policy PRIVATE Threads::Threads)
endif()
add_test(NAME test_desktop_search_policy COMMAND test_desktop_search_policy)

add_executable(test_adaptive_concurrency
    "../tests/native/test_main.cpp"
    "../tests/native/test_adaptive_concurrency.cpp"
    "Batch/BatchSearchService.cpp"
    "BatchCpu/CpuOptimization.cpp"
)
target_include_directories(test_adaptive_concurrency
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(TARGET Threads::Threads)
target_link_libraries(test_adaptive_concurrency PRIVATE Threads::Threads)
endif()
add_test(NAME test_adaptive_concurrency COMMAND test_adaptive_concurrency)

add_executable(test_settings_cache
    "../tests/native/test_main.cpp"
    "../tests/native/test_settings_cache.cpp"
    ${ONI_BASE_SOURCES}
)
target_include_directories(test_settings_cache
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/clipper"
    PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/jsoncpp"
    PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/miniz"
)
target_link_libraries(test_settings_cache PRIVATE clipper jsoncpp miniz)
if(TARGET Threads::Threads)
target_link_libraries(test_settings_cache PRIVATE Threads::Threads)
endif()
add_test(NAME test_settings_cache COMMAND test_settings_cache)

add_executable(test_app_runtime_worker_reset
    "../tests/native/test_main.cpp"
    "../tests/native/test_app_runtime_worker_reset.cpp"
    ${ONI_BASE_SOURCES}
)
target_include_directories(test_app_runtime_worker_reset
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/clipper"
    PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/jsoncpp"
    PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/miniz"
)
target_link_libraries(test_app_runtime_worker_reset PRIVATE clipper jsoncpp miniz)
if(TARGET Threads::Threads)
target_link_libraries(test_app_runtime_worker_reset PRIVATE Threads::Threads)
endif()
add_test(NAME test_app_runtime_worker_reset COMMAND test_app_runtime_worker_reset)

add_executable(test_sidecar_protocol
    "../tests/native/test_main.cpp"
    "../tests/native/test_sidecar_protocol.cpp"
    "Batch/SidecarProtocol.cpp"
    "Batch/FilterConfig.cpp"
)
target_include_directories(test_sidecar_protocol
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_sidecar_protocol PRIVATE jsoncpp)
add_test(NAME test_sidecar_protocol COMMAND test_sidecar_protocol)

add_executable(test_recoverable_diagnostics
    "../tests/native/test_main.cpp"
    "../tests/native/test_recoverable_diagnostics.cpp"
)
target_include_directories(test_recoverable_diagnostics
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME test_recoverable_diagnostics COMMAND test_recoverable_diagnostics)

add_executable(test_search_analysis
    "../tests/native/test_main.cpp"
    "../tests/native/test_search_analysis.cpp"
    "SearchAnalysis/SearchConstraintNormalizer.cpp"
    "SearchAnalysis/HardValidator.cpp"
    "SearchAnalysis/PoissonBinomial.cpp"
    "SearchAnalysis/ExplainabilityBuilder.cpp"
    "SearchAnalysis/BottleneckSelectivityPredictor.cpp"
    "Batch/FilterConfig.cpp"
)
target_include_directories(test_search_analysis
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_search_analysis PRIVATE jsoncpp)
add_test(NAME test_search_analysis COMMAND test_search_analysis)

# 临时表征测试，先停用。
# add_executable(test_search_runtime_equivalence
#     "../tests/native/test_main.cpp"
#     "../tests/native/test_search_runtime_equivalence.cpp"
# )
# target_include_directories(test_search_runtime_equivalence
#     PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
# )
# add_test(NAME test_search_runtime_equivalence COMMAND test_search_runtime_equivalence)

add_executable(test_poisson_binomial
    "../tests/native/test_main.cpp"
    "../tests/native/test_poisson_binomial.cpp"
    "SearchAnalysis/PoissonBinomial.cpp"
)
target_include_directories(test_poisson_binomial
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test_poisson_binomial PRIVATE jsoncpp)
add_test(NAME test_poisson_binomial COMMAND test_poisson_binomial)

add_executable(test_world_envelope_profile
    "../tests/native/test_main.cpp"
    "../tests/native/test_world_envelope_profile.cpp"
    ${ONI_BASE_SOURCES}
)
target_include_directories(test_world_envelope_profile
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/clipper"
    PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/jsoncpp"
    PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty/miniz"
)
target_link_libraries(test_world_envelope_profile PRIVATE clipper jsoncpp miniz)
add_test(NAME test_world_envelope_profile COMMAND test_world_envelope_profile)
endif()

configure_file("config.h.in" "config.h")
