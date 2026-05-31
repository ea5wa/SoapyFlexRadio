cmake_minimum_required(VERSION 3.10)
project(SoapyFlexRadio VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
    # Usar runtime estático para evitar dependencias de dlls externas
    foreach(flag_var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        if(${flag_var} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
        endif()
    endforeach()
endif()

find_package(SoapySDR "0.7" REQUIRED)

set(SOURCES
    src/SoapyFlexRadio.cpp
    src/Registration.cpp
)

SOAPY_SDR_MODULE_UTIL(
    TARGET  SoapyFlexRadio
    SOURCES ${SOURCES}
)

# Forzar exportación de símbolos en Windows
if(WIN32)
    set_target_properties(SoapyFlexRadio PROPERTIES
        LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/src/SoapyFlexRadio.def\""
    )
endif()

target_include_directories(SoapyFlexRadio PRIVATE src)

target_compile_options(SoapyFlexRadio PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3>
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -O2>
)

if(WIN32)
    target_link_libraries(SoapyFlexRadio PRIVATE ws2_32)
endif()

message(STATUS "")
message(STATUS "=== SoapyFlexRadio v2 ===")
message(STATUS "  Backend: smartsdr-iqtransfer (UDP)")
message(STATUS "  Puerto UDP por defecto: 5900")
message(STATUS "")
message(STATUS "Uso:")
message(STATUS "  1. Iniciar smartsdr-iqtransfer.exe --RADIO=<IP> --MYUDP=5999 --RATE=96000 --CH=1 --FWD=127.0.0.1:5900")
message(STATUS "  2. SoapySDRUtil.exe --find=\"driver=FlexRadio\"")
message(STATUS "  3. Configurar SkyRoof con driver=FlexRadio,udp_port=5900")
message(STATUS "")
