find_package(Qt6 REQUIRED COMPONENTS Test)

function(prism_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE prism_core Qt6::Test)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

# prism_add_test(test_recording)   # disabled: subprocess abort in headless CI
prism_add_test(test_camerasource)
prism_add_test(test_sources)
prism_add_test(test_audio)
prism_add_test(test_persistence)
prism_add_test(test_hotkeys)
# prism_add_test(test_videowidget) # disabled: segfault without GL/camera in headless CI
prism_add_test(test_thumbhelper)
prism_add_test(test_packaging)
prism_add_test(test_remotecontrol)
prism_add_test(test_mainwindowutils)
