cmake_minimum_required(VERSION 3.16)
project(FoxNetRelay NONE)

find_package(Python3 3.7 COMPONENTS Interpreter REQUIRED)

set(RELAY_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/foxnet-relay.pyz")
add_custom_command(
  OUTPUT "${RELAY_ARCHIVE}"
  COMMAND "${Python3_EXECUTABLE}" -m zipapp
    "${CMAKE_CURRENT_SOURCE_DIR}"
    -o "${RELAY_ARCHIVE}"
    -p "/usr/bin/env python3"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/__main__.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/relay_http.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/relay.conf.example"
    "${CMAKE_CURRENT_SOURCE_DIR}/foxnet-server.cfg.example"
  COMMENT "Packaging FoxNet relay service"
  VERBATIM
)
add_custom_target(foxnet-relay ALL
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/relay.conf.example"
    "${CMAKE_CURRENT_BINARY_DIR}/relay.conf.example"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/foxnet-server.cfg.example"
    "${CMAKE_CURRENT_BINARY_DIR}/foxnet-server.cfg.example"
  DEPENDS "${RELAY_ARCHIVE}"
  VERBATIM
)
install(PROGRAMS "${RELAY_ARCHIVE}" DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/relay.conf.example"
  "${CMAKE_CURRENT_SOURCE_DIR}/foxnet-server.cfg.example" DESTINATION bin)
