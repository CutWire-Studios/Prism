find_package(Qt6 REQUIRED COMPONENTS Test)

function(switchx_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE switchx_core Qt6::Test)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

# switchx_add_test(test_recording)   # disabled: subprocess abort in headless CI
switchx_add_test(test_camerasource)
switchx_add_test(test_sources)
switchx_add_test(test_audio)
switchx_add_test(test_persistence)
switchx_add_test(test_hotkeys)
# switchx_add_test(test_videowidget) # disabled: segfault without GL/camera in headless CI
switchx_add_test(test_thumbhelper)
switchx_add_test(test_packaging)
switchx_add_test(test_remotecontrol)
switchx_add_test(test_mainwindowutils)
