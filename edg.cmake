# Toolchain для компилятора EDG (eccp) с системной libstdc++.
#
# Использование:
#   cmake -B build --toolchain edg.cmake [-DEDG_PREFIX=/opt/edg]
# или include(edg.cmake) до project().
#
# eccp транслирует C++ в C и компилирует результат через gcc. Ему нужно
# окружение (EDG_BASE, CPFE, пути к заголовкам хостового g++ и т.д.), поэтому
# здесь генерируется обёртка с уже выставленным окружением, и она становится
# CMAKE_CXX_COMPILER. Сборка не требует ни source, ни экспорта переменных.
#
# После project() доступна функция:
#   edg_enable_reflection(<target> [INJECTION])
# Включает C++26, рефлексию P2996 и (с INJECTION) token injection P3294,
# подключает <experimental/meta>.

include_guard(GLOBAL)

set(EDG_PREFIX "/opt/edg" CACHE PATH "Каталог установки EDG")
set(EDG_HOST_CXX "g++" CACHE STRING
    "Хостовый C++ компилятор, чья libstdc++ используется с EDG")

# Переменные, которые нужно передать в try_compile при проверке компилятора.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES EDG_PREFIX EDG_HOST_CXX)

if(NOT EXISTS "${EDG_PREFIX}/bin/eccp")
  message(FATAL_ERROR "EDG не найден: ${EDG_PREFIX}/bin/eccp")
endif()

# Пути поиска заголовков хостового компилятора (как edg-scrape-compiler).
function(_edg_scrape_includes lang out_var)
  execute_process(
    COMMAND ${EDG_HOST_CXX} -x ${lang} -E -v -
    INPUT_FILE /dev/null
    OUTPUT_QUIET
    ERROR_VARIABLE output
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Не удалось получить пути заголовков ${EDG_HOST_CXX}")
  endif()
  string(REGEX REPLACE ".*#include <...> search starts here:\n" "" output
         "${output}")
  string(REGEX REPLACE "End of search list.*" "" output "${output}")
  string(REGEX REPLACE "\n" ";" lines "${output}")
  set(dirs "")
  foreach(line IN LISTS lines)
    string(STRIP "${line}" line)
    if(line)
      file(REAL_PATH "${line}" line)
      list(APPEND dirs "${line}")
    endif()
  endforeach()
  list(JOIN dirs ":" dirs)
  set(${out_var} "${dirs}" PARENT_SCOPE)
endfunction()

_edg_scrape_includes(c++ _edg_incl)
_edg_scrape_includes(c _edg_cincl)

# CMake не может определить ABI eccp (CMAKE_SIZEOF_VOID_P остаётся пустым),
# и find_package тогда не ищет в lib64 (например, /usr/lib64/cmake/Qt6).
# Размер указателя берётся у хостового компилятора.
execute_process(
  COMMAND ${EDG_HOST_CXX} -dM -E -x c++ -
  INPUT_FILE /dev/null
  OUTPUT_VARIABLE _edg_macros)
if(_edg_macros MATCHES "#define __SIZEOF_POINTER__ ([0-9]+)")
  set(CMAKE_SIZEOF_VOID_P ${CMAKE_MATCH_1})
endif()

# Версия в формате EDG: 15.3.1 -> 150301.
execute_process(COMMAND ${EDG_HOST_CXX} -dumpfullversion
                OUTPUT_VARIABLE _edg_ver OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REPLACE "." ";" _edg_ver "${_edg_ver}")
list(APPEND _edg_ver 0 0)
list(GET _edg_ver 0 _edg_major)
list(GET _edg_ver 1 _edg_minor)
list(GET _edg_ver 2 _edg_patch)
math(EXPR _edg_ver "${_edg_major} * 10000 + ${_edg_minor} * 100 + ${_edg_patch}")

# Обёртка кладётся рядом с кэшем основной сборки (в try_compile CMake
# передаёт уже готовый CMAKE_CXX_COMPILER, поэтому повторно не создаётся).
# «Голый» eccp (например, из набора IDE) тоже заменяется обёрткой: без её
# окружения не находятся заголовки libstdc++. Уже созданная обёртка
# перегенерируется при каждой настройке.
set(_edg_wrapper "${CMAKE_BINARY_DIR}/edg/eccp")
if(NOT CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER STREQUAL "${EDG_PREFIX}/bin/eccp"
   OR CMAKE_CXX_COMPILER STREQUAL _edg_wrapper)
  file(CONFIGURE OUTPUT "${_edg_wrapper}" @ONLY CONTENT [=[#!/bin/bash
# Сгенерировано edg.cmake. Окружение для eccp с libstdc++ @EDG_HOST_CXX@.
export EDG_USE_SYSTEM_HEADERS=1
export EDG_GCC_INCL_SCRAPE="@_edg_incl@"
export EDG_GCC_CINCL_SCRAPE="@_edg_cincl@"
export EDG_GCC_VER_SCRAPE=@_edg_ver@
export EDG_PREFIX="@EDG_PREFIX@"
. "$EDG_PREFIX/environment.sh"
eccp="$EDG_PREFIX/bin/eccp"

# CMake считает eccp компилятором GNU (EDG определяет __GNUC__) и передаёт
# -MD -MT <target> -MF <file>. eccp умеет только -M (список зависимостей без
# компиляции), поэтому зависимости пишутся отдельным вызовом.
args=() dep_args=() dep_target= dep_file=
while [ $# -gt 0 ]; do
  case "$1" in
    -MD) ;;
    # CMAKE_COLOR_DIAGNOSTICS (Qt Creator включает его) добавляет gcc-флаг,
    # который eccp не понимает.
    -fdiagnostics-color*|-fno-diagnostics-color) ;;
    # Стандарт задаёт --c++NN; -std=... приходит из compile features
    # импортированных целей (Qt6::Core требует cxx_std_17).
    -std=*) ;;
    # POSITION_INDEPENDENT_CODE (Qt требует -fPIC): eccp флаг не знает,
    # он нужен gcc, компилирующему сгенерированный C.
    -fPIC|-fpic|-fPIE|-fpie) args+=(--c_to_obj_option "$1") ;;
    # Каталоги заголовков импортированных целей (Qt6::Core и т.п.).
    -isystem)
      args+=(--sys_include "$2"); dep_args+=(--sys_include "$2"); shift ;;
    -isystem*)
      args+=(--sys_include "${1#-isystem}"); dep_args+=(--sys_include "${1#-isystem}") ;;
    -MT) dep_target=$2; shift ;;
    -MF) dep_file=$2; shift ;;
    -o) args+=("$1" "$2"); shift ;;
    -c) args+=("$1") ;;
    *) args+=("$1"); dep_args+=("$1") ;;
  esac
  shift
done

if [ -n "$dep_file" ]; then
  # Строки вида "x.o: dep" сводятся в одно правило "<target>: dep \ ...".
  "$eccp" -M "${dep_args[@]}" | {
    printf '%s: \\\n' "$dep_target"
    sed -e 's/^[^:]*: *//' -e 's/ /\\ /g' -e 's/^/  /' -e 's/$/ \\/'
    printf '\n'
  } > "$dep_file"
  [ "${PIPESTATUS[0]}" -eq 0 ] || exit 1
fi
exec "$eccp" "${args[@]}"
]=])
  file(CHMOD "${_edg_wrapper}" PERMISSIONS
       OWNER_READ OWNER_WRITE OWNER_EXECUTE
       GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  set(CMAKE_CXX_COMPILER "${_edg_wrapper}" CACHE FILEPATH "" FORCE)
endif()

# Модель инстанцирования шаблонов:
#   local - все используемые инстанцирования генерируются в каждой единице
#           трансляции как локальные; линковка без prelinker'а;
#   auto  - штатная модель EDG: prelinker на линковке назначает
#           инстанцирования объектам и перекомпилирует их.
set(EDG_INSTANTIATION "local" CACHE STRING "Модель инстанцирования EDG")
set_property(CACHE EDG_INSTANTIATION PROPERTY STRINGS local auto)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES EDG_INSTANTIATION)
if(EDG_INSTANTIATION STREQUAL "local")
  set(CMAKE_CXX_FLAGS_INIT "-tlocal --no_auto_instantiation")
  set(CMAKE_EXE_LINKER_FLAGS_INIT "--no_auto_instantiation")
  set(CMAKE_SHARED_LINKER_FLAGS_INIT "--no_auto_instantiation")
endif()

# eccp линкует через gcc, а не g++, и libm сам не добавляет (std::hypot и
# прочие функции <cmath> дают «undefined reference to hypot»).
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "-lm")

# EDG не понимает gcc-флаги -std=..., стандарт задаётся через --c++NN,
# поэтому CMAKE_CXX_STANDARD / cxx_std_NN использовать нельзя.
set(CMAKE_CXX_EXTENSIONS OFF)

function(edg_enable_reflection target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "INJECTION" "" "")

  # Только <experimental/meta>: остальные заголовки из base/include
  # (<compare> и др.) конфликтуют с libstdc++.
  set(meta_inc "${CMAKE_BINARY_DIR}/edg/include")
  if(NOT EXISTS "${meta_inc}/experimental")
    file(MAKE_DIRECTORY "${meta_inc}")
    file(CREATE_LINK "${EDG_PREFIX}/base/include/experimental"
         "${meta_inc}/experimental" SYMBOLIC)
  endif()

  target_compile_options(${target} PRIVATE
    --c++26
    --set_flag=reflection
    "$<$<BOOL:${arg_INJECTION}>:--set_flag=injection>"
    --sys_include "${meta_inc}"
    # 445: ложное «constant is not used in declaring the parameter types»
    # на лямбдах []<info r>{...} в идиоме expand(...) >> ...
    --diag_suppress 445)
endfunction()
