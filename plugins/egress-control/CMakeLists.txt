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
    egress-controller.cpp
    egress-controller.hpp
    egress-state.hpp
    nvs-api-endpoints.hpp
    nvs-credential-store.cpp
    nvs-credential-store.hpp
    nvs-fleet-client.cpp
    nvs-fleet-client.hpp
    nvs-identity.cpp
    nvs-identity.hpp
    nvs-multi-rtmp.cpp
    nvs-multi-rtmp.hpp
    nvs-room-client.cpp
    nvs-room-client.hpp
    nvs-stream-target-client.cpp
    nvs-stream-target-client.hpp
    nvs-startup.cpp
    nvs-startup.hpp
    nvs-tray.cpp
    nvs-tray.hpp
    program-preview-widget.cpp
    program-preview-widget.hpp
    stream-console-window.cpp
    stream-console-window.hpp
)

target_link_libraries(egress-control PRIVATE OBS::libobs OBS::frontend-api Qt::Widgets Qt::Network)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in egress-control.rc)
  target_sources(egress-control PRIVATE egress-control.rc)

  # Crypt32 provides DPAPI, used to seal stored credentials at rest.
  target_link_libraries(egress-control PRIVATE crypt32)
endif()

set_target_properties_obs(egress-control PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
