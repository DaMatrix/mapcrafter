#ifndef SIMD_H_
#define SIMD_H_

#include <cstring> // std::memcpy()

#ifndef __has_attribute
    #define __has_attribute(x) 0  // Compatibility with non-clang/GCC compilers.
#endif

#if __has_attribute(vector_size)
    #define MAPCRAFTER_SIMD 1

    namespace mapcrafter {
    namespace simd {

    //works on both GCC and clang
    template<typename T, unsigned N>
    using vec __attribute__((vector_size(N * sizeof(T)))) = T;

    }
    }
#endif

#if MAPCRAFTER_AUTO_SIMD && __x86_64__ && __has_attribute(target_clones)
    #define MAPCRAFTER_TARGET_CLONES __attribute__((target_clones("default,sse4.2,avx2,avx512dq")))
#else
    #define MAPCRAFTER_TARGET_CLONES
#endif

#if __x86_64__
    #include <immintrin.h>
#endif

#endif //SIMD_H_
