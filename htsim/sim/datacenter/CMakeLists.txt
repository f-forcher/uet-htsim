# List of common source files shared among executables.
set(COMMON_SOURCES
    firstfit.cpp
    vl2_topology.cpp
    fat_tree_topology.cpp
    bcube_topology.cpp
    connection_matrix.cpp
    oversubscribed_fat_tree_topology.cpp
    shortflows.cpp
    multihomed_fat_tree_topology.cpp
    star_topology.cpp
    fat_tree_switch.cpp
    dragon_fly_topology.cpp
    generic_topology.cpp
)

# -------------------------------
# Executable Definitions
# -------------------------------
# List of Executables and Their Main Source Files.
add_executable(htsim_tcp main_tcp.cpp ${COMMON_SOURCES} )
add_executable(htsim_ndp main_ndp.cpp ${COMMON_SOURCES})
add_executable(htsim_roce main_roce.cpp ${COMMON_SOURCES})
add_executable(htsim_swift main_swift.cpp ${COMMON_SOURCES} )
add_executable(htsim_hpcc main_hpcc.cpp ${COMMON_SOURCES})
add_executable(htsim_eqds main_eqds.cpp ${COMMON_SOURCES})
add_executable(htsim_uec main_uec.cpp ${COMMON_SOURCES})

# -------------------------------
# Linking Executables with Libraries
# -------------------------------
set (ALL_EXECUTABLES
    htsim_tcp
    htsim_ndp
    htsim_roce
    htsim_swift
    htsim_hpcc
    htsim_eqds
    htsim_uec
)
foreach(EXECUTABLE ${ALL_EXECUTABLES})
    # Link each executable with the 'htsim' library defined in the parent directory.
    target_link_libraries(${EXECUTABLE} PRIVATE htsim)
    # Add a post-build command to explicitly remove and recreate the symlink for all previous executables.
    add_symlink_target(${EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
