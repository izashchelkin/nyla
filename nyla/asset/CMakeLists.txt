set(TARGET_PACKER nyla_asset_packer)

if(WIN32)
    add_executable(${TARGET_PACKER} WIN32)
else()
    add_executable(${TARGET_PACKER})
endif()

add_executable(nyla::asset_packer ALIAS ${TARGET_PACKER})

target_sources(${TARGET_PACKER}
    PUBLIC
        asset.h
    PRIVATE
        asset_packer.cc
)

target_link_libraries(${TARGET_PACKER}
    PUBLIC
        nyla::platform
)