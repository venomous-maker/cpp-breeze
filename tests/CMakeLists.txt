add_executable(breeze_tests tests.cpp)

target_link_libraries(breeze_tests PRIVATE breeze::breeze)

add_test(NAME breeze_tests COMMAND breeze_tests)
