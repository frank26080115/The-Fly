cmake_minimum_required(VERSION 3.16.0)

set(COMPONENTS
    framework-arduinoespressif32-src-77c8e93767360b28deee4aedf5d0a1ab
    .dummy
    thefly_linker_overrides
    __pio_env
)

set(EXCLUDE_COMPONENTS
    esp_insights
    espressif__esp_insights
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(The-Fly)
