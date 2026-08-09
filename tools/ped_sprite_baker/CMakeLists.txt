cmake_minimum_required(VERSION 3.14)
project(revc_ped_sprite_baker_tool LANGUAGES CXX)

add_executable(revc_ped_sprite_baker
    main.cpp
    backend.cpp
    backend.h
)

target_compile_features(revc_ped_sprite_baker PRIVATE cxx_std_11)

if(TARGET librw::librw)
    target_link_libraries(revc_ped_sprite_baker PRIVATE librw::librw)
    target_compile_definitions(revc_ped_sprite_baker PRIVATE REVC_PED_BAKER_WITH_LIBRW)
endif()
