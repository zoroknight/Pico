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
set(PICO_GENERATED_DIR ${CMAKE_BINARY_DIR}/Generated)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")

option(PICO_BUILD_INSPECTOR "Build the Pico reflection inspector" ON)

add_library(PicoCore STATIC
    Source/Runtime/Core/Private/AssetPath.cpp
    Source/Runtime/Core/Private/App.cpp
    Source/Runtime/Core/Private/CommandLine.cpp
    Source/Runtime/Core/Private/Config.cpp
    Source/Runtime/Core/Private/Delegate.cpp
    Source/Runtime/Core/Private/GameThread.cpp
    Source/Runtime/Core/Private/Log.cpp
    Source/Runtime/Core/Private/Math/Math.cpp
    Source/Runtime/Core/Private/Name.cpp
    Source/Runtime/Core/Private/Paths.cpp
    Source/Runtime/Core/Private/PlatformProcess.cpp
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

add_library(PicoAsset STATIC
    Source/Runtime/Asset/Private/AssetManager.cpp
    Source/Runtime/Asset/Private/AssetRegistry.cpp
    Source/Runtime/Asset/Private/Material.cpp
    Source/Runtime/Asset/Private/StaticMesh.cpp
    Source/Runtime/Asset/Private/Texture.cpp
)

target_include_directories(PicoAsset
    PUBLIC
        Source/Runtime/Asset/Public
)

target_link_libraries(PicoAsset
    PUBLIC
        PicoCore
)

set_target_properties(PicoAsset PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoAsset PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoAsset PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoInput STATIC
    Source/Runtime/Input/Private/InputSystem.cpp
)

target_include_directories(PicoInput
    PUBLIC
        Source/Runtime/Input/Public
)

target_link_libraries(PicoInput PUBLIC PicoCore)
set_target_properties(PicoInput PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoInput PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoInput PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(tinyobjloader STATIC
    ThirdParty/TinyObjLoader/tiny_obj_loader.cc
)

target_include_directories(tinyobjloader PUBLIC ThirdParty/TinyObjLoader)
set_target_properties(tinyobjloader PROPERTIES FOLDER "ThirdParty")

if(MSVC)
    target_compile_options(tinyobjloader PRIVATE /W0)
else()
    target_compile_options(tinyobjloader PRIVATE -w)
endif()

add_library(PicoAssetImport STATIC
    Source/Developer/AssetImport/Private/StaticMeshImporter.cpp
    Source/Developer/AssetImport/Private/TextureImporter.cpp
)

target_include_directories(PicoAssetImport
    PUBLIC
        Source/Developer/AssetImport/Public
)

target_link_libraries(PicoAssetImport
    PUBLIC
        PicoAsset
    PRIVATE
        tinyobjloader
)

target_include_directories(PicoAssetImport PRIVATE ThirdParty/stb)

set_target_properties(PicoAssetImport PROPERTIES FOLDER "Developer")

if(MSVC)
    target_compile_options(PicoAssetImport PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoAssetImport PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoObject STATIC
    Source/Runtime/Object/Private/Archive.cpp
    Source/Runtime/Object/Private/Class.cpp
    Source/Runtime/Object/Private/ClassRegistry.cpp
    Source/Runtime/Object/Private/DynamicMulticastDelegate.cpp
    Source/Runtime/Object/Private/Function.cpp
    Source/Runtime/Object/Private/GarbageCollection.cpp
    Source/Runtime/Object/Private/Object.cpp
    Source/Runtime/Object/Private/ObjectGlobals.cpp
    Source/Runtime/Object/Private/ObjectInitializer.cpp
    Source/Runtime/Object/Private/ObjectRegistry.cpp
    Source/Runtime/Object/Private/ObjectSerialization.cpp
    Source/Runtime/Object/Private/ObjectSystem.cpp
    Source/Runtime/Object/Private/Property.cpp
    Source/Runtime/Object/Private/ReferenceCollector.cpp
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

add_executable(PicoHeaderTool
    Source/Programs/PicoHeaderTool/Private/Main.cpp
)

set_target_properties(PicoHeaderTool PROPERTIES FOLDER "Programs")

if(MSVC)
    target_compile_options(PicoHeaderTool PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoHeaderTool PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(pico_generate_reflection OutSources Header IncludePath GeneratedHeader FileId)
    set(HeaderOutput "${PICO_GENERATED_DIR}/${GeneratedHeader}")
    set(SourceOutput "${PICO_GENERATED_DIR}/${FileId}.gen.cpp")
    add_custom_command(
        OUTPUT "${HeaderOutput}" "${SourceOutput}"
        COMMAND $<TARGET_FILE:PicoHeaderTool>
            --input "${Header}"
            --header-output "${HeaderOutput}"
            --source-output "${SourceOutput}"
            --include "${IncludePath}"
            --file-id "${FileId}"
        DEPENDS PicoHeaderTool "${Header}"
        COMMENT "Generating reflection metadata for ${IncludePath}"
        VERBATIM
    )
    set_source_files_properties("${HeaderOutput}" "${SourceOutput}" PROPERTIES GENERATED TRUE)
    set(${OutSources} "${SourceOutput}" PARENT_SCOPE)
endfunction()

pico_generate_reflection(
    PICO_REFLECTION_SAMPLE_GENERATED
    "${PICO_ROOT_DIR}/Source/Samples/Reflection/Public/Pico/Samples/DemoCharacter.h"
    "Pico/Samples/DemoCharacter.h"
    "Pico/Samples/DemoCharacter.generated.h"
    "Pico_Samples_DemoCharacter_h"
)

add_library(PicoReflectionSample STATIC
    Source/Samples/Reflection/Private/DemoCharacter.cpp
    ${PICO_REFLECTION_SAMPLE_GENERATED}
)

target_include_directories(PicoReflectionSample
    PUBLIC
        Source/Samples/Reflection/Public
        ${PICO_GENERATED_DIR}
)

target_link_libraries(PicoReflectionSample
    PUBLIC
        PicoObject
)

set_target_properties(PicoReflectionSample PROPERTIES FOLDER "Pico/Samples")

add_library(PicoEngine STATIC
    Source/Runtime/Engine/Private/Actor.cpp
    Source/Runtime/Engine/Private/ActorComponent.cpp
    Source/Runtime/Engine/Private/CameraComponent.cpp
    Source/Runtime/Engine/Private/CubeComponent.cpp
    Source/Runtime/Engine/Private/DirectionalLightComponent.cpp
    Source/Runtime/Engine/Private/Controller.cpp
    Source/Runtime/Engine/Private/EngineLoop.cpp
    Source/Runtime/Engine/Private/GameInstance.cpp
    Source/Runtime/Engine/Private/GameEngine.cpp
    Source/Runtime/Engine/Private/GameModeBase.cpp
    Source/Runtime/Engine/Private/GameStateBase.cpp
    Source/Runtime/Engine/Private/Level.cpp
    Source/Runtime/Engine/Private/LightComponent.cpp
    Source/Runtime/Engine/Private/LocalPlayer.cpp
    Source/Runtime/Engine/Private/Pawn.cpp
    Source/Runtime/Engine/Private/Player.cpp
        Source/Runtime/Engine/Private/PointLightComponent.cpp
        Source/Runtime/Engine/Private/SpringArmComponent.cpp
    Source/Runtime/Engine/Private/PrimitiveComponent.cpp
    Source/Runtime/Engine/Private/PlayerController.cpp
    Source/Runtime/Engine/Private/PlayerStart.cpp
    Source/Runtime/Engine/Private/PlayerState.cpp
    Source/Runtime/Engine/Private/SceneComponent.cpp
    Source/Runtime/Engine/Private/StaticMeshComponent.cpp
    Source/Runtime/Engine/Private/TickFunction.cpp
    Source/Runtime/Engine/Private/TickTaskManager.cpp
    Source/Runtime/Engine/Private/World.cpp
    Source/Runtime/Engine/Private/WorldSerialization.cpp
)

target_include_directories(PicoEngine
    PUBLIC
        Source/Runtime/Engine/Public
)

target_link_libraries(PicoEngine
    PUBLIC
        PicoAsset
        PicoInput
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

add_executable(PicoAssetTool
    Source/Programs/PicoAssetTool/Private/Main.cpp
)

target_link_libraries(PicoAssetTool PRIVATE PicoAssetImport PicoEngine)
set_target_properties(PicoAssetTool PROPERTIES FOLDER "Programs")

if(MSVC)
    target_compile_options(PicoAssetTool PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoAssetTool PRIVATE -Wall -Wextra -Wpedantic)
endif()

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

set_target_properties(glad glfw PROPERTIES FOLDER "ThirdParty")

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

add_executable(PicoGame
    Source/Programs/PicoGame/Private/Main.cpp
)

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

add_library(PicoGameRuntime STATIC
    Source/Runtime/Launch/Private/GameLaunch.cpp
)

target_include_directories(PicoGameRuntime
    PUBLIC
        Source/Runtime/Launch/Public
)

target_link_libraries(PicoGameRuntime PUBLIC glfw PicoImGui PicoRender)
set_target_properties(PicoGameRuntime PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoGameRuntime PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoGameRuntime PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(PicoGame PRIVATE PicoGameRuntime)
target_compile_definitions(PicoGame
    PRIVATE
        PICO_DEFAULT_PROJECT_FILE="${PICO_ROOT_DIR}/Projects/PicoSandbox/PicoSandbox.pico"
)
set_target_properties(PicoGame PROPERTIES
    FOLDER "Programs"
    VS_DPI_AWARE "PerMonitor"
)

if(MSVC)
    target_compile_options(PicoGame PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoGame PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PICO_BUILD_INSPECTOR)
    add_library(PicoImGuizmo STATIC
        ThirdParty/ImGuizmo/src/ImGuizmo.cpp
    )

    target_include_directories(PicoImGuizmo
        PUBLIC
            ThirdParty/ImGuizmo/src
    )

    target_link_libraries(PicoImGuizmo
        PUBLIC
            PicoImGui
    )

    set_target_properties(PicoImGuizmo PROPERTIES FOLDER "ThirdParty")

    add_library(PicoEditorCore STATIC
        Source/Editor/PicoEditor/Private/AssetDependencyService.cpp
        Source/Editor/PicoEditor/Private/EditorAssetSelection.cpp
        Source/Editor/PicoEditor/Private/EditorAssetService.cpp
        Source/Editor/PicoEditor/Private/EditorCommandService.cpp
        Source/Editor/PicoEditor/Private/EditorPropertyService.cpp
        Source/Editor/PicoEditor/Private/EditorSceneClipboard.cpp
        Source/Editor/PicoEditor/Private/EditorSelection.cpp
        Source/Editor/PicoEditor/Private/EditorTransformService.cpp
        Source/Editor/PicoEditor/Private/EditorTransactionManager.cpp
        Source/Editor/PicoEditor/Private/EditorWorldDocument.cpp
        Source/Editor/PicoEditor/Public/Pico/Editor/EditorPropertyService.h
        Source/Editor/PicoEditor/Public/Pico/Editor/AssetDependencyService.h
    )

    target_include_directories(PicoEditorCore
        PUBLIC
            Source/Editor/PicoEditor/Public
    )

    target_link_libraries(PicoEditorCore
        PUBLIC
            PicoEngine
            PicoAssetImport
    )

    set_target_properties(PicoEditorCore PROPERTIES FOLDER "Pico")

    if(MSVC)
        target_compile_options(PicoEditorCore PRIVATE /W4 /permissive- /FS)
    else()
        target_compile_options(PicoEditorCore PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(PicoInspector
        Source/Editor/PicoInspector/Private/DynamicDelegateExperiment.cpp
        Source/Editor/PicoInspector/Private/DynamicDelegateExperiment.h
        Source/Editor/PicoInspector/Private/InspectorExperiment.h
        Source/Editor/PicoInspector/Private/GarbageCollectionExperiment.cpp
        Source/Editor/PicoInspector/Private/GarbageCollectionExperiment.h
        Source/Editor/PicoInspector/Private/InspectorApp.cpp
        Source/Editor/PicoInspector/Private/InspectorApp.h
        Source/Editor/PicoInspector/Private/Main.cpp
        Source/Editor/PicoInspector/Private/NativeDelegateExperiment.cpp
        Source/Editor/PicoInspector/Private/NativeDelegateExperiment.h
        Source/Editor/PicoInspector/Private/PropertyNotificationExperiment.cpp
        Source/Editor/PicoInspector/Private/PropertyNotificationExperiment.h
    )

    target_link_libraries(PicoInspector
        PRIVATE
            PicoImGui
            PicoReflectionTools
            PicoReflectionSample
    )

    set_target_properties(PicoInspector PROPERTIES FOLDER "Programs")

    add_executable(PicoEditor
        Source/Editor/PicoEditor/Private/AssetReferenceWidget.cpp
        Source/Editor/PicoEditor/Private/AssetReferenceWidget.h
        Source/Editor/PicoEditor/Private/ContentBrowserPanel.cpp
        Source/Editor/PicoEditor/Private/ContentBrowserPanel.h
        Source/Editor/PicoEditor/Private/DeleteAssetsDialog.cpp
        Source/Editor/PicoEditor/Private/DeleteAssetsDialog.h
        Source/Editor/PicoEditor/Private/EditorAssetWorkflowController.cpp
        Source/Editor/PicoEditor/Private/EditorAssetWorkflowController.h
        Source/Editor/PicoEditor/Private/EditorTransformGizmo.cpp
        Source/Editor/PicoEditor/Private/EditorTransformGizmo.h
        Source/Editor/PicoEditor/Private/EditorViewportPanel.cpp
        Source/Editor/PicoEditor/Private/EditorViewportPanel.h
        Source/Editor/PicoEditor/Private/MaterialDialog.cpp
        Source/Editor/PicoEditor/Private/MaterialDialog.h
        Source/Editor/PicoEditor/Private/DetailsPanel.cpp
        Source/Editor/PicoEditor/Private/DetailsPanel.h
        Source/Editor/PicoEditor/Private/SceneOutlinerPanel.cpp
        Source/Editor/PicoEditor/Private/SceneOutlinerPanel.h
        Source/Editor/PicoEditor/Private/StaticMeshImportDialog.cpp
        Source/Editor/PicoEditor/Private/StaticMeshImportDialog.h
        Source/Editor/PicoEditor/Private/Main.cpp
        Source/Editor/PicoEditor/Private/NativeFileDialog.cpp
        Source/Editor/PicoEditor/Private/NativeFileDialog.h
        Source/Editor/PicoEditor/Private/PicoEditorApp.cpp
        Source/Editor/PicoEditor/Private/PicoEditorApp.h
    )

    target_link_libraries(PicoEditor
        PRIVATE
            PicoImGui
            PicoImGuizmo
            PicoEditorCore
            PicoEngine
            PicoRender
            PicoReflectionTools
    )

    target_compile_definitions(PicoEditor
        PRIVATE
            PICO_DEFAULT_PROJECT_FILE="${PICO_ROOT_DIR}/Projects/PicoSandbox/PicoSandbox.pico"
    )

    if(WIN32)
        target_link_libraries(PicoEditor PRIVATE comdlg32)
    endif()

    set_target_properties(PicoEditor PROPERTIES
        FOLDER "Programs"
        VS_DPI_AWARE "PerMonitor"
    )

    if(MSVC)
        target_compile_options(PicoEditor PRIVATE /W4 /permissive- /FS)
    else()
        target_compile_options(PicoEditor PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(PicoEditorTests
        Tests/Editor/Private/Main.cpp
    )

    add_executable(PicoEditorAssetTests
        Tests/EditorAsset/Private/Main.cpp
    )

    target_include_directories(PicoEditorAssetTests PRIVATE Tests/Common)
    target_link_libraries(PicoEditorAssetTests PRIVATE PicoEditorCore)
    set_target_properties(PicoEditorAssetTests PROPERTIES FOLDER "Tests/Editor")

    if(MSVC)
        target_compile_options(PicoEditorAssetTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(PicoEditorAssetTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    target_include_directories(PicoEditorTests PRIVATE Tests/Common)

    target_link_libraries(PicoEditorTests
        PRIVATE
            PicoEditorCore
            PicoRender
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

add_executable(PicoAssetTests
    Tests/Asset/Private/Main.cpp
)

target_include_directories(PicoAssetTests PRIVATE Tests/Common)

target_link_libraries(PicoAssetTests
    PRIVATE
        PicoAsset
)

set_target_properties(PicoAssetTests PROPERTIES FOLDER "Tests/Asset")

if(MSVC)
    target_compile_options(PicoAssetTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoAssetTests PRIVATE -Wall -Wextra -Wpedantic)
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

pico_generate_reflection(
    PICO_GC_TEST_GENERATED
    "${PICO_ROOT_DIR}/Tests/GarbageCollection/Public/Pico/Tests/GCNode.h"
    "Pico/Tests/GCNode.h"
    "Pico/Tests/GCNode.generated.h"
    "Pico_Tests_GCNode_h"
)

add_executable(PicoGarbageCollectionTests
    Tests/GarbageCollection/Private/GCNode.cpp
    Tests/GarbageCollection/Private/Main.cpp
    ${PICO_GC_TEST_GENERATED}
)

target_include_directories(PicoGarbageCollectionTests
    PRIVATE
        Tests/Common
        Tests/GarbageCollection/Public
        ${PICO_GENERATED_DIR}
)
target_link_libraries(PicoGarbageCollectionTests PRIVATE PicoEngine)
set_target_properties(PicoGarbageCollectionTests PROPERTIES FOLDER "Tests/Object")

if(MSVC)
    target_compile_options(PicoGarbageCollectionTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoGarbageCollectionTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

pico_generate_reflection(
    PICO_DYNAMIC_DELEGATE_TEST_GENERATED
    "${PICO_ROOT_DIR}/Tests/DynamicDelegate/Public/Pico/Tests/DynamicDelegateListener.h"
    "Pico/Tests/DynamicDelegateListener.h"
    "Pico/Tests/DynamicDelegateListener.generated.h"
    "Pico_Tests_DynamicDelegateListener_h"
)

add_executable(PicoDynamicDelegateTests
    Tests/DynamicDelegate/Private/DynamicDelegateListener.cpp
    Tests/DynamicDelegate/Private/Main.cpp
    ${PICO_DYNAMIC_DELEGATE_TEST_GENERATED}
)

target_include_directories(PicoDynamicDelegateTests
    PRIVATE
        Tests/Common
        Tests/DynamicDelegate/Public
        ${PICO_GENERATED_DIR}
)
target_link_libraries(PicoDynamicDelegateTests PRIVATE PicoObject)
set_target_properties(PicoDynamicDelegateTests PROPERTIES FOLDER "Tests/Object")

if(MSVC)
    target_compile_options(PicoDynamicDelegateTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoDynamicDelegateTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
    target_compile_options(PicoObjectTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoObjectTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoAssetImportTests
    Tests/AssetImport/Private/Main.cpp
)

target_include_directories(PicoAssetImportTests PRIVATE Tests/Common)
target_link_libraries(PicoAssetImportTests PRIVATE PicoAssetImport)
set_target_properties(PicoAssetImportTests PROPERTIES FOLDER "Tests/AssetImport")

if(MSVC)
    target_compile_options(PicoAssetImportTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoAssetImportTests PRIVATE -Wall -Wextra -Wpedantic)
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

add_executable(PicoGameTests
    Tests/Game/Private/Main.cpp
)

target_include_directories(PicoGameTests PRIVATE Tests/Common)
target_link_libraries(PicoGameTests PRIVATE PicoEngine)
set_target_properties(PicoGameTests PROPERTIES FOLDER "Tests/Game")

if(MSVC)
    target_compile_options(PicoGameTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoGameTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

add_subdirectory(Projects/PicoSandbox)

if(BUILD_TESTING)
    add_test(
        NAME PicoHeaderToolTests
        COMMAND ${CMAKE_COMMAND}
            -DPHT=$<TARGET_FILE:PicoHeaderTool>
            -DFIXTURE_DIR=${PICO_ROOT_DIR}/Tests/HeaderTool/Fixtures
            -DOUTPUT_DIR=${CMAKE_BINARY_DIR}/HeaderToolTests
            -P ${PICO_ROOT_DIR}/Tests/HeaderTool/RunHeaderToolTests.cmake
    )
    add_test(NAME PicoCoreTests COMMAND PicoCoreTests)
    add_test(NAME PicoAssetTests COMMAND PicoAssetTests)
    add_test(NAME PicoAssetImportTests COMMAND PicoAssetImportTests)
    add_test(NAME PicoObjectTests COMMAND PicoObjectTests)
    add_test(NAME PicoGarbageCollectionTests COMMAND PicoGarbageCollectionTests)
    add_test(NAME PicoDynamicDelegateTests COMMAND PicoDynamicDelegateTests)
    add_test(NAME PicoEngineTests COMMAND PicoEngineTests)
    add_test(NAME PicoGameTests COMMAND PicoGameTests)
    if(PICO_BUILD_INSPECTOR)
        add_test(NAME PicoEditorTests COMMAND PicoEditorTests)
        add_test(NAME PicoEditorAssetTests COMMAND PicoEditorAssetTests)
    endif()
    set_tests_properties(PicoCoreTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoAssetTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoAssetImportTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoObjectTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoGarbageCollectionTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoDynamicDelegateTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoEngineTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoGameTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    if(PICO_BUILD_INSPECTOR)
        set_tests_properties(PicoEditorTests PROPERTIES
            WORKING_DIRECTORY ${PICO_ROOT_DIR}
        )
        set_tests_properties(PicoEditorAssetTests PROPERTIES
            WORKING_DIRECTORY ${PICO_ROOT_DIR}
        )
    endif()
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT PicoLaunch)
