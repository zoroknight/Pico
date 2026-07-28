set(PICO_SANDBOX_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(PicoSandboxGame STATIC
    Source/Private/SandboxCharacter.cpp
    Source/Private/SandboxEntity.cpp
    Source/Private/SandboxModule.cpp
    Source/Private/SandboxSession.cpp
)

target_include_directories(PicoSandboxGame
    PUBLIC
        Source/Public
)

target_compile_definitions(PicoSandboxGame
    PRIVATE
        PICO_SANDBOX_PROJECT_FILE="${PICO_SANDBOX_ROOT_DIR}/PicoSandbox.pico"
)

target_link_libraries(PicoSandboxGame
    PUBLIC
        PicoObject
)

set_target_properties(PicoSandboxGame PROPERTIES FOLDER "Projects/PicoSandbox")

add_executable(PicoSandboxDemo
    Source/Demo/Main.cpp
)

target_link_libraries(PicoSandboxDemo
    PRIVATE
        PicoReflectionTools
        PicoSandboxGame
)

set_target_properties(PicoSandboxDemo PROPERTIES FOLDER "Projects/PicoSandbox")

if(TARGET PicoImGui)
    add_executable(PicoSandboxEditor
        Source/Editor/Main.cpp
        Source/Editor/SandboxEditorApp.cpp
        Source/Editor/SandboxEditorApp.h
    )

    target_link_libraries(PicoSandboxEditor
        PRIVATE
            PicoImGui
            PicoReflectionTools
            PicoSandboxGame
    )

    set_target_properties(PicoSandboxEditor PROPERTIES
        FOLDER "Projects/PicoSandbox"
        VS_DPI_AWARE "PerMonitor"
    )
endif()

if(BUILD_TESTING)
    add_executable(PicoSandboxTests
        Tests/SandboxWorkflowTests.cpp
    )

    target_include_directories(PicoSandboxTests PRIVATE ${PICO_ROOT_DIR}/Tests/Common)
    target_link_libraries(PicoSandboxTests PRIVATE PicoSandboxGame)
    set_target_properties(PicoSandboxTests PROPERTIES FOLDER "Projects/PicoSandbox")

    add_test(NAME PicoSandboxTests COMMAND PicoSandboxTests)
    set_tests_properties(PicoSandboxTests PROPERTIES
        WORKING_DIRECTORY ${PICO_SANDBOX_ROOT_DIR}
    )
endif()

foreach(SandboxTarget PicoSandboxGame PicoSandboxDemo PicoSandboxEditor PicoSandboxTests)
    if(TARGET ${SandboxTarget})
        if(MSVC)
            target_compile_options(${SandboxTarget} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${SandboxTarget} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endforeach()
