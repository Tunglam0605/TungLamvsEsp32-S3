cmake_minimum_required(VERSION 3.22)

project(bsp_host_tests C)

enable_testing()

add_executable(bsp_do_state_test
    bsp_do_state_test.c
    ../../components/bsp/bsp_waveshare_s3_8di8do/src/bsp_do_state.c)

target_include_directories(bsp_do_state_test PRIVATE
    ../../components/bsp/bsp_waveshare_s3_8di8do/private_include)

add_test(NAME bsp_do_state_test COMMAND bsp_do_state_test)
