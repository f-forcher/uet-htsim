# List of common source files shared among executables.
set(LIB_SOURCES
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

# Create static library
add_library(htsim_dc STATIC ${LIB_SOURCES})
target_include_directories(htsim_dc PUBLIC ${PROJECT_SOURCE_DIR}) 
target_include_directories(htsim_dc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}) 

# -------------------------------
# Executable Definitions
# -------------------------------
# List of Executables and Their Main Source Files.
add_executable(htsim_tcp main_tcp.cpp)
add_executable(htsim_ndp main_ndp.cpp)
add_executable(htsim_roce main_roce.cpp)
add_executable(htsim_swift main_swift.cpp)
add_executable(htsim_hpcc main_hpcc.cpp)
add_executable(htsim_eqds main_eqds.cpp)
add_executable(htsim_uec main_uec.cpp)

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
    target_link_libraries(${EXECUTABLE} PRIVATE htsim_dc)
    target_link_libraries(${EXECUTABLE} PRIVATE htsim)
endforeach()

