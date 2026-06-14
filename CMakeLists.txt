cmake_minimum_required(VERSION 3.16.0)

set(THEFLY_ARDUINO_COMPONENT framework-arduinoespressif32)
foreach(component_dir IN LISTS EXTRA_COMPONENT_DIRS)
    get_filename_component(component_name "${component_dir}" NAME)
    if(component_name MATCHES "^framework-arduinoespressif32")
        set(THEFLY_ARDUINO_COMPONENT "${component_name}")
    endif()
endforeach()

set(COMPONENTS
    ${THEFLY_ARDUINO_COMPONENT}
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
