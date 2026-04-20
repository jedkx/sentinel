cmake_minimum_required(VERSION 3.10)
project(sentinel CXX C)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Waveshare vendor library paths
# ---------------------------------------------------------------------------
set(EPD_BASE "vendor/e-Paper/RaspberryPi_JetsonNano/c/lib")

set(EPD_INCLUDES
    ${EPD_BASE}/Config
    ${EPD_BASE}/GUI
    ${EPD_BASE}/Fonts
    ${EPD_BASE}/e-Paper
)

set(EPD_SOURCES
    ${EPD_BASE}/Config/DEV_Config.c
    ${EPD_BASE}/GUI/GUI_Paint.c
    ${EPD_BASE}/GUI/GUI_BMPfile.c
    ${EPD_BASE}/Fonts/font8.c
    ${EPD_BASE}/Fonts/font12.c
    ${EPD_BASE}/Fonts/font16.c
    ${EPD_BASE}/Fonts/font20.c
    ${EPD_BASE}/Fonts/font24.c
    ${EPD_BASE}/e-Paper/EPD_2in13_V4.c
)

# ---------------------------------------------------------------------------
# Project sources
# ---------------------------------------------------------------------------
set(APP_SOURCES
    src/main.cpp
    src/telemetry.cpp
    src/ui_engine.cpp
)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(sentinel ${APP_SOURCES} ${EPD_SOURCES})

target_include_directories(sentinel PRIVATE
    include
    ${EPD_INCLUDES}
)

# RPI + USE_LGPIO_LIB must both be set for the Waveshare driver
target_compile_definitions(sentinel PRIVATE
    RPI
    USE_LGPIO_LIB
)

target_link_libraries(sentinel PRIVATE lgpio m rt pthread)