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

# Third-party subprojects must not leak a Debug postfix into Pico or Jolt targets.
set(CMAKE_DEBUG_POSTFIX "" CACHE STRING "Debug library postfix" FORCE)

include(FetchContent)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    jolt
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG e77f175595e64cb44218cc9d9d56fc365ad0e36a
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build
)
FetchContent_MakeAvailable(jolt)

# Keep the local MCP listener on a maintained HTTP implementation. Pico owns
# the MCP binding and security policy, not an ad-hoc HTTP parser.
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    # v0.51.0, pinned to an immutable commit for reproducible builds.
    GIT_TAG d66d9a95997d51a8ba9822a611d1267757741535
)
FetchContent_MakeAvailable(cpp_httplib)

option(PICO_FETCH_ASSIMP "Download Assimp for glTF/FBX skeletal animation import" OFF)
set(PICO_ASSIMP_SOURCE_DIR
    "${PICO_ROOT_DIR}/ThirdParty/Assimp"
    CACHE PATH
    "Path to a local Assimp source tree")

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)

if(EXISTS "${PICO_ASSIMP_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "Using local Assimp source: ${PICO_ASSIMP_SOURCE_DIR}")
    set(_PICO_SAVED_CMAKE_DEBUG_POSTFIX "${CMAKE_DEBUG_POSTFIX}")
    add_subdirectory(
        "${PICO_ASSIMP_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/ThirdParty/Assimp"
        EXCLUDE_FROM_ALL)
    set(CMAKE_DEBUG_POSTFIX
        "${_PICO_SAVED_CMAKE_DEBUG_POSTFIX}"
        CACHE STRING
        "Debug library postfix"
        FORCE)
    unset(_PICO_SAVED_CMAKE_DEBUG_POSTFIX)
elseif(PICO_FETCH_ASSIMP)
    FetchContent_Declare(
        assimp
        URL https://github.com/assimp/assimp/archive/refs/tags/v6.0.4.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(assimp)
else()
    find_package(assimp CONFIG QUIET)
endif()

add_library(PicoCore STATIC
    Source/Runtime/Core/Private/AssetPath.cpp
    Source/Runtime/Core/Private/App.cpp
    Source/Runtime/Core/Private/CommandLine.cpp
    Source/Runtime/Core/Private/Config.cpp
    Source/Runtime/Core/Private/Delegate.cpp
    Source/Runtime/Core/Private/GameThread.cpp
    Source/Runtime/Core/Private/Log.cpp
    Source/Runtime/Core/Private/Math/Math.cpp
    Source/Runtime/Core/Private/MemoryTracker.cpp
    Source/Runtime/Core/Private/Name.cpp
    Source/Runtime/Core/Private/Paths.cpp
    Source/Runtime/Core/Private/PlatformProcess.cpp
    Source/Runtime/Core/Private/PlatformTextInput.cpp
    Source/Runtime/Core/Private/Profiler.cpp
    Source/Runtime/Core/Private/ProjectDescriptor.cpp
    Source/Runtime/Core/Private/Time.cpp
)

find_package(Threads REQUIRED)

add_library(PicoTasks STATIC
    Source/Runtime/Tasks/Private/GameThreadDispatcher.cpp
    Source/Runtime/Tasks/Private/TaskSystem.cpp
)

target_include_directories(PicoTasks
    PUBLIC
        Source/Runtime/Tasks/Public
)

target_link_libraries(PicoTasks
    PUBLIC
        PicoCore
        Threads::Threads
)

set_target_properties(PicoTasks PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoTasks PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoTasks PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoAgentCore STATIC
    Source/Developer/Agent/Private/AgentCredentialStore.cpp
    Source/Developer/Agent/Private/AgentEvaluation.cpp
    Source/Developer/Agent/Private/AgentIntent.cpp
    Source/Developer/Agent/Private/AgentKnowledgeStore.cpp
    Source/Developer/Agent/Private/AgentMetrics.cpp
    Source/Developer/Agent/Private/AgentOperationJournal.cpp
    Source/Developer/Agent/Private/AgentProjectHandoff.cpp
    Source/Developer/Agent/Private/AgentProvider.cpp
    Source/Developer/Agent/Private/AgentRuntime.cpp
    Source/Developer/Agent/Private/AgentSession.cpp
    Source/Developer/Agent/Private/AgentSkill.cpp
    Source/Developer/Agent/Private/AgentToolRegistry.cpp
    Source/Developer/Agent/Private/AgentTypes.cpp
    Source/Developer/Agent/Private/FakeAgentProvider.cpp
    Source/Developer/Agent/Private/OpenAICompatibleProvider.cpp
    Source/Developer/Agent/Private/PlatformAgentHttpTransport.cpp
)

target_include_directories(PicoAgentCore
    PUBLIC
        Source/Developer/Agent/Public
    PRIVATE
        ThirdParty/NlohmannJson/single_include
)

target_link_libraries(PicoAgentCore PUBLIC PicoTasks)
if(WIN32)
    target_link_libraries(PicoAgentCore PRIVATE winhttp)
endif()
set_target_properties(PicoAgentCore PROPERTIES FOLDER "Pico/Developer")

if(MSVC)
    target_compile_options(PicoAgentCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoAgentCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoMcpCore STATIC
    Source/Developer/Mcp/Private/McpServerCore.cpp
)

target_include_directories(PicoMcpCore
    PUBLIC
        Source/Developer/Mcp/Public
    PRIVATE
        ThirdParty/NlohmannJson/single_include
)

target_link_libraries(PicoMcpCore PUBLIC PicoCore Threads::Threads)
set_target_properties(PicoMcpCore PROPERTIES FOLDER "Pico/Developer")

if(MSVC)
    target_compile_options(PicoMcpCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoMcpCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoMcpAgentAdapter STATIC
    Source/Developer/McpAdapter/Private/McpAgentToolsetAdapter.cpp
)

target_include_directories(PicoMcpAgentAdapter
    PUBLIC
        Source/Developer/McpAdapter/Public
    PRIVATE
        ThirdParty/NlohmannJson/single_include
)

target_link_libraries(PicoMcpAgentAdapter
    PUBLIC
        PicoMcpCore
        PicoAgentCore
)
set_target_properties(PicoMcpAgentAdapter PROPERTIES FOLDER "Pico/Developer")

if(MSVC)
    target_compile_options(PicoMcpAgentAdapter PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoMcpAgentAdapter PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoMcpHttp STATIC
    Source/Developer/McpHttp/Private/McpHttpServer.cpp
)

target_include_directories(PicoMcpHttp
    PUBLIC
        Source/Developer/McpHttp/Public
    PRIVATE
        ThirdParty/NlohmannJson/single_include
)

target_link_libraries(PicoMcpHttp
    PUBLIC
        PicoMcpCore
    PRIVATE
        httplib::httplib
)
set_target_properties(PicoMcpHttp PROPERTIES FOLDER "Pico/Developer")

if(MSVC)
    target_compile_options(PicoMcpHttp PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoMcpHttp PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoPackaging STATIC
    Source/Developer/Packaging/Private/Packaging.cpp
)

target_include_directories(PicoPackaging
    PUBLIC
        Source/Developer/Packaging/Public
)

target_link_libraries(PicoPackaging PUBLIC PicoCore)
set_target_properties(PicoPackaging PROPERTIES FOLDER "Pico/Developer")

if(MSVC)
    target_compile_options(PicoPackaging PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoPackaging PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_include_directories(PicoCore
    PUBLIC
        Source/Runtime/Core/Public
)

target_compile_definitions(PicoCore
    PUBLIC
        PICO_PROJECT_NAME="${PROJECT_NAME}"
        PICO_VERSION="${PROJECT_VERSION}"
)

if(WIN32)
    target_link_libraries(PicoCore PRIVATE imm32)
endif()

set_target_properties(PicoCore PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoNetCore STATIC
    Source/Runtime/NetCore/Private/LoopbackTransport.cpp
    Source/Runtime/NetCore/Private/NetConnection.cpp
    Source/Runtime/NetCore/Private/NetPacket.cpp
    Source/Runtime/NetCore/Private/NetTypes.cpp
    Source/Runtime/NetCore/Private/UdpTransport.cpp
)

target_include_directories(PicoNetCore
    PUBLIC
        Source/Runtime/NetCore/Public
)

target_link_libraries(PicoNetCore PUBLIC PicoCore)
if(WIN32)
    target_link_libraries(PicoNetCore PRIVATE ws2_32)
endif()
set_target_properties(PicoNetCore PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoNetCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoNetCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoAsset STATIC
    Source/Runtime/Asset/Private/CharacterProfile.cpp
    Source/Runtime/Asset/Private/ThirdPersonControlProfile.cpp
    Source/Runtime/Asset/Private/AssetManager.cpp
    Source/Runtime/Asset/Private/AssetRegistry.cpp
    Source/Runtime/Asset/Private/Material.cpp
    Source/Runtime/Asset/Private/SkeletalAnimation.cpp
    Source/Runtime/Asset/Private/StaticMesh.cpp
    Source/Runtime/Asset/Private/Texture.cpp
)

add_library(PicoGraph STATIC
    Source/Runtime/Graph/Private/GraphAsset.cpp
    Source/Runtime/Graph/Private/GraphCompiler.cpp
    Source/Runtime/Graph/Private/ScriptRuntimeExtensions.cpp
    Source/Runtime/Graph/Private/ScriptVM.cpp
)

target_include_directories(PicoGraph
    PUBLIC
        Source/Runtime/Graph/Public
    PRIVATE
        ThirdParty/NlohmannJson/single_include
)

target_link_libraries(PicoGraph PUBLIC PicoCore PicoObject)
target_link_libraries(PicoPackaging PUBLIC PicoGraph)
set_target_properties(PicoGraph PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoGraph PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoGraph PRIVATE -Wall -Wextra -Wpedantic)
endif()

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

set(PICO_ASSET_IMPORT_SOURCES
    Source/Developer/AssetImport/Private/StaticMeshImporter.cpp
    Source/Developer/AssetImport/Private/TextureImporter.cpp
)

if(TARGET assimp::assimp)
    list(APPEND PICO_ASSET_IMPORT_SOURCES
        Source/Developer/AssetImport/Private/SkeletalAnimationImporter.cpp)
else()
    list(APPEND PICO_ASSET_IMPORT_SOURCES
        Source/Developer/AssetImport/Private/SkeletalAnimationImporterStub.cpp)
endif()

add_library(PicoAssetImport STATIC ${PICO_ASSET_IMPORT_SOURCES})

target_include_directories(PicoAssetImport
    PUBLIC
        Source/Developer/AssetImport/Public
)

target_link_libraries(PicoAssetImport
    PUBLIC
        PicoAsset
    PRIVATE tinyobjloader
)

if(TARGET assimp::assimp)
    target_link_libraries(PicoAssetImport PRIVATE assimp::assimp)
    target_compile_definitions(PicoAssetImport PRIVATE PICO_WITH_ASSIMP=1)
endif()

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

add_library(PicoPhysicsCore STATIC
    Source/Runtime/PhysicsCore/Private/CollisionTypes.cpp
    Source/Runtime/PhysicsCore/Private/WorldCollisionQuery.cpp
)

target_include_directories(PicoPhysicsCore
    PUBLIC
        Source/Runtime/PhysicsCore/Public
)

target_link_libraries(PicoPhysicsCore PUBLIC PicoObject)
set_target_properties(PicoPhysicsCore PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoPhysicsCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoPhysicsCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoPhysicsJolt STATIC
    Source/Runtime/PhysicsJolt/Private/JoltPhysicsScene.cpp
)

target_include_directories(PicoPhysicsJolt
    PUBLIC
        Source/Runtime/PhysicsJolt/Public
)

target_link_libraries(PicoPhysicsJolt
    PUBLIC PicoPhysicsCore
    PRIVATE Jolt
)
set_target_properties(PicoPhysicsJolt PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoPhysicsJolt PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoPhysicsJolt PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoEngine STATIC
    Source/Runtime/Engine/Private/ActorBlueprint.cpp
    Source/Runtime/Engine/Private/AnimInstance.cpp
    Source/Runtime/Engine/Private/Actor.cpp
    Source/Runtime/Engine/Private/ActorComponent.cpp
    Source/Runtime/Engine/Private/CameraComponent.cpp
    Source/Runtime/Engine/Private/CameraActor.cpp
    Source/Runtime/Engine/Private/CapsuleComponent.cpp
    Source/Runtime/Engine/Private/Character.cpp
    Source/Runtime/Engine/Private/CharacterMovementComponent.cpp
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
    Source/Runtime/Engine/Private/NetPlayer.cpp
    Source/Runtime/Engine/Private/MovementComponent.cpp
    Source/Runtime/Engine/Private/NetDriver.cpp
    Source/Runtime/Engine/Private/Pawn.cpp
    Source/Runtime/Engine/Private/PawnMovementComponent.cpp
    Source/Runtime/Engine/Private/FloatingPawnMovement.cpp
    Source/Runtime/Engine/Private/Player.cpp
        Source/Runtime/Engine/Private/PointLightComponent.cpp
        Source/Runtime/Engine/Private/SpringArmComponent.cpp
    Source/Runtime/Engine/Private/PrimitiveComponent.cpp
    Source/Runtime/Engine/Private/Replication.cpp
    Source/Runtime/Engine/Private/PlayerController.cpp
    Source/Runtime/Engine/Private/PlayerStart.cpp
    Source/Runtime/Engine/Private/PlayerState.cpp
    Source/Runtime/Engine/Private/SceneComponent.cpp
    Source/Runtime/Engine/Private/ScriptComponent.cpp
    Source/Runtime/Engine/Private/SkeletalMeshComponent.cpp
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
        PicoGraph
        PicoNetCore
        PicoObject
        PicoPhysicsCore
        PicoPhysicsJolt
)

set_target_properties(PicoEngine PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoEngine PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(PicoGameplayAbilities STATIC
    Source/Runtime/GameplayAbilities/Private/AbilityTask.cpp
    Source/Runtime/GameplayAbilities/Private/AbilitySystemComponent.cpp
    Source/Runtime/GameplayAbilities/Private/GameplayAbilityPrediction.cpp
    Source/Runtime/GameplayAbilities/Private/AttributeSet.cpp
    Source/Runtime/GameplayAbilities/Private/GameplayAbility.cpp
    Source/Runtime/GameplayAbilities/Private/GameplayAbilitiesModule.cpp
    Source/Runtime/GameplayAbilities/Private/GameplayEffect.cpp
    Source/Runtime/GameplayAbilities/Private/GameplayTag.cpp
)

target_include_directories(PicoGameplayAbilities
    PUBLIC
        Source/Runtime/GameplayAbilities/Public
)

target_link_libraries(PicoGameplayAbilities PUBLIC PicoEngine)
set_target_properties(PicoGameplayAbilities PROPERTIES FOLDER "Pico")

if(MSVC)
    target_compile_options(PicoGameplayAbilities PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoGameplayAbilities PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoLaunch
    Source/Runtime/Launch/Private/Launch.cpp
)

add_executable(PicoAssetTool
    Source/Programs/PicoAssetTool/Private/Main.cpp
)

add_executable(PicoPackager
    Source/Programs/PicoPackager/Private/Main.cpp
)

add_executable(PicoAgentHost
    Source/Programs/PicoAgentHost/Private/Main.cpp
)

add_executable(PicoRuntimeBenchmarks
    Source/Programs/PicoRuntimeBenchmarks/Private/Main.cpp
)
target_link_libraries(PicoRuntimeBenchmarks PRIVATE PicoEngine)
target_include_directories(PicoRuntimeBenchmarks PRIVATE
    ThirdParty/NlohmannJson/single_include)
target_compile_definitions(PicoRuntimeBenchmarks PRIVATE
    PICO_BENCHMARK_BUILD_CONFIG="$<CONFIG>")
set_target_properties(PicoRuntimeBenchmarks PROPERTIES FOLDER "Programs")
if(MSVC)
    target_compile_options(PicoRuntimeBenchmarks PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoRuntimeBenchmarks PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_include_directories(PicoAgentHost PRIVATE ThirdParty/NlohmannJson/single_include)
target_link_libraries(PicoAgentHost PRIVATE PicoAgentCore)
set_target_properties(PicoAgentHost PROPERTIES FOLDER "Programs")

if(MSVC)
    target_compile_options(PicoAgentHost PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoAgentHost PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(PicoPackager PRIVATE PicoPackaging)
set_target_properties(PicoPackager PROPERTIES FOLDER "Programs")

if(MSVC)
    target_compile_options(PicoPackager PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoPackager PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
    Source/Runtime/Render/Private/EditorIconImageLoader.cpp
    Source/Runtime/Render/Private/SceneViewportRenderer.cpp
)

target_include_directories(PicoRender
    PUBLIC
        Source/Runtime/Render/Public
    PRIVATE
        ThirdParty/stb
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
    WIN32_EXECUTABLE TRUE
)

if(MSVC)
    target_link_options(PicoGame PRIVATE "/ENTRY:mainCRTStartup")
endif()

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

    add_library(PicoMd4c STATIC
        ThirdParty/MD4C/md4c.c
    )
    target_include_directories(PicoMd4c PUBLIC ThirdParty/MD4C)
    set_target_properties(PicoMd4c PROPERTIES FOLDER "ThirdParty")

    add_library(PicoEditorCore STATIC
        Source/Editor/PicoEditor/Private/AssetDependencyService.cpp
        Source/Editor/PicoEditor/Private/EditorAgentExecutionService.cpp
        Source/Editor/PicoEditor/Private/EditorAgentTools.cpp
        Source/Editor/PicoEditor/Private/EditorAssetSelection.cpp
        Source/Editor/PicoEditor/Private/EditorAssetService.cpp
        Source/Editor/PicoEditor/Private/EditorCommandService.cpp
        Source/Editor/PicoEditor/Private/EditorPropertyService.cpp
        Source/Editor/PicoEditor/Private/PlaySession.cpp
        Source/Editor/PicoEditor/Private/EditorProjectManager.cpp
        Source/Editor/PicoEditor/Private/EditorRuntimeFreshness.cpp
        Source/Editor/PicoEditor/Private/EditorSceneClipboard.cpp
        Source/Editor/PicoEditor/Private/EditorSelection.cpp
        Source/Editor/PicoEditor/Private/EditorTransformService.cpp
        Source/Editor/PicoEditor/Private/EditorTransactionManager.cpp
        Source/Editor/PicoEditor/Private/EditorWorldDocument.cpp
        Source/Editor/PicoEditor/Public/Pico/Editor/EditorPropertyService.h
        Source/Editor/PicoEditor/Public/Pico/Editor/EditorProjectManager.h
        Source/Editor/PicoEditor/Public/Pico/Editor/EditorRuntimeFreshness.h
        Source/Editor/PicoEditor/Public/Pico/Editor/PlaySession.h
        Source/Editor/PicoEditor/Public/Pico/Editor/AssetDependencyService.h
        Source/Editor/PicoEditor/Public/Pico/Editor/EditorAgentExecutionService.h
    )

    target_include_directories(PicoEditorCore
        PUBLIC
            Source/Editor/PicoEditor/Public
    )

    target_link_libraries(PicoEditorCore
        PUBLIC
            PicoEngine
            PicoAssetImport
            PicoAgentCore
            PicoGameplayAbilities
            PicoMcpAgentAdapter
    )

    target_include_directories(PicoEditorCore PRIVATE ThirdParty/NlohmannJson/single_include)

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
        Source/Editor/PicoInspector/Private/GameplayAbilitiesExperiment.cpp
        Source/Editor/PicoInspector/Private/GameplayAbilitiesExperiment.h
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
            PicoGameplayAbilities
    )

    set_target_properties(PicoInspector PROPERTIES FOLDER "Programs")

    add_executable(PicoEditor
        Source/Editor/PicoEditor/Private/AgentChatWorkspace.cpp
        Source/Editor/PicoEditor/Private/AgentChatWorkspace.h
        Source/Editor/PicoEditor/Private/MarkdownRenderer.cpp
        Source/Editor/PicoEditor/Private/MarkdownRenderer.h
        Source/Editor/PicoEditor/Private/ActorBlueprintEditor.cpp
        Source/Editor/PicoEditor/Private/ActorBlueprintEditor.h
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
        Source/Editor/PicoEditor/Private/PicoGraphEditor.cpp
        Source/Editor/PicoEditor/Private/PicoGraphEditor.h
        Source/Editor/PicoEditor/Private/DetailsPanel.cpp
        Source/Editor/PicoEditor/Private/DetailsPanel.h
        Source/Editor/PicoEditor/Private/SceneOutlinerPanel.cpp
        Source/Editor/PicoEditor/Private/SceneOutlinerPanel.h
        Source/Editor/PicoEditor/Private/SkeletalAssetEditor.cpp
        Source/Editor/PicoEditor/Private/SkeletalAssetEditor.h
        Source/Editor/PicoEditor/Private/StaticMeshImportDialog.cpp
        Source/Editor/PicoEditor/Private/StaticMeshImportDialog.h
        Source/Editor/PicoEditor/Private/Main.cpp
        Source/Editor/PicoEditor/Private/NativeFileDialog.cpp
        Source/Editor/PicoEditor/Private/NativeFileDialog.h
        Source/Editor/PicoEditor/Private/PicoEditorApp.cpp
        Source/Editor/PicoEditor/Private/PicoEditorApp.h
    )

    target_include_directories(PicoEditor PRIVATE ThirdParty/NlohmannJson/single_include)

    add_dependencies(PicoEditor PicoPackager)

    target_link_libraries(PicoEditor
        PRIVATE
            PicoImGui
            PicoImGuizmo
            PicoMd4c
            PicoEditorCore
            PicoMcpHttp
            PicoEngine
            PicoRender
            PicoTasks
            PicoReflectionTools
            PicoAssetImport
            PicoGraph
    )

    if(WIN32)
        target_link_libraries(PicoEditor PRIVATE comdlg32 ole32 shell32)
        set_property(TARGET PicoEditor PROPERTY WIN32_EXECUTABLE TRUE)
    endif()

    set_target_properties(PicoEditor PROPERTIES
        FOLDER "Programs"
        VS_DPI_AWARE "PerMonitor"
    )

    if(MSVC)
        target_compile_options(PicoEditor PRIVATE /W4 /permissive- /FS /utf-8)
        target_link_options(PicoEditor PRIVATE "/ENTRY:mainCRTStartup")
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
            PicoMcpHttp
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

add_executable(PicoTaskTests
    Tests/Tasks/Private/Main.cpp
)

target_include_directories(PicoTaskTests PRIVATE Tests/Common)
target_link_libraries(PicoTaskTests PRIVATE PicoTasks)
set_target_properties(PicoTaskTests PROPERTIES FOLDER "Tests/Core")

if(MSVC)
    target_compile_options(PicoTaskTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoTaskTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoAgentTests
    Tests/Agent/Private/Main.cpp
)

target_include_directories(PicoAgentTests PRIVATE Tests/Common)
target_link_libraries(PicoAgentTests PRIVATE PicoAgentCore)
set_target_properties(PicoAgentTests PROPERTIES FOLDER "Tests/Developer")

if(MSVC)
    target_compile_options(PicoAgentTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoAgentTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoMcpTests
    Tests/Mcp/Private/Main.cpp
)

target_include_directories(PicoMcpTests PRIVATE
    Tests/Common
    ThirdParty/NlohmannJson/single_include
)
target_link_libraries(PicoMcpTests PRIVATE PicoMcpCore)
set_target_properties(PicoMcpTests PROPERTIES FOLDER "Tests/Developer")

if(MSVC)
    target_compile_options(PicoMcpTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoMcpTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoMcpAdapterTests
    Tests/McpAdapter/Private/Main.cpp
)

target_include_directories(PicoMcpAdapterTests PRIVATE
    Tests/Common
    ThirdParty/NlohmannJson/single_include
)
target_link_libraries(PicoMcpAdapterTests PRIVATE PicoMcpAgentAdapter)
set_target_properties(PicoMcpAdapterTests PROPERTIES FOLDER "Tests/Developer")

if(MSVC)
    target_compile_options(PicoMcpAdapterTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoMcpAdapterTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoMcpHttpTests
    Tests/McpHttp/Private/Main.cpp
)

target_include_directories(PicoMcpHttpTests PRIVATE
    Tests/Common
    ThirdParty/NlohmannJson/single_include
)
target_link_libraries(PicoMcpHttpTests PRIVATE PicoMcpHttp httplib::httplib)
set_target_properties(PicoMcpHttpTests PROPERTIES FOLDER "Tests/Developer")

if(MSVC)
    target_compile_options(PicoMcpHttpTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoMcpHttpTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoPackagingTests
    Tests/Packaging/Private/Main.cpp
)

target_include_directories(PicoPackagingTests PRIVATE Tests/Common)
target_link_libraries(PicoPackagingTests PRIVATE PicoPackaging)
set_target_properties(PicoPackagingTests PROPERTIES FOLDER "Tests/Packaging")

if(MSVC)
    target_compile_options(PicoPackagingTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoPackagingTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

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

add_executable(PicoGraphTests
    Tests/Graph/Private/Main.cpp
)

target_include_directories(PicoGraphTests PRIVATE Tests/Common)
target_link_libraries(PicoGraphTests PRIVATE PicoGraph)
set_target_properties(PicoGraphTests PROPERTIES FOLDER "Tests/Graph")

if(MSVC)
    target_compile_options(PicoGraphTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoGraphTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

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

if(TARGET assimp::assimp
    AND EXISTS "${PICO_ASSIMP_SOURCE_DIR}/test/models/glTF2/simple_skin/simple_skin.gltf"
    AND EXISTS "${PICO_ASSIMP_SOURCE_DIR}/test/models/FBX/animation_with_skeleton.fbx")
    target_compile_definitions(PicoAssetImportTests PRIVATE
        PICO_ASSIMP_GLTF_FIXTURE="${PICO_ASSIMP_SOURCE_DIR}/test/models/glTF2/simple_skin/simple_skin.gltf"
        PICO_ASSIMP_FBX_FIXTURE="${PICO_ASSIMP_SOURCE_DIR}/test/models/FBX/animation_with_skeleton.fbx"
    )
endif()

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

add_executable(PicoGameplayAbilitiesTests
    Tests/GameplayAbilities/Private/Main.cpp
)

target_include_directories(PicoGameplayAbilitiesTests PRIVATE Tests/Common)
target_link_libraries(PicoGameplayAbilitiesTests PRIVATE PicoGameplayAbilities)
set_target_properties(PicoGameplayAbilitiesTests PROPERTIES FOLDER "Tests/GameplayAbilities")

if(MSVC)
    target_compile_options(PicoGameplayAbilitiesTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoGameplayAbilitiesTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoNetCoreTests
    Tests/NetCore/Private/Main.cpp
)

add_executable(PicoReplicationTests
    Tests/Replication/Private/Main.cpp
)

target_include_directories(PicoReplicationTests PRIVATE Tests/Common)
target_link_libraries(PicoReplicationTests PRIVATE PicoEngine)
set_target_properties(PicoReplicationTests PROPERTIES FOLDER "Tests/Net")

if(MSVC)
    target_compile_options(PicoReplicationTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoReplicationTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_include_directories(PicoNetCoreTests PRIVATE Tests/Common)
target_link_libraries(PicoNetCoreTests PRIVATE PicoNetCore)
set_target_properties(PicoNetCoreTests PROPERTIES FOLDER "Tests/NetCore")

if(MSVC)
    target_compile_options(PicoNetCoreTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoNetCoreTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(PicoGameTests
    Tests/Game/Private/Main.cpp
)

target_include_directories(PicoGameTests PRIVATE Tests/Common)
target_link_libraries(PicoGameTests PRIVATE PicoEngine)
set_target_properties(PicoGameTests PROPERTIES FOLDER "Tests/Game")

add_executable(PicoMovementTests
    Tests/Movement/Private/Main.cpp
)
target_include_directories(PicoMovementTests PRIVATE Tests/Common)
target_link_libraries(PicoMovementTests PRIVATE PicoEngine)
set_target_properties(PicoMovementTests PROPERTIES FOLDER "Tests/Movement")

add_executable(PicoPhysicsTests
    Tests/Physics/Private/Main.cpp
)
target_include_directories(PicoPhysicsTests PRIVATE Tests/Common)
target_link_libraries(PicoPhysicsTests PRIVATE PicoEngine)
set_target_properties(PicoPhysicsTests PROPERTIES FOLDER "Tests/Physics")

add_executable(PicoCharacterMovementTests
    Tests/CharacterMovement/Private/Main.cpp
)
target_include_directories(PicoCharacterMovementTests PRIVATE Tests/Common)
target_link_libraries(PicoCharacterMovementTests PRIVATE PicoEngine)
set_target_properties(PicoCharacterMovementTests PROPERTIES FOLDER "Tests/Movement")

add_executable(PicoAnimationTests
    Tests/Animation/Private/Main.cpp
)
target_include_directories(PicoAnimationTests PRIVATE Tests/Common)
target_link_libraries(PicoAnimationTests PRIVATE PicoEngine)
set_target_properties(PicoAnimationTests PROPERTIES FOLDER "Tests/Animation")

if(MSVC)
    target_compile_options(PicoGameTests PRIVATE /W4 /permissive-)
    target_compile_options(PicoMovementTests PRIVATE /W4 /permissive-)
    target_compile_options(PicoPhysicsTests PRIVATE /W4 /permissive-)
    target_compile_options(PicoCharacterMovementTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(PicoGameTests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(PicoMovementTests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(PicoPhysicsTests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(PicoCharacterMovementTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

add_subdirectory(Projects/PicoSandbox)

# PicoGameTests intentionally loads the PicoSandbox project map, so it must know
# every project class that may be authored into that map.
target_link_libraries(PicoGameTests PRIVATE PicoSandboxModule)
target_link_libraries(PicoEditorTests PRIVATE PicoSandboxModule)

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
    add_test(NAME PicoTaskTests COMMAND PicoTaskTests)
    add_test(NAME PicoAgentTests COMMAND PicoAgentTests)
    add_test(NAME PicoMcpTests COMMAND PicoMcpTests)
    add_test(NAME PicoMcpAdapterTests COMMAND PicoMcpAdapterTests)
    add_test(NAME PicoMcpHttpTests COMMAND PicoMcpHttpTests)
    add_test(
        NAME PicoRuntimeBenchmarksSmoke
        COMMAND PicoRuntimeBenchmarks --quick
            --output=${CMAKE_BINARY_DIR}/BenchmarkResults/CTest)
    add_test(NAME PicoPackagingTests COMMAND PicoPackagingTests)
    add_test(NAME PicoAssetTests COMMAND PicoAssetTests)
    add_test(NAME PicoGraphTests COMMAND PicoGraphTests)
    add_test(NAME PicoAssetImportTests COMMAND PicoAssetImportTests)
    add_test(NAME PicoObjectTests COMMAND PicoObjectTests)
    add_test(NAME PicoGarbageCollectionTests COMMAND PicoGarbageCollectionTests)
    add_test(NAME PicoDynamicDelegateTests COMMAND PicoDynamicDelegateTests)
    add_test(NAME PicoEngineTests COMMAND PicoEngineTests)
    add_test(NAME PicoGameplayAbilitiesTests COMMAND PicoGameplayAbilitiesTests)
    add_test(NAME PicoNetCoreTests COMMAND PicoNetCoreTests)
    add_test(NAME PicoReplicationTests COMMAND PicoReplicationTests)
    add_test(NAME PicoGameTests COMMAND PicoGameTests)
    add_test(NAME PicoMovementTests COMMAND PicoMovementTests)
    add_test(NAME PicoPhysicsTests COMMAND PicoPhysicsTests)
    add_test(NAME PicoCharacterMovementTests COMMAND PicoCharacterMovementTests)
    add_test(NAME PicoAnimationTests COMMAND PicoAnimationTests)
    if(PICO_BUILD_INSPECTOR)
        add_test(NAME PicoEditorTests COMMAND PicoEditorTests)
        add_test(NAME PicoEditorAssetTests COMMAND PicoEditorAssetTests)
    endif()
    set_tests_properties(PicoCoreTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoTaskTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoAgentTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoMcpTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoMcpAdapterTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoMcpHttpTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoRuntimeBenchmarksSmoke PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
        TIMEOUT 60
    )
    set_tests_properties(PicoPackagingTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoAssetTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoGraphTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoReplicationTests PROPERTIES
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
    set_tests_properties(PicoGameplayAbilitiesTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoGameTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoMovementTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoPhysicsTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoCharacterMovementTests PROPERTIES
        WORKING_DIRECTORY ${PICO_ROOT_DIR}
    )
    set_tests_properties(PicoAnimationTests PROPERTIES
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
