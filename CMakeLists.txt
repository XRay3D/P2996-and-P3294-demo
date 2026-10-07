cmake_minimum_required(VERSION 3.21)

# Без явного --toolchain используется EDG из edg.cmake, если компилятор не
# задан или это eccp (так его передаёт кит Qt Creator). Сборка clang-p2996:
#   cmake -B build-clang -DCMAKE_CXX_COMPILER=/opt/clang-p2996/bin/clang++
set(_edg_toolchain "${CMAKE_CURRENT_SOURCE_DIR}/edg.cmake")
if(CMAKE_CXX_COMPILER AND NOT CMAKE_CXX_COMPILER MATCHES "/eccp$")
  # edg.cmake мог остаться в кэше от прошлой настройки с другим компилятором.
  if(CMAKE_TOOLCHAIN_FILE STREQUAL _edg_toolchain)
    unset(CMAKE_TOOLCHAIN_FILE CACHE)
  endif()
elseif(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${_edg_toolchain}")
endif()

project(reflection_demo LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB demo_sources CONFIGURE_DEPENDS src/[0-9]*.cpp)

if(NOT COMMAND edg_enable_reflection)
  # clang-p2996: та же функция, что в edg.cmake, но с флагами clang.
  include(clang-p2996.cmake)
endif()

set(demo_targets "")
foreach(source IN LISTS demo_sources)
  get_filename_component(name "${source}" NAME_WE)
  add_executable(${name} "${source}")
  target_include_directories(${name} PRIVATE src)
  edg_enable_reflection(${name} INJECTION)
  list(APPEND demo_targets ${name})
endforeach()

# Qt 6 без moc (qt-reflect/): EDG и clang-p2996 с libstdc++ (см.
# clang-p2996.cmake). Системный Qt собран с libstdc++, с libc++ не линкуется.
if(DEFINED CLANG_P2996_QT_COMPATIBLE AND NOT CLANG_P2996_QT_COMPATIBLE)
  message(STATUS "qt-reflect пропущен: clang-p2996 без поддержки libstdc++")
else()
  find_package(Qt6 QUIET COMPONENTS Core)
  if(Qt6_FOUND)
    add_subdirectory(qt-reflect)
  else()
    message(STATUS "Qt6 не найден, qt-reflect пропущен")
  endif()
endif()

# cmake --build build --target run
set(run_commands "")
foreach(target IN LISTS demo_targets)
  list(
    APPEND
    run_commands
    COMMAND
    ${CMAKE_COMMAND}
    -E
    echo
    "===== ${target}"
    COMMAND
    $<TARGET_FILE:${target}>
    COMMAND
    ${CMAKE_COMMAND}
    -E
    echo
    "")
endforeach()
add_custom_target(
  run
  ${run_commands}
  DEPENDS ${demo_targets}
  USES_TERMINAL)
