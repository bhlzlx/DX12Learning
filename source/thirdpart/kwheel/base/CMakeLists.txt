project( base )

set( FILES 
	${CMAKE_CURRENT_SOURCE_DIR}/io/archive.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/io/archive.h
	${CMAKE_CURRENT_SOURCE_DIR}/memory/buddysystemallocator.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/memory/buddysystemallocator.h
	${CMAKE_CURRENT_SOURCE_DIR}/string/path.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/string/path.h
	${CMAKE_CURRENT_SOURCE_DIR}/encrypt/base64.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/encrypt/base64.h
)

add_library( kwheel_base STATIC
	${FILES}
)