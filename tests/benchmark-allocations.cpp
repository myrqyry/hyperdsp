// Opt-in Linux/glibc audit shim. Scope counters to the calling audio thread.
// Direct libc entry points avoid recursion through dlsym during loader startup.
// No production module or installed package is modified by this instrument.
// Must be preloaded with initial-exec TLS (see CMake target).
#include <features.h>
#if !defined(__GLIBC__)
#error "The allocation audit requires glibc"
#endif
#include <cstddef>
#include <cerrno>
#include <cstdint>
#include <limits>
extern "C" {
void* __libc_malloc(size_t) noexcept;
void* __libc_calloc(size_t,size_t) noexcept;
void* __libc_realloc(void*,size_t) noexcept;
void* __libc_memalign(size_t,size_t) noexcept;
void __libc_free(void*) noexcept;
}
namespace {
thread_local bool active=false;
thread_local uint64_t allocations=0,releases=0;
void count(){if(active)++allocations;}
}
extern "C" void hyperdsp_bench_alloc_begin() noexcept {allocations=releases=0;active=true;}
extern "C" void hyperdsp_bench_alloc_end(uint64_t* alloc,uint64_t* release) noexcept {
    active=false;*alloc=allocations;*release=releases;
}
extern "C" void* malloc(size_t n) noexcept {count();return __libc_malloc(n);}
extern "C" void* calloc(size_t n,size_t size) noexcept {count();return __libc_calloc(n,size);}
extern "C" void* realloc(void* p,size_t n) noexcept {count();return __libc_realloc(p,n);}
extern "C" void free(void* p) noexcept {if(active&&p)++releases;__libc_free(p);}
extern "C" void* aligned_alloc(size_t alignment,size_t n) noexcept {count();return __libc_memalign(alignment,n);}
extern "C" void* memalign(size_t alignment,size_t n) noexcept {count();return __libc_memalign(alignment,n);}
extern "C" int posix_memalign(void** out,size_t alignment,size_t n) noexcept {
    count();
    if(alignment<sizeof(void*)||(alignment&(alignment-1)))return EINVAL;
    auto* p=__libc_memalign(alignment,n);
    if(!p)return ENOMEM;
    *out=p;return 0;
}
extern "C" void* reallocarray(void* p,size_t n,size_t size) noexcept {
    count();
    if(size&&n>std::numeric_limits<size_t>::max()/size){errno=ENOMEM;return nullptr;}
    return __libc_realloc(p,n*size);
}
