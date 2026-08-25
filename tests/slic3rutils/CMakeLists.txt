get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
set(_TEST_EXTRA_SOURCES "")
if (WIN32)
    # libslic3r_gui references CBaseException (GUI_App.cpp), but its implementation
    # is compiled straight into the PhrozenOrca target rather than into a library,
    # so every other consumer has to compile it as well.
    list(APPEND _TEST_EXTRA_SOURCES
        ${PROJECT_SOURCE_DIR}/src/dev-utils/BaseException.cpp
        ${PROJECT_SOURCE_DIR}/src/dev-utils/StackWalker.cpp
        )
endif ()

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    ${_TEST_EXTRA_SOURCES}
    )

target_link_libraries(${_TEST_NAME}_tests test_common libslic3r_gui libslic3r)
if (MSVC)
    target_link_libraries(${_TEST_NAME}_tests Setupapi.lib)
    # Two copies of libjpeg-turbo reach this link: deps' jpeg-static.lib and the
    # one OpenCV bundles. libslic3r needs jpeg_mem_dest, which OpenCV's copy was
    # built without, so both jdatadst.obj end up in the link and jpeg_stdio_dest
    # is reported as multiply defined (LNK2005). Both are JPEG_LIB_VERSION 62
    # builds of the same library, so letting the first definition win is safe
    # here. /NODEFAULTLIB does not help -- it only filters default libraries,
    # not the ones named on the link line.
    target_link_options(${_TEST_NAME}_tests PRIVATE "/FORCE:MULTIPLE")
endif ()
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    orcaslicer_copy_dlls(COPY_DLLS "Debug" "d" output_dlls_Debug)
    elseif("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
    orcaslicer_copy_dlls(COPY_DLLS "RelWithDebInfo" "" output_dlls_Release)
    else()
    orcaslicer_copy_dlls(COPY_DLLS "Release" "" output_dlls_Release)
    endif()
endif()

catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
# add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests "${CATCH_EXTRA_ARGS} exclude:[NotWorking]")
