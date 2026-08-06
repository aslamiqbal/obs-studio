cmake_minimum_required(VERSION 3.28...3.30)

find_package(Qt6 REQUIRED Widgets Network)

add_library(egress-control MODULE)
add_library(OBS::egress-control ALIAS egress-control)

target_sources(
  egress-control
  PRIVATE
    egress-api-client.cpp
    egress-api-client.hpp
    egress-config.cpp
    egress-config.hpp
    egress-control-dock.cpp
    egress-control-dock.hpp
    egress-control-plugin.cpp
    egress-state.hpp
)

target_link_libraries(egress-control PRIVATE OBS::libobs OBS::frontend-api Qt::Widgets Qt::Network)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in egress-control.rc)
  target_sources(egress-control PRIVATE egress-control.rc)
endif()

set_target_properties_obs(egress-control PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
