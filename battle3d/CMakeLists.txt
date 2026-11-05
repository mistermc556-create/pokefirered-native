cmake_minimum_required(VERSION 3.16)
project(battle3d C)

set(CMAKE_C_STANDARD 11)

option(BATTLE3D_3DS "Build the Nintendo 3DS citro3d target" OFF)

set(B3D_COMMON
    src/pk3m.c
    src/battle_view.c
)

if(BATTLE3D_3DS)
    add_executable(battle3d_3ds src/main_3ds.c src/renderer_c3d.c ${B3D_COMMON})
    target_include_directories(battle3d_3ds PRIVATE include)
    target_compile_definitions(battle3d_3ds PRIVATE BATTLE3D_3DS=1)
    target_link_libraries(battle3d_3ds citro3d ctru m)
    ctr_generate_smdh(battle3d_3ds
        APP_TITLE "FR X/Y Battle"
        APP_DESCRIPTION "FireRed battle presentation, Kalos camera"
        APP_AUTHOR "romhack"
    )
    ctr_create_3dsx(battle3d_3ds)
else()
    include(FetchContent)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)

    add_executable(battle3d_pc src/main_pc.c src/renderer_gl.c ${B3D_COMMON})
    target_include_directories(battle3d_pc PRIVATE include)
    target_link_libraries(battle3d_pc PRIVATE glfw GL m)
    if(UNIX)
        target_link_libraries(battle3d_pc PRIVATE dl pthread)
    endif()
endif()
