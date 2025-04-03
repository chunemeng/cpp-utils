include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(
        benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.2.tar.gz
        URL_MD5 adcead8dd5416fc6c6245f68ff75a761

        DOWNLOAD_NO_PROGRESS 1
        INACTIVITY_TIMEOUT 5
        TIMEOUT 30
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark tests" FORCE)
set(BENCHMARK_ENABLE_EXAMPLES OFF CACHE BOOL "Disable benchmark examples" FORCE)
set(BENCHMARK_INSTALL_COMMAND OFF CACHE BOOL "Disable install" FORCE)

FetchContent_MakeAvailable(benchmark)

if(TARGET benchmark)
    set_target_properties(benchmark PROPERTIES
            RULE_LAUNCH_COMPILE ""
            RULE_LAUNCH_LINK ""
    )
endif()