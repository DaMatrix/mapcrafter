INCLUDE(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS -std=c++0x)
CHECK_CXX_SOURCE_COMPILES("int main() { void* p = nullptr; }" HAVE_NULLPTR)
CHECK_CXX_SOURCE_COMPILES("enum class Test { A=0, B=1, C=3 }; int main() { Test::A < Test::C; }" HAVE_ENUM_CLASS_COMPARISON)
CHECK_CXX_SOURCE_COMPILES("enum class Test; enum class Test { A, B }; int main() { Test::A == Test::B; }" HAVE_ENUM_CLASS_FORWARD_DECLARATION)

INCLUDE(CheckIncludeFiles)
CHECK_INCLUDE_FILES("sys/ioctl.h" HAVE_SYS_IOCTL_H)
CHECK_INCLUDE_FILES("unistd.h" HAVE_UNISTD_H)
CHECK_INCLUDE_FILES("syslog.h" HAVE_SYSLOG_H)

CHECK_CXX_SOURCE_COMPILES("
    template<class T, int N> using vec __attribute__((vector_size(sizeof(T) * N))) = T;
    static_assert(sizeof(vec<int, 4>) == 4 * sizeof(int), \"\");
    int main(){return 0;}" HAVE_ATTRIBUTE_VECTOR_SIZE)
if(OPT_AUTO_SIMD)
    CHECK_CXX_SOURCE_COMPILES("__attribute__((target_clones(\"default\"))) void test(){} int main(){return 0;}" HAVE_ATTRIBUTE_TARGET_CLONES)
    if(HAVE_ATTRIBUTE_TARGET_CLONES)
        CHECK_CXX_SOURCE_COMPILES("__attribute__((target_clones(\"default,sse4.2\"))) void test(){} int main(){return 0;}" HAVE_ATTRIBUTE_TARGET_CLONES_SSE42 FAIL_REGEX warning)
        CHECK_CXX_SOURCE_COMPILES("__attribute__((target_clones(\"default,avx2\"))) void test(){} int main(){return 0;}" HAVE_ATTRIBUTE_TARGET_CLONES_AVX2 FAIL_REGEX warning)
        CHECK_CXX_SOURCE_COMPILES("__attribute__((target_clones(\"default,arch=x86-64-v2\"))) void test(){} int main(){return 0;}" HAVE_ATTRIBUTE_TARGET_CLONES_X86_64_V2 FAIL_REGEX warning)
        CHECK_CXX_SOURCE_COMPILES("__attribute__((target_clones(\"default,arch=x86-64-v3\"))) void test(){} int main(){return 0;}" HAVE_ATTRIBUTE_TARGET_CLONES_X86_64_V3 FAIL_REGEX warning)
        CHECK_CXX_SOURCE_COMPILES("__attribute__((target_clones(\"default,arch=x86-64-v4\"))) void test(){} int main(){return 0;}" HAVE_ATTRIBUTE_TARGET_CLONES_X86_64_V4 FAIL_REGEX warning)

        if(HAVE_ATTRIBUTE_TARGET_CLONES_SSE42)
            string(APPEND AUTO_TARGET_CLONES ",sse4.2")
        endif()
        if(HAVE_ATTRIBUTE_TARGET_CLONES_AVX2)
            string(APPEND AUTO_TARGET_CLONES ",avx2")
        endif()
        if(HAVE_ATTRIBUTE_TARGET_CLONES_X86_64_V2)
            string(APPEND AUTO_TARGET_CLONES ",arch=x86-64-v2")
        endif()
        if(HAVE_ATTRIBUTE_TARGET_CLONES_X86_64_V3)
            string(APPEND AUTO_TARGET_CLONES ",arch=x86-64-v3")
        endif()
        if(HAVE_ATTRIBUTE_TARGET_CLONES_X86_64_V4)
            string(APPEND AUTO_TARGET_CLONES ",arch=x86-64-v4")
        endif()
    endif()

    CHECK_CXX_SOURCE_COMPILES("__attribute__((target(\"default\"))) void test(){} int main(){return 0;}" HAVE_ATTRIBUTE_TARGET)
    if(HAVE_ATTRIBUTE_TARGET)
        CHECK_CXX_SOURCE_COMPILES("
            __attribute__((target(\"default\"))) void test(){}
            __attribute__((target(\"sse4.2\"))) void test(){}
            int main(){return 0;}
        " HAVE_ATTRIBUTE_TARGET_SSE42 FAIL_REGEX warning)
        CHECK_CXX_SOURCE_COMPILES("
            __attribute__((target(\"default\"))) void test(){}
            __attribute__((target(\"avx2\"))) void test(){}
            int main(){return 0;}
        " HAVE_ATTRIBUTE_TARGET_AVX2 FAIL_REGEX warning)
    endif()
endif()

CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/config.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/config.h")

add_custom_target(version.cpp
    sh ./version.sh
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM
)
if(NOT "${CMAKE_CURRENT_SOURCE_DIR}/version.cpp")
    execute_process(
        COMMAND sh ./version.sh
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

set(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/version.cpp"
)
set(HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/util.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h"
)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/compat")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/config")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/config/configsections")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/mc")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/mc/versions")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/renderer")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/renderer/rendermodes")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/renderer/image")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/renderer/renderviews/isometricnew")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/renderer/renderviews/side")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/renderer/renderviews/topdown")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/util")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/thread")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/thread/impl")

add_library(mapcraftercore STATIC ${SOURCE})
add_dependencies(mapcraftercore version.cpp)

target_link_libraries(mapcraftercore "${Boost_IOSTREAMS_LIBRARY}" "${Boost_SYSTEM_LIBRARY}" "${Boost_FILESYSTEM_LIBRARY}")
if(OPT_USE_BOOST_THREAD)
    target_link_libraries(mapcraftercore "${Boost_THREAD_LIBRARY}")
endif()

if(OPT_LINK_DEPS_STATICALLY)
    target_link_libraries(mapcraftercore libpng.a libjpeg.a)
else()
    target_link_libraries(mapcraftercore ${PNG_LIBRARIES})
    target_link_libraries(mapcraftercore ${JPEG_LIBRARIES})
    target_link_libraries(mapcraftercore ${CMAKE_THREAD_LIBS_INIT})
endif()

if(HAVE_SPNG_LIBRARY)
    target_link_libraries(mapcraftercore spng)
endif()

if(OPT_LINK_BOOST_STATICALLY)
    if(OPT_LINK_DEPS_STATICALLY)
        target_link_libraries(mapcraftercore libz.a)
    else()
        target_link_libraries(mapcraftercore ${ZLIB_LIBRARIES})
    endif()
endif()

install(TARGETS mapcraftercore DESTINATION lib)
