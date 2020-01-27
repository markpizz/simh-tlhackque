## Various and sundry operating system features.
##
## Author: B. Scott Michel
## "scooter me fecit"

set(NEED_LIBRT FALSE)
set(NEED_THREADS FALSE)

add_library(os_features INTERFACE)

## semaphores and sem_timedwait support (OS feature):
check_include_file(semaphore.h semaphore_h_found)
if (semaphore_h_found)
    set(saved_cmake_required_flags ${CMAKE_REQUIRED_FLAGS})
    set(saved_cmake_required_libs  ${CMAKE_REQUIRED_LIBRARIES})

    if (TARGET Threads::Threads)
        set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
    endif (TARGET Threads::Threads)

    check_symbol_exists(sem_timedwait semaphore.h have_sem_timedwait)

    if (have_sem_timedwait)
        set(NEED_THREADS TRUE)
    else ()
        ## Maybe it's in librt, like shm_open (and more likely, it's not.)
        list(APPEND CMAKE_REQUIRED_LIBRARIES rt)
        check_symbol_exists(sem_timedwait semaphore.h have_sem_timedwait_rt)
        if (have_sem_timedwait_rt)
            set(NEED_LIBRT TRUE)
        endif (have_sem_timedwait_rt)
    endif (have_sem_timedwait)

    set(CMAKE_REQUIRED_FLAGS ${saved_cmake_required_flags})
    set(CMAKE_REQUIRED_LIBRARIES ${saved_cmake_required_libs})

    if (have_sem_timedwait OR have_sem_timedwait_rt)
        target_compile_definitions(os_features INTERFACE HAVE_SEMAPHORE)
    endif ()
endif (semaphore_h_found)

## <sys/ioctl.h>
check_include_file(sys/ioctl.h have_sys_ioctl_h)
if (have_sys_ioctl_h)
    target_compile_definitions(os_features INTERFACE HAVE_SYS_IOCTL)
endif (have_sys_ioctl_h)

## <linux/cdrom.h>
check_include_file(linux/cdrom.h have_linux_cdrom_h)
if (have_linux_cdrom_h)
    target_compile_definitions(os_features INTERFACE HAVE_LINUX_CDROM)
endif (have_linux_cdrom_h)

## <utime.h>
check_include_file(utime.h have_utime_h)
if (have_utime_h)
    target_compile_definitions(os_features INTERFACE HAVE_UTIME)
endif (have_utime_h)

## <glob.h>
check_include_file(glob.h have_glob_h)
if (have_glob_h)
    target_compile_definitions(os_features INTERFACE HAVE_GLOB)
endif (have_glob_h)

## <fnmatch.h>
check_include_file(fnmatch.h have_fnmatch_h)
if (have_fnmatch_h)
    target_compile_definitions(os_features INTERFACE HAVE_FNMATCH)
endif (have_fnmatch_h)

## <sys/mman.h> and shm_open
check_include_file(sys/mman.h have_sys_mman_h)
if (have_sys_mman_h)
    set(saved_cmake_required_flags ${CMAKE_REQUIRED_FLAGS})
    set(saved_cmake_required_libs  ${CMAKE_REQUIRED_LIBRARIES})

    check_symbol_exists(shm_open sys/mman.h have_shm_open)

    if (NOT have_shm_open OR NEED_LIBRT)
        ## Linux: shm_open is in the rt library?
        set(CMAKE_REQUIRED_LIBRARIES rt)
        check_symbol_exists(shm_open sys/mman.h have_shm_open_lrt)
    endif (NOT have_shm_open OR NEED_LIBRT)

    if (have_shm_open OR have_shm_open_lrt)
        target_compile_definitions(os_features INTERFACE HAVE_SHM_OPEN)
    endif (have_shm_open OR have_shm_open_lrt)
    if (have_shm_open_lrt)
        set(NEED_LIBRT TRUE)
    endif (have_shm_open_lrt)

    set(CMAKE_REQUIRED_FLAGS ${saved_cmake_required_flags})
    set(CMAKE_REQUIRED_LIBRARIES ${saved_cmake_required_libs})
endif (have_sys_mman_h)

IF (NEED_LIBRT)
    target_link_libraries(os_features INTERFACE rt)
ENDIF (NEED_LIBRT)

check_include_file(dlfcn.h have_dlfcn_h)
if (have_dlfcn_h)
    set(saved_cmake_required_flags ${CMAKE_REQUIRED_FLAGS})
    set(saved_cmake_required_libs  ${CMAKE_REQUIRED_LIBRARIES})

    set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_DL_LIBS})
    check_symbol_exists(dlopen dlfcn.h have_dlopen)

    if (have_dlopen)
        target_link_libraries(os_features INTERFACE ${CMAKE_DL_LIBS})

        set(dlext ${CMAKE_SHARED_LIBRARY_SUFFIX})
        string(REPLACE "." "" dlext "${dlext}")
        target_compile_definitions(os_features INTERFACE HAVE_DLOPEN=${dlext})
    endif (have_dlopen)

    set(CMAKE_REQUIRED_FLAGS ${saved_cmake_required_flags})
    set(CMAKE_REQUIRED_LIBRARIES ${saved_cmake_required_libs})
endif (have_dlfcn_h)

if (NEED_THREADS)
    target_link_libraries(os_features INTERFACE Threads::Threads)
endif (NEED_THREADS)

if (NOT MSVC)
    # Need the math library on non-Windows platforms
    target_link_libraries(os_features INTERFACE m)
endif (NOT MSVC)

