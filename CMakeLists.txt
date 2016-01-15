cmake_minimum_required(VERSION 2.6.4)
project(ptydaemonizer C)
add_definitions(-DPROJECT_NAME="${CMAKE_PROJECT_NAME}")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")

set(SOURCE_FILES ptydaemonizer.c arguments.c arguments.h ptydaemonizer.h)

add_executable(ptydaemonizer ${SOURCE_FILES})

option(WITH_SYSTEMD "Configure as a SystemD daemon" OFF)

if(WITH_SYSTEMD)
    add_definitions(-DWITH_SYSTEMD)
    find_package(SystemD REQUIRED)
    include_directories(${SYSTEMD_INCLUDE_DIRS})
    set(LIBS ${LIBS} ${SYSTEMD_LIBRARIES})
else()
    find_package(Syslog REQUIRED)
    include_directories(${SYSLOG_INCLUDE_DIRS})
    set(LIBS ${LIBS} ${SYSLOG_LIBRARIES})
endif()

target_link_libraries(ptydaemonizer ${LIBS})