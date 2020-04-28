SET_PROPERTY(GLOBAL PROPERTY USE_FOLDERS ON)

if(Linux)
    set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
endif()

add_subdirectory( kwheel )

SET_PROPERTY(TARGET kwheel_base PROPERTY FOLDER "Thirdpart")
