cmake_minimum_required(VERSION 3.20)
project(k3_audio_pipeline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Core Widgets Charts REQUIRED)
find_package(ALSA REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SAMPLERATE REQUIRED samplerate)

add_executable(k3_app
    src/main.cpp
    src/shm_transport.cpp
    src/dsp_engine.cpp
    src/ui_bridge.cpp
    src/persistence_worker.cpp
    src/playback_bypass.cpp
)

target_include_directories(k3_app PRIVATE
    include
    ${ALSA_INCLUDE_DIRS}
    ${SAMPLERATE_INCLUDE_DIRS}
)

target_compile_definitions(k3_app PRIVATE
    QT_DEPRECATED_WARNINGS
)

target_link_libraries(k3_app PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Charts
    ${ALSA_LIBRARIES}
    ${SAMPLERATE_LIBRARIES}
    pthread
)

