# Рефлексия на clang-p2996 (Bloomberg, https://github.com/bloomberg/clang-p2996,
# ветка libstdcxx-reflection форка XRay3D с поддержкой libstdc++).
#
# Подключается после project(), когда CMAKE_CXX_COMPILER - clang++ сборки
# clang-p2996:
#   cmake -B build-clang -DCMAKE_CXX_COMPILER=/opt/clang-p2996/bin/clang++
#
# Даёт ту же функцию, что edg.cmake:
#   edg_enable_reflection(<target> [INJECTION])
# C++26, -freflection-latest, с INJECTION -ftoken-injection (P3294).
#
# Стандартная библиотека. С libstdc++ (GCC >= 16, выбирается через
# <triple>.cfg рядом с clang++) <experimental/meta> берётся из ресурсного
# каталога clang (include/libstdcxx_wrappers), и с ней линкуется системный
# Qt. Если clang-p2996 собран без этой поддержки, <experimental/meta> есть
# только в его libc++: тогда берётся -stdlib=libc++, а Qt недоступен
# (в ABI Qt6::Core есть std::chrono и std::function).

include_guard(GLOBAL)

include(CheckIncludeFileCXX)
# Результат проверки не кэшируется между настройками: clang-p2996 могли
# переустановить (как раз добавив поддержку libstdc++), а Qt Creator
# перенастраивает проект с тем же CMakeCache.txt.
unset(CLANG_P2996_LIBSTDCXX_META CACHE)
set(CMAKE_REQUIRED_FLAGS "-std=c++26 -freflection-latest")
check_include_file_cxx("experimental/meta" CLANG_P2996_LIBSTDCXX_META)
unset(CMAKE_REQUIRED_FLAGS)

if(CLANG_P2996_LIBSTDCXX_META)
  set(CLANG_P2996_STDLIB "libstdc++")
  set(CLANG_P2996_QT_COMPATIBLE TRUE)
else()
  set(CLANG_P2996_STDLIB "libc++")
  set(CLANG_P2996_QT_COMPATIBLE FALSE)
  message(STATUS "clang-p2996: <experimental/meta> нет в libstdc++, "
                 "используется libc++ (Qt не собирается)")
endif()

get_filename_component(_clang_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
file(GLOB _libcxx_dir "${_clang_dir}/../lib/*-linux*")

function(edg_enable_reflection target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "INJECTION" "" "")
  target_compile_options(${target} PRIVATE
    -std=c++26 -freflection-latest
    "$<$<BOOL:${arg_INJECTION}>:-ftoken-injection>")
  if(CLANG_P2996_STDLIB STREQUAL "libc++")
    target_compile_options(${target} PRIVATE -stdlib=libc++)
    target_link_options(${target} PRIVATE -stdlib=libc++)
    if(_libcxx_dir)
      set_target_properties(${target} PROPERTIES BUILD_RPATH "${_libcxx_dir}")
    endif()
  endif()
endfunction()
