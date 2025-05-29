cmake_minimum_required(VERSION 3.16)
include(FetchContent)

project(htsim_project LANGUAGES CXX)

# Set C++ standard and compiler flags
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Ensures GNU extensions are disabled

# Set compiler flags
add_compile_options(
    -Wall
    -g
    -Wsign-compare
    -Wuninitialized
    -fPIE
    -O3
)
# Optionally enable sanitizers
# -fsanitize=address -fno-omit-frame-pointer -fsanitize=undefined"

# Fetch GoogleTest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)
enable_testing()

# enable LTO in release builds
include(CheckIPOSupported)
check_ipo_supported(RESULT HAVE_IPO)
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR
   CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  if(HAVE_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endif()

# Function to add a symlink creation step for an executable. 
# Used to create a symlink in the source directory to the executable 
# in the build directory for easier access and backward compatibility.
function(add_symlink_target EXECUTABLE TARGET_DIR)
    add_custom_command(
        TARGET ${EXECUTABLE}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove ${TARGET_DIR}/${EXECUTABLE} # Remove existing symlink
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            $<TARGET_FILE:${EXECUTABLE}>  # Path to the executable
            ${TARGET_DIR}/${EXECUTABLE}  # Path for the symlink
        COMMENT "Removing and recreating symlink for ${EXECUTABLE} in ${TARGET_DIR}"
    )
endfunction()

# Define source files
set(SOURCE_FILES
    aeolusqueue.cpp
    callback_pipe.cpp
    cbr.cpp
    cbrpacket.cpp
    clock.cpp
    cnppacket.cpp
    compositeprioqueue.cpp
    compositequeue.cpp
    config.cpp
    cpqueue.cpp
    dcqcn.cpp
    dcqcn_logger.cpp
    dctcp.cpp
    dctcp_transfer.cpp
    ecnprioqueue.cpp
    ecnqueue.cpp
    eqds.cpp
    eqds_logger.cpp
    eqdspacket.cpp
    eth_pause_packet.cpp
    eventlist.cpp
    exoqueue.cpp
    fairpullqueue.cpp
    hpcc.cpp
    hpccpacket.cpp
    logfile.cpp
    loggers.cpp
    meter.cpp
    mtcp.cpp
    ndp.cpp
    ndp_transfer.cpp
    ndppacket.cpp
    ndptunnel.cpp
    ndptunnelpacket.cpp
    network.cpp
    oversubscribed_cc.cpp
    pciemodel.cpp
    pipe.cpp
    priopullqueue.cpp
    prioqueue.cpp
    qcn.cpp
    queue.cpp
    queue_lossless.cpp
    queue_lossless_input.cpp
    queue_lossless_output.cpp
    randomqueue.cpp
    rng.cpp
    roce.cpp
    rocepacket.cpp
    route.cpp
    routetable.cpp
    sent_packets.cpp
    strack.cpp
    strackpacket.cpp
    swift.cpp
    swift_scheduler.cpp
    swiftpacket.cpp
    switch.cpp
    tcp.cpp
    tcp_periodic.cpp
    tcp_transfer.cpp
    tcppacket.cpp
    trigger.cpp
    uec.cpp
    uec_logger.cpp
    uecpacket.cpp
    uec_mp.cpp
    uec_pdcses.cpp
)

# Create static library
add_library(htsim STATIC ${SOURCE_FILES})
target_include_directories(htsim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}) # Needed for header-only libraries (e.g., loggertypes.h).

# Add subdirectories
add_subdirectory(datacenter)

# Define executables and link with htsim library
add_executable(parse_output parse_output.cpp)
target_link_libraries(parse_output PRIVATE htsim)

# Unit tests.
set(UNIT_TEST_FILES
    pipe_test
)

foreach(UNIT_TEST_FILE ${UNIT_TEST_FILES}) 
    add_executable(${UNIT_TEST_FILE} ${UNIT_TEST_FILE}.cpp)
    target_link_libraries(${UNIT_TEST_FILE} PRIVATE htsim gtest_main gmock_main)
    gtest_discover_tests(${UNIT_TEST_FILE}
        PROPERTIES LABELS "unit"
        PROPERTIES TIMEOUT 60
    )
endforeach()