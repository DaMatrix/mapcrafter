#ifndef SIMD_H_
#define SIMD_H_

#include <generated/config.h>

#if HAVE_ATTRIBUTE_VECTOR_SIZE
    #define HAVE_EXPLICIT_SIMD 1

    namespace mapcrafter {
    namespace simd {

    //works on both GCC and clang
    template<typename T, unsigned N>
    using vec __attribute__((vector_size(N * sizeof(T)))) = T;

    }
    }

    #if __x86_64__
        #include <immintrin.h>
    #endif
#endif

#endif //SIMD_H_
