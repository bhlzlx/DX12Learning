project( DX12Triangle )

set( PROJECT_SOURCE
    ${PLATFORM_SOURCE}
    ${CMAKE_CURRENT_SOURCE_DIR}/DX12Triangle.cpp
)

set ( PROJECT_HEADER 
   ${CMAKE_CURRENT_SOURCE_DIR}/DX12Triangle.h
)

add_executable( DX12Triangle WIN32
    ${PROJECT_SOURCE}
	${PROJECT_HEADER}
)

target_link_libraries(
    DX12Triangle
	d3d12
	dxgi
	d3dcompiler
    nix
)