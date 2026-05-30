if (NOT WIN32)
    set(TARGET wm_test)

    add_executable(${TARGET})
    add_test(NAME wm_test COMMAND $<TARGET_FILE:wm_test>)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
    pkg_check_modules(XCB_XTEST REQUIRED IMPORTED_TARGET xcb-xtest)
    pkg_check_modules(XKB REQUIRED IMPORTED_TARGET xkbcommon)
    pkg_check_modules(XKB_X11 REQUIRED IMPORTED_TARGET xkbcommon-x11)

    target_link_libraries(${TARGET} PRIVATE
        PkgConfig::XCB
        PkgConfig::XCB_XTEST
        PkgConfig::XKB
        PkgConfig::XKB_X11
    )

    target_sources(${TARGET} PRIVATE
        test_wm.cc
    )
endif()
