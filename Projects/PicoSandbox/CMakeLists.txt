set(PICO_SANDBOX_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

pico_generate_reflection(
    PICO_SANDBOX_PAWN_GENERATED
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Public/PicoSandbox/SandboxPawn.h"
    "PicoSandbox/SandboxPawn.h"
    "PicoSandbox/SandboxPawn.generated.h"
    "PicoSandbox_SandboxPawn_h"
)

pico_generate_reflection(
    PICO_SANDBOX_REPLICATION_LAB_GENERATED
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Public/PicoSandbox/SandboxReplicationLabActor.h"
    "PicoSandbox/SandboxReplicationLabActor.h"
    "PicoSandbox/SandboxReplicationLabActor.generated.h"
    "PicoSandbox_SandboxReplicationLabActor_h"
)

pico_generate_reflection(
    PICO_SANDBOX_PLAYER_CONTROLLER_GENERATED
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Public/PicoSandbox/SandboxPlayerController.h"
    "PicoSandbox/SandboxPlayerController.h"
    "PicoSandbox/SandboxPlayerController.generated.h"
    "PicoSandbox_SandboxPlayerController_h"
)

pico_generate_reflection(
    PICO_SANDBOX_PROJECTILE_GENERATED
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Public/PicoSandbox/SandboxFireballActor.h"
    "PicoSandbox/SandboxFireballActor.h"
    "PicoSandbox/SandboxFireballActor.generated.h"
    "PicoSandbox_SandboxFireballActor_h"
)

add_library(PicoSandboxModule STATIC
    Source/Private/SandboxCharacter.cpp
    Source/Private/SandboxEntity.cpp
    Source/Private/SandboxGameInstance.cpp
    Source/Private/SandboxGameMode.cpp
    Source/Private/SandboxGameplayAbilities.cpp
    Source/Private/SandboxFireballActor.cpp
    Source/Private/SandboxModule.cpp
    Source/Private/SandboxPawn.cpp
    Source/Private/SandboxPlayerController.cpp
    Source/Private/SandboxReplicationLabActor.cpp
    Source/Private/SandboxSession.cpp
    ${PICO_SANDBOX_PAWN_GENERATED}
    ${PICO_SANDBOX_REPLICATION_LAB_GENERATED}
    ${PICO_SANDBOX_PLAYER_CONTROLLER_GENERATED}
    ${PICO_SANDBOX_PROJECTILE_GENERATED}
)

target_include_directories(PicoSandboxModule
    PUBLIC
        Source/Public
        ${PICO_GENERATED_DIR}
)

target_compile_definitions(PicoSandboxModule
    PRIVATE
        PICO_SANDBOX_PROJECT_FILE="${PICO_SANDBOX_ROOT_DIR}/PicoSandbox.pico"
)

target_link_libraries(PicoSandboxModule
    PUBLIC
        PicoEngine
        PicoGameplayAbilities
)

if(TARGET PicoEditor)
    target_link_libraries(PicoEditor PRIVATE PicoSandboxModule)
    target_compile_definitions(PicoEditor PRIVATE PICO_EDITOR_WITH_SANDBOX=1)
endif()

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
    WIN32_EXECUTABLE TRUE
)

if(MSVC)
    target_link_options(PicoSandboxGame PRIVATE "/ENTRY:mainCRTStartup")
endif()

file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/$<CONFIG>/PicoSandboxGame.targetreceipt"
    CONTENT "[Target]\nName=PicoSandboxGame\nType=Game\nPlatform=Windows\nConfiguration=$<CONFIG>\nEngineVersion=${PROJECT_VERSION}\nExecutable=$<TARGET_FILE_NAME:PicoSandboxGame>\n\n[RuntimeDependencies]\n"
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
