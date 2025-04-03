
file(GLOB bench_sources "*.cpp")

foreach (bench_source ${bench_sources})
    get_filename_component(bench_name ${bench_source} NAME_WE)
    add_executable(${bench_name} ${bench_source})
    target_link_libraries(${bench_name} benchmark::benchmark)
endforeach ()