cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME ATbox_V1_0)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

set(ABOX_PRODUCT_CONFIG_DIR "${CMAKE_SOURCE_DIR}/../platform_config")
set(ABOX_PLATFORM_SCHEDULER "BAREMETAL")
set(ABOX_PLATFORM_USE_CJSON OFF)
add_subdirectory("${CMAKE_SOURCE_DIR}/../platform" abox_platform_build)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/STM32F105XX_FLASH.ld"
)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/User/Src/app_rc.c
    Core/User/Src/app_rc_emout.c
    Core/User/Src/app_rc_led.c
    Core/User/Src/app_rc_motor.c
    Core/User/Src/app_rc_ui.c
    Core/User/Src/bsp_can.c
    Core/User/Src/abox_product_port.c
    Core/User/Src/cJSON.c
    Core/User/Src/led_message.c
    Core/User/Src/motor485.c
    Core/User/Src/mqtt_app.c
    Core/User/Src/mqtt_app_cmd.c
    Core/User/Src/mqtt_app_hb.c
    Core/User/Src/mqtt_core.c
    Core/User/Src/mqtt_core_parser.c
    Core/User/Src/mqtt_core_port.c
    Core/User/Src/ota_info.c
    Core/User/Src/rc_logic.c
    Core/User/Src/rc_protocol.c
    Core/User/Src/serial_bus.c
    Core/User/Src/time_base.c
)

# Product handwritten headers stay outside the generated CubeMX directory.
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/User/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

abox_platform_attach(${CMAKE_PROJECT_NAME})

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
)
