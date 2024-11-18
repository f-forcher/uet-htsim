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

# Add Executables
# htsim_tcp
add_executable(htsim_tcp main_tcp.cpp ${COMMON_SOURCES} )
target_link_libraries(htsim_tcp PRIVATE htsim)

# htsim_ndp
add_executable(htsim_ndp main_ndp.cpp ${COMMON_SOURCES})
target_link_libraries(htsim_ndp PRIVATE htsim)

# htsim_roce
add_executable(htsim_roce main_roce.cpp ${COMMON_SOURCES})
target_link_libraries(htsim_roce PRIVATE htsim)

# htsim_swift
add_executable(htsim_swift main_swift.cpp ${COMMON_SOURCES} )
target_link_libraries(htsim_swift PRIVATE htsim)

# htsim_hpcc
add_executable(htsim_hpcc main_hpcc.cpp ${COMMON_SOURCES})
target_link_libraries(htsim_hpcc PRIVATE htsim)

# htsim_eqds
add_executable(htsim_eqds main_eqds.cpp ${COMMON_SOURCES})
target_link_libraries(htsim_eqds PRIVATE htsim)

# htsim_uec
add_executable(htsim_uec main_uec.cpp ${COMMON_SOURCES})
target_link_libraries(htsim_uec PRIVATE htsim)

# Add a post-build command to explicitly remove and recreate the symlink for all previous executables.
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
    add_symlink_target(${EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
