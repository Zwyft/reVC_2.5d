cmake_minimum_required(VERSION 3.14)
project(revc_ped_sprite_baker_tool LANGUAGES CXX)

add_executable(revc_ped_sprite_baker
    main.cpp
)

target_compile_features(revc_ped_sprite_baker PRIVATE cxx_std_11)
