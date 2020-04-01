project( Samples )

##if(WIN32)
##    set( CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
##    set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
##    set( CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
##    set( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
##endif()

SET_PROPERTY(GLOBAL PROPERTY USE_FOLDERS ON)

include_directories(
	${SOLUTION_DIR}/source/Utility
	${SOLUTION_DIR}/source/thirdpart
)

set( EXECUTABLE_TYPE )

if( WIN32 )
    if( ${WINDOW_SYSTEM} STREQUAL SDL2 )
        set( PLATFORM_SOURCE
            ${SOLUTION_DIR}/source/platform/Windows/SDL2/SDL2Main.cpp
        )
    elseif( ${WINDOW_SYSTEM} STREQUAL Win32 )
        set( PLATFORM_SOURCE 
            ${SOLUTION_DIR}/source/platform/Windows/WinMain/WinMain.cpp
            ${SOLUTION_DIR}/source/platform/Windows/WinMain/WinMain.rc
        )
    endif()
    set( EXECUTABLE_TYPE WIN32 )
endif()

if( CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" )
    set( PLATFORM_SOURCE ${SOLUTION_DIR}/source/platform/Linux/LinuxMain.cpp )
endif()

if( ANDROID )
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -E")
    add_subdirectory( Android )
    SET_PROPERTY(TARGET AndroidSample PROPERTY FOLDER "Samples")
else()
    if( WIN32 )
	
		add_definitions(-DUNICODE -D_UNICODE)
	
        add_subdirectory( DX12Test )
		add_subdirectory( DX12Triangle )

        SET_PROPERTY(TARGET HelloDX12 PROPERTY FOLDER "Samples")
		SET_PROPERTY(TARGET DX12Triangle PROPERTY FOLDER "Samples")
    endif()
endif()

if(  CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" )
    add_subdirectory( ClearScreen )
endif()
