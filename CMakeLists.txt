cmake_minimum_required(VERSION 3.20...3.31)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" MQTTVSCPD_VERSION_MAJOR REGEX "^#define MQTTVSCPD_VERSION_MAJOR[ \\t]+([0-9]+)$")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" MQTTVSCPD_VERSION_MINOR REGEX "^#define MQTTVSCPD_VERSION_MINOR[ \\t]+([0-9]+)$")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" MQTTVSCPD_VERSION_PATCH REGEX "^#define MQTTVSCPD_VERSION_PATCH[ \\t]+([0-9]+)$")
string(REGEX REPLACE "^#define MQTTVSCPD_VERSION_(MAJOR|MINOR|PATCH)[ \\t]+" "" MQTTVSCPD_VERSION_MAJOR "${MQTTVSCPD_VERSION_MAJOR}")
string(REGEX REPLACE "^#define MQTTVSCPD_VERSION_(MAJOR|MINOR|PATCH)[ \\t]+" "" MQTTVSCPD_VERSION_MINOR "${MQTTVSCPD_VERSION_MINOR}")
string(REGEX REPLACE "^#define MQTTVSCPD_VERSION_(MAJOR|MINOR|PATCH)[ \\t]+" "" MQTTVSCPD_VERSION_PATCH "${MQTTVSCPD_VERSION_PATCH}")

project(mqttvscpd
    VERSION ${MQTTVSCPD_VERSION_MAJOR}.${MQTTVSCPD_VERSION_MINOR}.${MQTTVSCPD_VERSION_PATCH}
    DESCRIPTION "VSCP MQTT daemon"
    LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)

if(UNIX AND NOT APPLE)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Installation prefix" FORCE)
    set(CMAKE_INSTALL_SYSCONFDIR "/etc" CACHE PATH "System configuration directory" FORCE)
    set(CMAKE_INSTALL_LOCALSTATEDIR "/var" CACHE PATH "State directory" FORCE)
endif()

option(MQTTVSCPD_BUILD_MANPAGES "Build and install man pages" ON)
set(VSCPD_PACKAGE_GENERATORS "" CACHE STRING "Semicolon-separated list of CPack generators to enable")
set(VSCPD_BUILD_NUMBER "" CACHE STRING "Optional CI build number appended to package version")

if(DEFINED MQTTVSCPD_PACKAGE_GENERATORS AND NOT VSCPD_PACKAGE_GENERATORS)
    set(VSCPD_PACKAGE_GENERATORS "${MQTTVSCPD_PACKAGE_GENERATORS}")
endif()
if(DEFINED MQTTVSCPD_BUILD_NUMBER AND NOT VSCPD_BUILD_NUMBER)
    set(VSCPD_BUILD_NUMBER "${MQTTVSCPD_BUILD_NUMBER}")
endif()

set(VSCP_BUILD_TESTS OFF CACHE BOOL "Build VSCP library tests" FORCE)
set(VSCP_INSTALL OFF CACHE BOOL "Install VSCP helper libraries" FORCE)
add_subdirectory(third-party/vscp)

if(NOT TARGET vscp_sqlite3)
    add_library(vscp_sqlite3 STATIC
        third-party/vscp/third-party/sqlite3/sqlite3.c)
    target_include_directories(vscp_sqlite3
        PUBLIC
            third-party/vscp/third-party/sqlite3)
endif()

target_include_directories(vscp_mdf
    PUBLIC
        third-party/json/include
        third-party/Mustache
        third-party/spdlog/include
        third-party/vscp/third_party/maddy/include)

set(MQTTVSCPD_SOURCES
    src/controlobject.cpp
    src/devicelist.cpp
    src/devicethread.cpp
    src/vscpd.cpp)

if(WIN32)
    list(APPEND MQTTVSCPD_SOURCES src/pch.cpp)
endif()

add_executable(mqttvscpd ${MQTTVSCPD_SOURCES})

target_include_directories(mqttvscpd
    PRIVATE
        src
        third-party/json/include
        third-party/vscp/third-party/sqlite3)

target_link_libraries(mqttvscpd
    PRIVATE
        vscp_common
        vscp_sqlite3)

if(WIN32)
    find_package(pthread REQUIRED)
    if(TARGET PThreads4W::PThreads4W_CXXEXC)
        target_link_libraries(mqttvscpd PRIVATE PThreads4W::PThreads4W_CXXEXC)
    endif()

    target_link_libraries(mqttvscpd PRIVATE netapi32)

    if(MSVC)
        target_compile_options(mqttvscpd PRIVATE /utf-8)
    endif()

    target_compile_definitions(mqttvscpd
        PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _WINSOCK_DEPRECATED_NO_WARNINGS
            WIN32_LEAN_AND_MEAN)
endif()

if(UNIX AND NOT APPLE)
    find_path(SYSTEMD_INCLUDE_DIR systemd/sd-daemon.h)
    find_library(SYSTEMD_LIBRARY NAMES systemd)
    if(NOT SYSTEMD_INCLUDE_DIR OR NOT SYSTEMD_LIBRARY)
        message(FATAL_ERROR "libsystemd headers and library are required to build mqttvscpd on Linux.")
    endif()

    target_include_directories(mqttvscpd PRIVATE ${SYSTEMD_INCLUDE_DIR})
    target_link_libraries(mqttvscpd PRIVATE ${SYSTEMD_LIBRARY})
endif()

if(MQTTVSCPD_BUILD_MANPAGES AND UNIX)
    add_subdirectory(man)
endif()

if(WIN32)
    set(MQTTVSCPD_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(MQTTVSCPD_CONFIG_DIR config)
    set(MQTTVSCPD_CERTS_DIR certs)
    set(MQTTVSCPD_DATA_ROOT share/mqttvscp)
    set(MQTTVSCPD_STATE_DIR data)
elseif(APPLE)
    set(MQTTVSCPD_RUNTIME_DESTINATION ${CMAKE_INSTALL_SBINDIR})
    set(MQTTVSCPD_CONFIG_DIR ${CMAKE_INSTALL_SYSCONFDIR}/vscp)
    set(MQTTVSCPD_CERTS_DIR ${MQTTVSCPD_CONFIG_DIR}/certs)
    set(MQTTVSCPD_DATA_ROOT ${CMAKE_INSTALL_DATADIR}/mqttvscp)
    set(MQTTVSCPD_STATE_DIR ${CMAKE_INSTALL_LOCALSTATEDIR}/lib/vscp/mqttvscpd)
else()
    set(MQTTVSCPD_RUNTIME_DESTINATION ${CMAKE_INSTALL_SBINDIR})
    set(MQTTVSCPD_CONFIG_DIR ${CMAKE_INSTALL_SYSCONFDIR}/vscp)
    set(MQTTVSCPD_CERTS_DIR ${MQTTVSCPD_CONFIG_DIR}/certs)
    set(MQTTVSCPD_DATA_ROOT ${CMAKE_INSTALL_DATADIR}/mqttvscp)
    set(MQTTVSCPD_STATE_DIR ${CMAKE_INSTALL_LOCALSTATEDIR}/lib/vscp/mqttvscpd)
endif()

if(NOT WIN32)
    install(PROGRAMS
        resources/linux/download-event-db
        resources/linux/vscp_eth_to_guid
        resources/linux/vscp_ip_to_guid
        DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS mqttvscpd
    RUNTIME DESTINATION ${MQTTVSCPD_RUNTIME_DESTINATION})

install(FILES
    LICENSE
    README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

install(FILES
    resources/linux/mqttvscpd.json
    resources/linux/mqttvscpd.json.min-example
    DESTINATION ${MQTTVSCPD_CONFIG_DIR})

if(WIN32)
    install(FILES resources/vscp.key DESTINATION ${MQTTVSCPD_CONFIG_DIR})
else()
    install(FILES resources/vscp.key
        DESTINATION ${MQTTVSCPD_CONFIG_DIR}
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ)
endif()

install(FILES
    resources/README-ca-certificates
    resources/README-certs
    DESTINATION ${MQTTVSCPD_DATA_ROOT})

install(DIRECTORY resources/database/
    DESTINATION ${MQTTVSCPD_STATE_DIR})

install(DIRECTORY resources/certs/
    DESTINATION ${MQTTVSCPD_CERTS_DIR})

if(NOT WIN32)
    install(DIRECTORY DESTINATION ${CMAKE_INSTALL_LOCALSTATEDIR}/lib/vscp/drivers/level1)
    install(DIRECTORY DESTINATION ${CMAKE_INSTALL_LOCALSTATEDIR}/lib/vscp/drivers/level2)
    install(DIRECTORY DESTINATION ${CMAKE_INSTALL_LOCALSTATEDIR}/lib/vscp/web/html/css)
    install(DIRECTORY DESTINATION ${CMAKE_INSTALL_LOCALSTATEDIR}/lib/vscp/web/html/images)
    install(DIRECTORY DESTINATION ${CMAKE_INSTALL_LOCALSTATEDIR}/lib/vscp/web/html/js)
    install(DIRECTORY DESTINATION ${CMAKE_INSTALL_LOCALSTATEDIR}/log/mqttvscp)
endif()

if(UNIX AND NOT APPLE)
    install(FILES
        resources/linux/mqttvscpd.service
        DESTINATION lib/systemd/system
        RENAME mqttvscpd.service)
    install(FILES
        service/systemd/mqttvscpd.service.simple
        service/systemd/mqttvscpd.service.notify
        DESTINATION ${MQTTVSCPD_DATA_ROOT}/systemd)
    install(FILES
        resources/debpkg/logrotate
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/logrotate.d
        RENAME mqttvscpd)
endif()

set(CPACK_MONOLITHIC_INSTALL ON)
if(WIN32)
    set(CPACK_SET_DESTDIR OFF)
else()
    set(CPACK_SET_DESTDIR ON)
endif()
set(CPACK_PACKAGE_NAME mqttvscpd)
set(CPACK_PACKAGE_VENDOR "Ake Hedman, the VSCP project")
set(CPACK_PACKAGE_CONTACT "info@vscp.org")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The VSCP daemon")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://www.vscp.org")
set(MQTTVSCPD_PACKAGE_VERSION ${PROJECT_VERSION})
if(VSCPD_BUILD_NUMBER)
    set(MQTTVSCPD_PACKAGE_VERSION "${PROJECT_VERSION}.${VSCPD_BUILD_NUMBER}")
endif()
set(CPACK_PACKAGE_VERSION ${MQTTVSCPD_PACKAGE_VERSION})

# Normalize architecture names for package naming
set(MQTTVSCPD_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(MQTTVSCPD_ARCHITECTURE "amd64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|i386)$")
    set(MQTTVSCPD_ARCHITECTURE "i386")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(MQTTVSCPD_ARCHITECTURE "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^armv7l?$")
    set(MQTTVSCPD_ARCHITECTURE "armhf")
endif()

set(CPACK_PACKAGE_FILE_NAME "mqttvscpd-${MQTTVSCPD_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${MQTTVSCPD_ARCHITECTURE}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "vscp")

if(VSCPD_PACKAGE_GENERATORS)
    set(CPACK_GENERATOR "${VSCPD_PACKAGE_GENERATORS}")
elseif(WIN32)
    find_program(MQTTVSCPD_MAKENSIS_EXECUTABLE makensis)
    if(MQTTVSCPD_MAKENSIS_EXECUTABLE)
        set(CPACK_GENERATOR "ZIP;NSIS")
    else()
        set(CPACK_GENERATOR "ZIP")
    endif()
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ;ZIP")
else()
    set(CPACK_GENERATOR "TGZ;DEB;RPM;ZIP")
endif()

if(WIN32)
    set(CPACK_NSIS_DISPLAY_NAME "VSCP Daemon")
    set(CPACK_NSIS_PACKAGE_NAME "VSCP Daemon")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_PACKAGE_EXECUTABLES "mqttvscpd;VSCP Daemon")
endif()

if(UNIX AND NOT APPLE)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ake Hedman <akhe@vscp.org>")
    set(CPACK_DEBIAN_PACKAGE_SECTION net)
    set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS adduser)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/debpkg/postinst;${CMAKE_CURRENT_SOURCE_DIR}/resources/debpkg/prerm;${CMAKE_CURRENT_SOURCE_DIR}/resources/debpkg/postrm;${CMAKE_CURRENT_SOURCE_DIR}/resources/debpkg/conffiles")
endif()

include(CPack)