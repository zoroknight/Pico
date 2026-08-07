set(PICO_SANDBOX_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(PicoSandboxModule STATIC
    Source/Private/SandboxCharacter.cpp
    Source/Private/SandboxEntity.cpp
    Source/Private/SandboxGameInstance.cpp
    Source/Private/SandboxModule.cpp
    Source/Private/SandboxPawn.cpp
    Source/Private/SandboxSession.cpp
)

target_include_directories(PicoSandboxModule
    PUBLIC
        Source/Public
)

target_compile_definitions(PicoSandboxModule
    PRIVATE
        PICO_SANDBOX_PROJECT_FILE="${PICO_SANDBOX_ROOT_DIR}/PicoSandbox.pico"
)

target_link_libraries(PicoSandboxModule
    PUBLIC
        PicoEngine
)

set_target_properties(PicoSandboxModule PROPERTIES FOLDER "Projects/PicoSandbox")

add_executable(PicoSandboxGame
    Source/Game/Main.cpp
)

target_link_libraries(PicoSandboxGame
    PRIVATE
        PicoGameRuntime
        PicoSandboxModule
)

target_compile_definitions(PicoSandboxGame
    PRIVATE
        PICO_SANDBOX_PROJECT_FILE="${PICO_SANDBOX_ROOT_DIR}/PicoSandbox.pico"
)

set_target_properties(PicoSandboxGame PROPERTIES
    FOLDER "Projects/PicoSandbox"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    VS_DPI_AWARE "PerMonitor"
)

add_executable(PicoSandboxDemo
    Source/Demo/Main.cpp
)

target_link_libraries(PicoSandboxDemo
    PRIVATE
        PicoReflectionTools
        PicoSandboxModule
)

set_target_properties(PicoSandboxDemo PROPERTIES FOLDER "Projects/PicoSandbox")

if(BUILD_TESTING)
    add_executable(PicoSandboxTests
        Tests/SandboxWorkflowTests.cpp
    )

    target_include_directories(PicoSandboxTests PRIVATE ${PICO_ROOT_DIR}/Tests/Common)
    target_link_libraries(PicoSandboxTests PRIVATE PicoSandboxModule)
    set_target_properties(PicoSandboxTests PROPERTIES FOLDER "Projects/PicoSandbox")

    add_test(NAME PicoSandboxTests COMMAND PicoSandboxTests)
    set_tests_properties(PicoSandboxTests PROPERTIES
        WORKING_DIRECTORY ${PICO_SANDBOX_ROOT_DIR}
    )
endif()

foreach(SandboxTarget PicoSandboxModule PicoSandboxGame PicoSandboxDemo PicoSandboxTests)
    if(TARGET ${SandboxTarget})
        if(MSVC)
            target_compile_options(${SandboxTarget} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${SandboxTarget} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endforeach()
