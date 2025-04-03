

file(GLOB test_sources "*.cpp")

foreach (test_source ${test_sources})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} gtest_main)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach ()