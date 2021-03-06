cmake_minimum_required(VERSION 3.4)

project( kwheel )

if( ANDROID )
    # android platform
    set( TARGET_ARCH ${CMAKE_ANDROID_ARCH_ABI} )
    if( CMAKE_CXX_COMPILER_ID STREQUAL "Clang" )
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fdeclspec -g -Wall -Wunused-function -pthread")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fdeclspec -pthread")
        add_link_options("-Wl,--build-id=sha1")
        set(TARGET_ARCH ${CMAKE_ANDROID_ARCH_ABI} )
    else()
        message("why not clang!?!?")
    endif()
    # add sdl2 library for Android
    include_directories( ${CMAKE_CURRENT_SOURCE_DIR}/SDL2/include )
    # add basic NDK native library
    find_library( log log )
    find_library( android android )
elseif( CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fdeclspec -g -Wall -Wunused-function -pthread")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fdeclspec -pthread")
elseif( APPLE )
    if( IOS )
    # iOS platform
    set( TARGET_ARCH iOS )
    else()
    # macOS platform
    set( TARGET_ARCH macOS )
    endif()
	# set( CMAKE_FIND_LIBRARY_PREFIXES lib )
	# set( CMAKE_FIND_LIBRARY_SUFFIXES a dylib )
elseif( WIN32 )
    # windows platform
    if( DEFINED TARGET_ARCH )
        if( ${TARGET_ARCH} STREQUAL x86 )
        elseif( ${TARGET_ARCH} STREQUAL x64 )
        else()
            set( TARGET_ARCH x64 )
            message("Invalid Win32 Architechture, set architecture to 'x64'...")
        endif()
    else()
        set( TARGET_ARCH x64 )
        message("Architechture was not set, set architecture to 'x64'...")
    endif()
    # MingW Compilers
    if( MINGW )
    if( CMAKE_BUILD_TYPE STREQUAL "Debug" )
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g -Wall -Wunused-function -pthread")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c++11 -g -pthread")
    elseif( CMAKE_BUILD_TYPE STREQUAL "Release" )
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -pthread -Wunused-function")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -pthread")
    endif()                 
    #
    if( TARGET_ARCH STREQUAL x64 )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m64")
    elseif(TARGET_ARCH STREQUAL x86)
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
    endif()

    endif()
    # Microsoft Visual C++ Compilers
    if(MSVC)
    if( CMAKE_BUILD_TYPE STREQUAL "Debug" )
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  /fp:fast /Gy /Oi /Oy /O2 /Ot /Zi /EHsc ")
    endif()
    ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
    IF(CMAKE_CL_64)
        set( TARGET_ARCH "x64" )
    ELSE(CMAKE_CL_64)
        set( TARGET_ARCH "x86" )
    ENDIF(CMAKE_CL_64)
    endif()
endif()

message( "target platform : ${CMAKE_SYSTEM_NAME}")

set( SOLUTION_DIR ${CMAKE_CURRENT_SOURCE_DIR} )

SET( EXECUTABLE_OUTPUT_PATH 
    ${SOLUTION_DIR}/bin/${CMAKE_SYSTEM_NAME}/${TARGET_ARCH}
)
SET( LIBRARY_OUTPUT_PATH 
    ${SOLUTION_DIR}/bin/${CMAKE_SYSTEM_NAME}/${TARGET_ARCH}
)
SET( CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG 
    ${SOLUTION_DIR}/bin/${CMAKE_SYSTEM_NAME}/${TARGET_ARCH}
)

link_directories(
    ${SOLUTION_DIR}/lib/${CMAKE_SYSTEM_NAME}/${TARGET_ARCH}
)

if( MSVC )
    set( THIRD_PART_LIB_DIR ${SOLUTION_DIR}/lib/${CMAKE_SYSTEM_NAME}/${TARGET_ARCH} )
endif()

add_subdirectory(base)