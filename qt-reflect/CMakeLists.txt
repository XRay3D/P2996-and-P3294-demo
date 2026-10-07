# Метаобъекты Qt 6 без moc: сигналы, слоты и свойства из аннотаций.
# Собирается EDG и clang-p2996 (с его libc++ системный Qt линкуется).
#
# В составе reflection-demo подключается из корневого CMakeLists.txt.
# Отдельно:
#   cmake -S qt-reflect -B build-qt -G Ninja                        # EDG
#   cmake -S qt-reflect -B build-qt -G Ninja -DCMAKE_CXX_COMPILER=/opt/clang-p2996/bin/clang++
cmake_minimum_required(VERSION 3.21)

if(NOT PROJECT_NAME)
  if(NOT CMAKE_CXX_COMPILER AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../edg.cmake")
  endif()
  project(qt_reflect LANGUAGES CXX)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
  endif()
  if(NOT COMMAND edg_enable_reflection)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../clang-p2996.cmake")
  endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)

file(GLOB qt_reflect_examples CONFIGURE_DEPENDS examples/[0-9]*.cpp)

set(qt_reflect_targets "")
foreach(source IN LISTS qt_reflect_examples)
  get_filename_component(name "${source}" NAME_WE)
  set(target qt_${name})
  add_executable(${target} "${source}")
  set_target_properties(${target} PROPERTIES AUTOMOC OFF)
  target_include_directories(${target} PRIVATE include examples)
  target_link_libraries(${target} PRIVATE Qt6::Core)
  edg_enable_reflection(${target} INJECTION)
  if(CMAKE_CXX_COMPILER MATCHES "/eccp$")
    # 1675: «unrecognized GCC pragma» (QT_WARNING_PUSH и т.п.),
    # 111: «statement is unreachable» в заголовках Qt.
    target_compile_options(${target} PRIVATE "SHELL:--diag_suppress 1675,111")
  endif()
  list(APPEND qt_reflect_targets ${target})
endforeach()

# Эталон для сверки: настоящий moc обрабатывает только moc_reference.h.
if(TARGET qt_04_vs_moc)
  qt_wrap_cpp(moc_reference_src examples/moc_reference.h TARGET qt_04_vs_moc)
  target_sources(qt_04_vs_moc PRIVATE ${moc_reference_src})
endif()

# cmake --build build --target run_qt
# qDebug() без терминала уходит в journald, примеры печатают через printf, но
# предупреждения Qt тоже должны быть видны.
set(run_commands "")
foreach(target IN LISTS qt_reflect_targets)
  list(APPEND run_commands
    COMMAND ${CMAKE_COMMAND} -E echo "===== ${target}"
    COMMAND ${CMAKE_COMMAND} -E env QT_FORCE_STDERR_LOGGING=1 $<TARGET_FILE:${target}>
    COMMAND ${CMAKE_COMMAND} -E echo "")
endforeach()
add_custom_target(run_qt ${run_commands} DEPENDS ${qt_reflect_targets} USES_TERMINAL)
