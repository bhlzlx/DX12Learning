project( HelloDX12 )

set( PROJECT_SOURCE
    ${PLATFORM_SOURCE}
    ${CMAKE_CURRENT_SOURCE_DIR}/HelloDX12.cpp
)

set ( PROJECT_HEADER 
   ${CMAKE_CURRENT_SOURCE_DIR}/HelloDX12.h
)

add_executable( HelloDX12 WIN32
    ${PROJECT_SOURCE}
	${PROJECT_HEADER}
)

target_link_libraries(
    HelloDX12
	d3d12
	dxgi
	d3dcompiler
    nix
)