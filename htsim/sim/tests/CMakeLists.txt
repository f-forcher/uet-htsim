# -------------------------------
# Executable Definitions
# -------------------------------
# List of Executables and Their Main Source Files.
add_executable(htsim_dumbell_ndp main_dumbell_ndp.cpp)
add_executable(htsim_dumbell_ndptunnel main_dumbell_ndptunnel.cpp)
add_executable(htsim_dumbell_tcp main_dumbell_tcp.cpp)
add_executable(htsim_dumbell_swift main_dumbell_swift.cpp)
add_executable(htsim_multihop_swift main_multihop_swift.cpp)
add_executable(htsim_multihop_swift2 main_multihop_swift2.cpp)
add_executable(htsim_bidir_swift main_bidir_swift.cpp)
add_executable(htsim_bidir_ndp main_bidir_ndp.cpp)
add_executable(htsim_multipath_swift main_multipath_swift.cpp)
add_executable(htsim_trigger_test main_trigger_test.cpp)
add_executable(htsim_dumbell_roce main_dumbell_roce.cpp)
add_executable(htsim_dumbell_hpcc main_dumbell_hpcc.cpp)
add_executable(htsim_dumbell_strack main_dumbell_strack.cpp)
add_executable(htsim_dumbell_uec main_dumbell_uec.cpp)

# -------------------------------
# Linking Executables with Libraries
# -------------------------------
set(ALL_EXECUTABLES
    htsim_dumbell_ndp
    htsim_dumbell_ndptunnel
    htsim_dumbell_tcp
    htsim_dumbell_swift
    htsim_multihop_swift
    htsim_multihop_swift2
    htsim_bidir_swift
    htsim_bidir_ndp
    htsim_multipath_swift
    htsim_trigger_test
    htsim_dumbell_roce
    htsim_dumbell_hpcc
    htsim_dumbell_strack
    htsim_dumbell_uec
)
foreach(EXECUTABLE ${ALL_EXECUTABLES})
    # Link each executable with the 'htsim' library defined in the parent directory.
    target_link_libraries(${EXECUTABLE} PRIVATE htsim)
    # Add a post-build command to explicitly remove and recreate the symlink for all previous executables.
    add_symlink_target(${EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
