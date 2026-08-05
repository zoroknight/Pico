cmake_minimum_required(VERSION 3.22)

project(Pico
    VERSION 0.1.0
    DESCRIPTION "A small learning-oriented engine inspired by Unreal Engine architecture"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PICO_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(PICO_SOURCE_DIR ${PICO_ROOT_DIR}/Source)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")

option(PICO_BUILD_INSPECTOR "Build the Pico reflection inspector" ON)

add_library(PicoCore STATIC
    Source/Runtime/Core/Private/App.cpp
    Source/Runtime/Core/Private/CommandLine.cpp
    Source/Runtime/Core/Private/Config.cpp
    Source/Runtime/Core/Private/Log.cpp
    Source/Runtime/Core/Private/Math/Math.cpp
    Source/Runtime/Core/Private/Name.cpp
    Source/Runtime/Core/Private/Paths.cpp
    Source/Runtime/Core/Private/ProjectDescriptor.cpp
    Source/Runtime/Core/Private/Time.cpp
)

target_include_directories(PicoCore
    PUBLIC
        Source/Runtime/Core/Public
)

target_compile_definitions(PicoCore
    PUBLIC
        PICO_PROJECT_NAME="${PROJECT_NAME}"
        PICO_VERSION="${PROJECT_VERSION}"
)

set_target_properties(PicoCore PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoObject STATIC
    Source/Runtime/Object/Private/Archive.cpp
    Source/Runtime/Object/Private/Class.cpp
    Source/Runtime/Object/Private/ClassRegistry.cpp
    Source/Runtime/Object/Private/Object.cpp
    Source/Runtime/Object/Private/ObjectGlobals.cpp
    Source/Runtime/Object/Private/ObjectRegistry.cpp
    Source/Runtime/Object/Private/ObjectSerialization.cpp
    Source/Runtime/Object/Private/ObjectSystem.cpp
    Source/Runtime/Object/Private/Property.cpp
    Source/Runtime/Object/Private/SerializationFile.cpp
    Source/Runtime/Object/Private/SerializedProperty.cpp
)

target_include_directories(PicoObject
    PUBLIC
        Source/Runtime/Object/Public
)

target_link_libraries(PicoObject
    PUBLIC
        PicoCore
)

set_target_properties(PicoObject PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoObject PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoObject PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoReflectionTools STATIC
    Source/Developer/Reflection/Private/ReflectionDebug.cpp
)

target_include_directories(PicoReflectionTools
    PUBLIC
        Source/Developer/Reflection/Public
)

target_link_libraries(PicoReflectionTools
    PUBLIC
        PicoObject
)

set_target_properties(PicoReflectionTools PROPERTIES FOLDER "Pico/Developer")

add_library(PicoReflectionSample STATIC
    Source/Samples/Reflection/Private/DemoCharacter.cpp
)

target_include_directories(PicoReflectionSample
    PUBLIC
        Source/Samples/Reflection/Public
)

target_link_libraries(PicoReflectionSample
    PUBLIC
        PicoObject
)

set_target_properties(PicoReflectionSample PROPERTIES FOLDER "Pico/Samples")

add_library(PicoEngine STATIC
    Source/Runtime/Engine/Private/Actor.cpp
    Source/Runtime/Engine/Private/ActorComponent.cpp
    Source/Runtime/Engine/Private/CubeComponent.cpp
    Source/Runtime/Engine/Private/EngineLoop.cpp
    Source/Runtime/Engine/Private/Level.cpp
    Source/Runtime/Engine/Private/PrimitiveComponent.cpp
    Source/Runtime/Engine/Private/SceneComponent.cpp
    Source/Runtime/Engine/Private/World.cpp
    Source/Runtime/Engine/Private/WorldSerialization.cpp
)

target_include_directories(PicoEngine
    PUBLIC
        Source/Runtime/Engine/Public
)

target_link_libraries(PicoEngine
    PUBLIC
        PicoObject
)

set_target_properties(PicoEngine PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoEngine PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoLaunch
    Source/Runtime/Launch/Private/Launch.cpp
)

target_link_libraries(PicoLaunch
    PRIVATE
        PicoEngine
)

set_target_properties(PicoLaunch PROPERTIES FOLDER "Pico")

add_executable(PicoReflectionDemo
    Source/Programs/ReflectionDemo/Private/Main.cpp
)

target_link_libraries(PicoReflectionDemo
    PRIVATE
        PicoReflectionTools
        PicoReflectionSample
)

set_target_properties(PicoReflectionDemo PROPERTIES FOLDER "Programs")

if(PICO_BUILD_INSPECTOR)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(ThirdParty/GLFW EXCLUDE_FROM_ALL)

    find_package(OpenGL REQUIRED)

    add_library(glad STATIC
        ThirdParty/GLAD/src/gl.c
    )

    target_include_directories(glad
        PUBLIC
            ThirdParty/GLAD/include
    )

    set_target_properties(glad PROPERTIES FOLDER "ThirdParty")

    add_library(PicoImGui STATIC
        ThirdParty/ImGui/imgui.cpp
        ThirdParty/ImGui/imgui_draw.cpp
        ThirdParty/ImGui/imgui_tables.cpp
        ThirdParty/ImGui/imgui_widgets.cpp
        ThirdParty/ImGui/backends/imgui_impl_glfw.cpp
        ThirdParty/ImGui/backends/imgui_impl_opengl3.cpp
    )

    target_include_directories(PicoImGui
        PUBLIC
            ThirdParty/ImGui
            ThirdParty/ImGui/backends
    )

    target_link_libraries(PicoImGui
        PUBLIC
            glfw
            OpenGL::GL
    )

    set_target_properties(PicoImGui PROPERTIES FOLDER "ThirdParty")
    set_target_properties(glfw PROPERTIES FOLDER "ThirdParty")

    add_library(PicoRender STATIC
        Source/Runtime/Render/Private/SceneViewportRenderer.cpp
    )

    target_include_directories(PicoRender
        PUBLIC
            Source/Runtime/Render/Public
    )

    target_link_libraries(PicoRender
        PUBLIC
            PicoEngine
        PRIVATE
            glad
    )

    set_target_properties(PicoRender PROPERTIES FOLDER "Pico")

    if(MSVC)
        target_compile_options(PicoRender PRIVATE /W4 /permissive-)
    else()
        target_compile_options(PicoRender PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_library(PicoEditorCore STATIC
        Source/Editor/PicoEditor/Private/EditorCommandService.cpp
        Source/Editor/PicoEditor/Private/EditorSceneClipboard.cpp
        Source/Editor/PicoEditor/Private/EditorSelection.cpp
        Source/Editor/PicoEditor/Private/EditorTransactionManager.cpp
    )

    target_include_directories(PicoEditorCore
        PUBLIC
            Source/Editor/PicoEditor/Public
    )

    target_link_libraries(PicoEditorCore
        PUBLIC
            PicoEngine
    )

    set_target_properties(PicoEditorCore PROPERTIES FOLDER "Pico")

    if(MSVC)
        target_compile_options(PicoEditorCore PRIVATE /W4 /permissive-)
    else()
        target_compile_options(PicoEditorCore PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(PicoInspector
        Source/Editor/PicoInspector/Private/InspectorApp.cpp
        Source/Editor/PicoInspector/Private/InspectorApp.h
        Source/Editor/PicoInspector/Private/Main.cpp
    )

    target_link_libraries(PicoInspector
        PRIVATE
            PicoImGui
            PicoReflectionTools
            PicoReflectionSample
    )

    set_target_properties(PicoInspector PROPERTIES FOLDER "Programs")

    add_executable(PicoEditor
        Source/Editor/PicoEditor/Private/EditorViewportPanel.cpp
        Source/Editor/PicoEditor/Private/EditorViewportPanel.h
        Source/Editor/PicoEditor/Private/DetailsPanel.cpp
        Source/Editor/PicoEditor/Private/DetailsPanel.h
        Source/Editor/PicoEditor/Private/SceneOutlinerPanel.cpp
        Source/Editor/PicoEditor/Private/SceneOutlinerPanel.h
        Source/Editor/PicoEditor/Private/Main.cpp
        Source/Editor/PicoEditor/Private/PicoEditorApp.cpp
        Source/Editor/PicoEditor/Private/PicoEditorApp.h
    )

    target_link_libraries(PicoEditor
        PRIVATE
            PicoImGui
            PicoEditorCore
            PicoEngine
            PicoRender
            PicoReflectionTools
    )

    target_compile_definitions(PicoEditor
        PRIVATE
            PICO_DEFAULT_PROJECT_FILE="${PICO_ROOT_DIR}/Projects/PicoSandbox/PicoSandbox.pico"
    )

    set_target_properties(PicoEditor PROPERTIES
        FOLDER "Programs"
        VS_DPI_AWARE "PerMonitor"
    )

    if(MSVC)
        target_compile_options(PicoEditor PRIVATE /W4 /permissive-)
    else()
        target_compile_options(PicoEditor PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(PicoEditorTests
        Tests/Editor/Private/Main.cpp
    )

    target_include_directories(PicoEditorTests PRIVATE Tests/Common)

    target_link_libraries(PicoEditorTests
        PRIVATE
            PicoEditorCore
    )

    set_target_properties(PicoEditorTests PROPERTIES FOLDER "Tests/Editor")

    if(MSVC)
        target_compile_options(PicoEditorTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(PicoEditorTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

add_executable(PicoCoreTests
    Tests/Core/Private/Main.cpp
)

target_include_directories(PicoCoreTests PRIVATE Tests/Common)

target_link_libraries(PicoCoreTests
    PRIVATE
        PicoCore
)

set_target_properties(PicoCoreTests PROPERTIES FOLDER "Tests/Core")

if(MSVC)
    target_compile_options(PicoCoreTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoCoreTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoObjectTests
    Tests/Object/Private/Main.cpp
)

target_include_directories(PicoObjectTests PRIVATE Tests/Common)

target_link_libraries(PicoObjectTests
    PRIVATE
        PicoReflectionTools
        PicoObject
)

set_target_properties(PicoObjectTests PROPERTIES FOLDER "Tests/Object")

if(MSVC)
    target_compile_options(PicoObjectTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoObjectTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoEngineTests
    Tests/Engine/Private/Main.cpp
)

target_include_directories(PicoEngineTests PRIVATE Tests/Common)

target_link_libraries(PicoEngineTests
    PRIVATE
        PicoEngine
)

set_target_properties(PicoEngineTests PROPERTIES FOLDER "Tests/Engine")

if(MSVC)
    target_compile_options(PicoEngineTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoEngineTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

add_subdirectory(Projects/PicoSandbox)

if(BUILD_TESTING)
    add_test(NAME PicoCoreTests COMMAND PicoCoreTests)
    add_test(NAME PicoObjectTests COMMAND PicoObjectTests)
    add_test(NAME PicoEngineTests COMMAND PicoEngineTests)
    if(PICO_BUILD_INSPECTOR)
        add_test(NAME PicoEditorTests COMMAND PicoEditorTests)
    endif()
    set_tests_properties(PicoCoreTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoObjectTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoEngineTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    if(PICO_BUILD_INSPECTOR)
        set_tests_properties(PicoEditorTests PROPERTIES
            WORKING_DIRECTORY ${PICO_ROOT_DIR}
        )
    endif()
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT PicoLaunch)
