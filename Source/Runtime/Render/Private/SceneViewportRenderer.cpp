#include "Pico/Render/SceneViewportRenderer.h"

#include "Pico/Asset/AssetManager.h"
#include "Pico/Asset/AssetRegistry.h"
#include "Pico/Asset/StaticMesh.h"
#include "Pico/Core/Log.h"
#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Core/Math/Matrix4.h"
#include "Pico/Core/Math/Transform.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CameraComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/DirectionalLightComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/LightComponent.h"
#include "Pico/Engine/PointLightComponent.h"
#include "Pico/Engine/PlayerStart.h"
#include "Pico/Engine/PrimitiveComponent.h"
#include "Pico/Engine/StaticMeshComponent.h"
#include "Pico/Engine/SpringArmComponent.h"
#include "Pico/Engine/World.h"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

namespace Pico
{
FMatrix4 BuildSceneProjectionMatrix(
    const FSceneView& View,
    float AspectRatio)
{
    const float HalfFovRadians =
        DegreesToRadians(View.VerticalFieldOfViewDegrees) * 0.5f;
    const float FocalLength = 1.0f / std::tan(HalfFovRadians);

    FMatrix4 Result;
    Result[0][0] = FocalLength / AspectRatio;
    Result[1][1] = FocalLength;
    Result[2][2] = (View.FarPlane + View.NearPlane)
        / (View.NearPlane - View.FarPlane);
    Result[2][3] = (2.0f * View.FarPlane * View.NearPlane)
        / (View.NearPlane - View.FarPlane);
    Result[3][2] = -1.0f;
    return Result;
}

FMatrix4 BuildSceneViewMatrix(const FSceneView& View)
{
    const FVector3 Forward = (View.Target - View.Position).GetSafeNormal();
    FVector3 Right = FVector3::Cross(Forward, View.Up).GetSafeNormal();
    if (Right.IsNearlyZero())
    {
        Right = FVector3::RightVector;
    }
    const FVector3 CameraUp = FVector3::Cross(Right, Forward).GetSafeNormal();

    FMatrix4 Result = FMatrix4::Identity;
    Result[0][0] = Right.X;
    Result[0][1] = Right.Y;
    Result[0][2] = Right.Z;
    Result[0][3] = -FVector3::Dot(Right, View.Position);
    Result[1][0] = CameraUp.X;
    Result[1][1] = CameraUp.Y;
    Result[1][2] = CameraUp.Z;
    Result[1][3] = -FVector3::Dot(CameraUp, View.Position);
    Result[2][0] = -Forward.X;
    Result[2][1] = -Forward.Y;
    Result[2][2] = -Forward.Z;
    Result[2][3] = FVector3::Dot(Forward, View.Position);
    return Result;
}

bool TryBuildActiveCameraView(
    const PWorld* World,
    FSceneView& OutView,
    bool bAllowInactiveFallback)
{
    if (World == nullptr)
    {
        return false;
    }
    const PCameraComponent* FallbackCamera = nullptr;
    for (const PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr) continue;
        for (const PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr) continue;
            for (const PActorComponent* Component : Actor->GetComponents())
            {
                const PCameraComponent* Camera = Component != nullptr
                    && Component->IsA(PCameraComponent::StaticClass())
                    ? static_cast<const PCameraComponent*>(Component) : nullptr;
                if (Camera == nullptr) continue;
                if (!Camera->IsActive())
                {
                    if (FallbackCamera == nullptr)
                    {
                        FallbackCamera = Camera;
                    }
                    continue;
                }

                OutView.Position = Camera->GetViewPosition();
                OutView.Target = OutView.Position + Camera->GetViewForward();
                OutView.Up = Camera->GetViewUp();
                OutView.VerticalFieldOfViewDegrees =
                    Camera->GetVerticalFieldOfViewDegrees();
                OutView.NearPlane = Camera->GetNearPlane();
                OutView.FarPlane = Camera->GetFarPlane();
                return true;
            }
        }
    }
    if (!bAllowInactiveFallback || FallbackCamera == nullptr)
    {
        return false;
    }
    OutView.Position = FallbackCamera->GetViewPosition();
    OutView.Target = OutView.Position + FallbackCamera->GetViewForward();
    OutView.Up = FallbackCamera->GetViewUp();
    OutView.VerticalFieldOfViewDegrees =
        FallbackCamera->GetVerticalFieldOfViewDegrees();
    OutView.NearPlane = FallbackCamera->GetNearPlane();
    OutView.FarPlane = FallbackCamera->GetFarPlane();
    return true;
}

FSceneLighting GatherSceneLighting(const PWorld* World)
{
    FSceneLighting Lighting;
    if (World == nullptr)
    {
        return Lighting;
    }
    for (const PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr) continue;
        for (const PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr) continue;
            for (const PActorComponent* Component : Actor->GetComponents())
            {
                const PLightComponent* Light = Component != nullptr
                    && Component->IsA(PLightComponent::StaticClass())
                    ? static_cast<const PLightComponent*>(Component) : nullptr;
                if (Light == nullptr) continue;
                Lighting.bHasAuthoredLights = true;
                if (!Light->IsEnabled()) continue;

                if (!Lighting.DirectionalLight.bEnabled
                    && Light->IsA(PDirectionalLightComponent::StaticClass()))
                {
                    const auto* Directional =
                        static_cast<const PDirectionalLightComponent*>(Light);
                    Lighting.DirectionalLight.bEnabled = true;
                    Lighting.DirectionalLight.Direction =
                        -Directional->GetLightDirection();
                    Lighting.DirectionalLight.Color = Light->GetLightColor();
                    Lighting.DirectionalLight.Intensity = Light->GetIntensity();
                }
                else if (Lighting.PointLightCount < FSceneLighting::MaxPointLights
                    && Light->IsA(PPointLightComponent::StaticClass()))
                {
                    const auto* Point = static_cast<const PPointLightComponent*>(Light);
                    FPointLightData& Data =
                        Lighting.PointLights[Lighting.PointLightCount++];
                    Data.Position = Point->GetLightPosition();
                    Data.Color = Light->GetLightColor();
                    Data.Intensity = Light->GetIntensity();
                    Data.AttenuationRadius = Point->GetAttenuationRadius();
                }
            }
        }
    }
    if (!Lighting.bHasAuthoredLights)
    {
        Lighting.DirectionalLight.bEnabled = true;
    }
    return Lighting;
}

namespace
{

constexpr std::array<float, 144> CubeVertices {
    -1, -1,  1,  0,  0,  1,   1, -1,  1,  0,  0,  1,
     1,  1,  1,  0,  0,  1,  -1,  1,  1,  0,  0,  1,
     1, -1, -1,  0,  0, -1,  -1, -1, -1,  0,  0, -1,
    -1,  1, -1,  0,  0, -1,   1,  1, -1,  0,  0, -1,
     1, -1,  1,  1,  0,  0,   1, -1, -1,  1,  0,  0,
     1,  1, -1,  1,  0,  0,   1,  1,  1,  1,  0,  0,
    -1, -1, -1, -1,  0,  0,  -1, -1,  1, -1,  0,  0,
    -1,  1,  1, -1,  0,  0,  -1,  1, -1, -1,  0,  0,
    -1,  1,  1,  0,  1,  0,   1,  1,  1,  0,  1,  0,
     1,  1, -1,  0,  1,  0,  -1,  1, -1,  0,  1,  0,
    -1, -1, -1,  0, -1,  0,   1, -1, -1,  0, -1,  0,
     1, -1,  1,  0, -1,  0,  -1, -1,  1,  0, -1,  0
};

constexpr std::array<unsigned int, 36> CubeIndices {
     0,  1,  2,  2,  3,  0,   4,  5,  6,  6,  7,  4,
     8,  9, 10, 10, 11,  8,  12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,  20, 21, 22, 22, 23, 20
};

constexpr std::array<unsigned int, 24> CubeOutlineIndices {
     0,  1,  1,  2,  2,  3,  3,  0,
     5,  4,  4,  7,  7,  6,  6,  5,
     0,  5,  1,  4,  2,  7,  3,  6
};

struct FComponentVisualization
{
    std::vector<FVector3> Vertices;
    FVector3 Color = FVector3::OneVector;
};

void AddLine(
    std::vector<FVector3>& Vertices,
    const FVector3& Start,
    const FVector3& End)
{
    Vertices.push_back(Start);
    Vertices.push_back(End);
}

void AddCircle(
    std::vector<FVector3>& Vertices,
    const FVector3& Center,
    const FVector3& AxisX,
    const FVector3& AxisY,
    float Radius)
{
    constexpr int SegmentCount = 32;
    for (int Segment = 0; Segment < SegmentCount; ++Segment)
    {
        const float Angle0 = 2.0f * Pi * static_cast<float>(Segment)
            / static_cast<float>(SegmentCount);
        const float Angle1 = 2.0f * Pi * static_cast<float>(Segment + 1)
            / static_cast<float>(SegmentCount);
        AddLine(
            Vertices,
            Center + AxisX * (std::cos(Angle0) * Radius)
                + AxisY * (std::sin(Angle0) * Radius),
            Center + AxisX * (std::cos(Angle1) * Radius)
                + AxisY * (std::sin(Angle1) * Radius));
    }
}

FComponentVisualization BuildComponentVisualization(
    const PSceneComponent* Component,
    const FSceneView& View,
    float AspectRatio,
    bool bSelected)
{
    FComponentVisualization Result;
    if (Component == nullptr)
    {
        return Result;
    }

    const FTransform WorldTransform = Component->GetWorldTransform();
    const FVector3 Origin = WorldTransform.Translation;
    const float Distance = (Origin - View.Position).Size();
    const float VisualSize = std::clamp(Distance * 0.08f, 20.0f, 200.0f);
    const FVector3 Forward = WorldTransform.Rotation.RotateVector(
        FVector3::ForwardVector).GetSafeNormal();
    const FVector3 Up = WorldTransform.Rotation.RotateVector(
        FVector3::UpVector).GetSafeNormal();
    const FVector3 Right = FVector3::Cross(Forward, Up).GetSafeNormal();

    const PActor* Owner = Component->GetOwner();
    if (Owner != nullptr && Owner->IsA(PPlayerStart::StaticClass()))
    {
        const float Radius = VisualSize * 0.34f;
        const float HalfHeight = VisualSize * 0.75f;
        const FVector3 Top = Origin + Up * HalfHeight;
        const FVector3 Bottom = Origin - Up * HalfHeight;
        AddCircle(Result.Vertices, Top, Forward, Right, Radius);
        AddCircle(Result.Vertices, Bottom, Forward, Right, Radius);
        AddLine(Result.Vertices, Top + Forward * Radius, Bottom + Forward * Radius);
        AddLine(Result.Vertices, Top - Forward * Radius, Bottom - Forward * Radius);
        AddLine(Result.Vertices, Top + Right * Radius, Bottom + Right * Radius);
        AddLine(Result.Vertices, Top - Right * Radius, Bottom - Right * Radius);
        const FVector3 ArrowEnd = Origin + Forward * (VisualSize * 1.35f);
        AddLine(Result.Vertices, Origin, ArrowEnd);
        AddLine(
            Result.Vertices,
            ArrowEnd,
            ArrowEnd - Forward * (VisualSize * 0.35f) + Right * (VisualSize * 0.2f));
        AddLine(
            Result.Vertices,
            ArrowEnd,
            ArrowEnd - Forward * (VisualSize * 0.35f) - Right * (VisualSize * 0.2f));
        Result.Color = bSelected
            ? FVector3(0.25f, 1.0f, 0.55f)
            : FVector3(0.15f, 0.75f, 0.42f);
    }
    else if (Component->IsA(PCameraComponent::StaticClass()))
    {
        const auto* Camera = static_cast<const PCameraComponent*>(Component);
        const FVector3 ViewForward = (View.Target - View.Position).GetSafeNormal();
        if (Origin.Equals(View.Position, 0.01f)
            && Forward.Equals(ViewForward, 0.001f))
        {
            return Result;
        }
        const float Depth = VisualSize * 1.5f;
        const float HalfHeight = std::tan(DegreesToRadians(
            Camera->GetVerticalFieldOfViewDegrees()) * 0.5f) * Depth;
        const float HalfWidth = HalfHeight * AspectRatio;
        const FVector3 Center = Origin + Forward * Depth;
        const std::array<FVector3, 4> Corners {
            Center + Right * HalfWidth + Up * HalfHeight,
            Center - Right * HalfWidth + Up * HalfHeight,
            Center - Right * HalfWidth - Up * HalfHeight,
            Center + Right * HalfWidth - Up * HalfHeight
        };
        for (const FVector3& Corner : Corners)
        {
            AddLine(Result.Vertices, Origin, Corner);
        }
        for (std::size_t Index = 0; Index < Corners.size(); ++Index)
        {
            AddLine(
                Result.Vertices,
                Corners[Index],
                Corners[(Index + 1) % Corners.size()]);
        }
        AddLine(Result.Vertices, Origin, Origin + Up * (VisualSize * 0.5f));
        Result.Color = FVector3(0.35f, 0.8f, 1.0f);
    }
    else if (Component->IsA(PDirectionalLightComponent::StaticClass()))
    {
        const auto* Light = static_cast<const PDirectionalLightComponent*>(Component);
        const FVector3 Direction = Light->GetLightDirection();
        const FVector3 End = Origin + Direction * (VisualSize * 2.0f);
        const FVector3 ArrowRight = FVector3::Cross(Direction, Up).GetSafeNormal();
        AddLine(Result.Vertices, Origin, End);
        AddLine(
            Result.Vertices,
            End,
            End - Direction * (VisualSize * 0.45f)
                + Up * (VisualSize * 0.25f));
        AddLine(
            Result.Vertices,
            End,
            End - Direction * (VisualSize * 0.45f)
                - Up * (VisualSize * 0.25f));
        AddLine(
            Result.Vertices,
            End,
            End - Direction * (VisualSize * 0.45f)
                + ArrowRight * (VisualSize * 0.25f));
        AddLine(
            Result.Vertices,
            End,
            End - Direction * (VisualSize * 0.45f)
                - ArrowRight * (VisualSize * 0.25f));
        AddCircle(Result.Vertices, Origin, Up, Right, VisualSize * 0.3f);
        Result.Color = FVector3(1.0f, 0.82f, 0.25f);
    }
    else if (Component->IsA(PPointLightComponent::StaticClass()))
    {
        const auto* Light = static_cast<const PPointLightComponent*>(Component);
        const float IconRadius = VisualSize * 0.28f;
        AddCircle(
            Result.Vertices,
            Origin,
            FVector3::ForwardVector,
            FVector3::RightVector,
            IconRadius);
        AddCircle(
            Result.Vertices,
            Origin,
            FVector3::ForwardVector,
            FVector3::UpVector,
            IconRadius);
        AddCircle(
            Result.Vertices,
            Origin,
            FVector3::RightVector,
            FVector3::UpVector,
            IconRadius);
        if (bSelected)
        {
            const float Radius = Light->GetAttenuationRadius();
            AddCircle(
                Result.Vertices,
                Origin,
                FVector3::ForwardVector,
                FVector3::RightVector,
                Radius);
            AddCircle(
                Result.Vertices,
                Origin,
                FVector3::ForwardVector,
                FVector3::UpVector,
                Radius);
            AddCircle(
                Result.Vertices,
                Origin,
                FVector3::RightVector,
                FVector3::UpVector,
                Radius);
        }
        AddLine(
            Result.Vertices,
            Origin - FVector3::ForwardVector * (VisualSize * 0.3f),
            Origin + FVector3::ForwardVector * (VisualSize * 0.3f));
        AddLine(
            Result.Vertices,
            Origin - FVector3::RightVector * (VisualSize * 0.3f),
            Origin + FVector3::RightVector * (VisualSize * 0.3f));
        AddLine(
            Result.Vertices,
            Origin - FVector3::UpVector * (VisualSize * 0.3f),
            Origin + FVector3::UpVector * (VisualSize * 0.3f));
        Result.Color = FVector3(1.0f, 0.65f, 0.2f);
    }
    else if (Component->IsA(PSpringArmComponent::StaticClass()))
    {
        const auto* SpringArm = static_cast<const PSpringArmComponent*>(Component);
        const FVector3 Endpoint = SpringArm->GetSocketTransform(
            PSpringArmComponent::GetEndpointSocketName()).Translation;
        AddLine(Result.Vertices, Origin, Endpoint);
        AddLine(
            Result.Vertices,
            Endpoint - Up * (VisualSize * 0.2f),
            Endpoint + Up * (VisualSize * 0.2f));
        AddLine(
            Result.Vertices,
            Endpoint - Right * (VisualSize * 0.2f),
            Endpoint + Right * (VisualSize * 0.2f));
        Result.Color = FVector3(0.65f, 0.45f, 1.0f);
    }
    return Result;
}
}

struct FSceneViewportRenderer::FImpl
{
    struct FStaticMeshGpuResource
    {
        FAssetPath AssetPath;
        std::shared_ptr<const FStaticMeshData> Source;
        GLuint VertexArray = 0;
        GLuint VertexBuffer = 0;
        GLuint IndexBuffer = 0;
        GLsizei IndexCount = 0;
    };

    struct FTextureGpuResource
    {
        FAssetPath AssetPath;
        std::shared_ptr<const FTextureData> Source;
        GLuint Texture = 0;
    };

    GLuint Program = 0;
    GLuint CubeVertexArray = 0;
    GLuint CubeVertexBuffer = 0;
    GLuint CubeIndexBuffer = 0;
    GLuint CubeOutlineVertexArray = 0;
    GLuint CubeOutlineIndexBuffer = 0;
    GLuint GridVertexArray = 0;
    GLuint GridVertexBuffer = 0;
    GLsizei GridVertexCount = 0;
    GLuint ComponentVisualizationVertexArray = 0;
    GLuint ComponentVisualizationVertexBuffer = 0;
    std::vector<FStaticMeshGpuResource> StaticMeshes;
    std::vector<FTextureGpuResource> Textures;
    GLuint Framebuffer = 0;
    GLuint ColorTexture = 0;
    GLuint PickingTexture = 0;
    GLuint DepthRenderbuffer = 0;
    std::vector<FObjectHandle> PickHandles;
    uint32 Width = 0;
    uint32 Height = 0;
    bool bInitialized = false;

    GLuint Compile(GLenum Type, const char* Source)
    {
        const GLuint Shader = glCreateShader(Type);
        glShaderSource(Shader, 1, &Source, nullptr);
        glCompileShader(Shader);

        GLint bCompiled = GL_FALSE;
        glGetShaderiv(Shader, GL_COMPILE_STATUS, &bCompiled);
        if (bCompiled == GL_TRUE)
        {
            return Shader;
        }

        GLint Length = 0;
        glGetShaderiv(Shader, GL_INFO_LOG_LENGTH, &Length);
        std::string Log(static_cast<std::size_t>(std::max(Length, 1)), '\0');
        glGetShaderInfoLog(Shader, Length, nullptr, Log.data());
        PICO_LOG(LogRender, Error, "OpenGL shader compilation failed: {}", Log);
        glDeleteShader(Shader);
        return 0;
    }

    bool CreateProgramAndGeometry()
    {
        static constexpr char VertexSource[] = R"(
#version 330 core
layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InTexCoord;
uniform mat4 ViewProjection;
uniform mat4 Model;
out vec3 WorldPosition;
out vec3 WorldNormal;
out vec2 TexCoord;
void main()
{
    vec4 Position = Model * vec4(InPosition, 1.0);
    WorldPosition = Position.xyz;
    WorldNormal = mat3(Model) * InNormal;
    TexCoord = InTexCoord;
    gl_Position = ViewProjection * Position;
}
)";
        static constexpr char FragmentSource[] = R"(
#version 330 core
in vec3 WorldPosition;
in vec3 WorldNormal;
in vec2 TexCoord;
uniform vec3 BaseColor;
uniform vec3 CameraPosition;
uniform float Metallic;
uniform float Roughness;
uniform sampler2D BaseColorTexture;
uniform int UseBaseColorTexture;
uniform int UseLighting;
uniform int DirectionalLightEnabled;
uniform vec3 DirectionalLightDirection;
uniform vec3 DirectionalLightColor;
uniform float DirectionalLightIntensity;
uniform int PointLightCount;
uniform vec3 PointLightPositions[4];
uniform vec3 PointLightColors[4];
uniform float PointLightIntensities[4];
uniform float PointLightRadii[4];
uniform uint PickingId;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out uint FragPickingId;
const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float RoughnessValue)
{
    float A = RoughnessValue * RoughnessValue;
    float A2 = A * A;
    float NdotH = max(dot(N, H), 0.0);
    float Denominator = NdotH * NdotH * (A2 - 1.0) + 1.0;
    return A2 / max(PI * Denominator * Denominator, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float RoughnessValue)
{
    float R = RoughnessValue + 1.0;
    float K = (R * R) / 8.0;
    return NdotV / max(NdotV * (1.0 - K) + K, 0.0001);
}

vec3 FresnelSchlick(float CosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - CosTheta, 5.0);
}

vec3 EvaluatePbrLight(
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 Radiance,
    vec3 Albedo)
{
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    if (NdotL <= 0.0 || NdotV <= 0.0)
    {
        return vec3(0.0);
    }
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), Albedo, Metallic);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = DistributionGGX(N, H, Roughness);
    float G = GeometrySchlickGGX(NdotV, Roughness)
        * GeometrySchlickGGX(NdotL, Roughness);
    vec3 Specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);
    vec3 Diffuse = (1.0 - F) * (1.0 - Metallic) * Albedo / PI;
    return (Diffuse + Specular) * Radiance * NdotL;
}

void main()
{
    vec3 Albedo = BaseColor;
    if (UseBaseColorTexture != 0)
    {
        Albedo *= texture(BaseColorTexture, TexCoord).rgb;
    }
    vec3 Color = Albedo;
    if (UseLighting != 0)
    {
        vec3 N = normalize(WorldNormal);
        vec3 V = normalize(CameraPosition - WorldPosition);
        vec3 Direct = vec3(0.0);
        if (DirectionalLightEnabled != 0)
        {
            Direct += EvaluatePbrLight(
                N,
                V,
                normalize(DirectionalLightDirection),
                DirectionalLightColor * DirectionalLightIntensity,
                Albedo);
        }
        for (int Index = 0; Index < PointLightCount; ++Index)
        {
            vec3 ToLight = PointLightPositions[Index] - WorldPosition;
            float Distance = length(ToLight);
            if (Distance <= 0.0001)
            {
                continue;
            }
            float Radius = max(PointLightRadii[Index], 0.0001);
            float Falloff = max(1.0 - Distance / Radius, 0.0);
            vec3 Radiance = PointLightColors[Index]
                * PointLightIntensities[Index] * Falloff * Falloff;
            Direct += EvaluatePbrLight(
                N, V, normalize(ToLight), Radiance, Albedo);
        }
        vec3 Ambient = Albedo * 0.04;
        Color = Ambient + Direct;
        Color = Color / (Color + vec3(1.0));
        Color = pow(Color, vec3(1.0 / 2.2));
    }
    FragColor = vec4(Color, 1.0);
    FragPickingId = PickingId;
}
)";

        const GLuint VertexShader = Compile(GL_VERTEX_SHADER, VertexSource);
        const GLuint FragmentShader = Compile(GL_FRAGMENT_SHADER, FragmentSource);
        if (VertexShader == 0 || FragmentShader == 0)
        {
            if (VertexShader != 0)
            {
                glDeleteShader(VertexShader);
            }
            if (FragmentShader != 0)
            {
                glDeleteShader(FragmentShader);
            }
            return false;
        }

        Program = glCreateProgram();
        glAttachShader(Program, VertexShader);
        glAttachShader(Program, FragmentShader);
        glLinkProgram(Program);
        glDeleteShader(VertexShader);
        glDeleteShader(FragmentShader);

        GLint bLinked = GL_FALSE;
        glGetProgramiv(Program, GL_LINK_STATUS, &bLinked);
        if (bLinked != GL_TRUE)
        {
            GLint Length = 0;
            glGetProgramiv(Program, GL_INFO_LOG_LENGTH, &Length);
            std::string Log(static_cast<std::size_t>(std::max(Length, 1)), '\0');
            glGetProgramInfoLog(Program, Length, nullptr, Log.data());
            PICO_LOG(LogRender, Error, "OpenGL program linking failed: {}", Log);
            return false;
        }

        glGenVertexArrays(1, &CubeVertexArray);
        glBindVertexArray(CubeVertexArray);
        glGenBuffers(1, &CubeVertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, CubeVertexBuffer);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(CubeVertices.size() * sizeof(float)),
            CubeVertices.data(),
            GL_STATIC_DRAW);
        glGenBuffers(1, &CubeIndexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, CubeIndexBuffer);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(CubeIndices.size() * sizeof(unsigned int)),
            CubeIndices.data(),
            GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1,
            3,
            GL_FLOAT,
            GL_FALSE,
            6 * sizeof(float),
            reinterpret_cast<const void*>(3 * sizeof(float)));

        glGenVertexArrays(1, &CubeOutlineVertexArray);
        glBindVertexArray(CubeOutlineVertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, CubeVertexBuffer);
        glGenBuffers(1, &CubeOutlineIndexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, CubeOutlineIndexBuffer);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(
                CubeOutlineIndices.size() * sizeof(unsigned int)),
            CubeOutlineIndices.data(),
            GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1,
            3,
            GL_FLOAT,
            GL_FALSE,
            6 * sizeof(float),
            reinterpret_cast<const void*>(3 * sizeof(float)));

        std::vector<float> GridVertices;
        constexpr int HalfLineCount = 10;
        constexpr float Spacing = 100.0f;
        constexpr float Extent = HalfLineCount * Spacing;
        GridVertices.reserve((HalfLineCount * 2 + 1) * 4 * 6);
        for (int Index = -HalfLineCount; Index <= HalfLineCount; ++Index)
        {
            const float Coordinate = static_cast<float>(Index) * Spacing;
            const std::array<FVector3, 4> Positions {
                FVector3(-Extent, Coordinate, 0.0f),
                FVector3(Extent, Coordinate, 0.0f),
                FVector3(Coordinate, -Extent, 0.0f),
                FVector3(Coordinate, Extent, 0.0f)
            };
            for (const FVector3& Position : Positions)
            {
                GridVertices.insert(
                    GridVertices.end(),
                    { Position.X, Position.Y, Position.Z, 0.0f, 0.0f, 1.0f });
            }
        }
        GridVertexCount = static_cast<GLsizei>(GridVertices.size() / 6);

        glGenVertexArrays(1, &GridVertexArray);
        glBindVertexArray(GridVertexArray);
        glGenBuffers(1, &GridVertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, GridVertexBuffer);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(GridVertices.size() * sizeof(float)),
            GridVertices.data(),
            GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1,
            3,
            GL_FLOAT,
            GL_FALSE,
            6 * sizeof(float),
            reinterpret_cast<const void*>(3 * sizeof(float)));

        glGenVertexArrays(1, &ComponentVisualizationVertexArray);
        glBindVertexArray(ComponentVisualizationVertexArray);
        glGenBuffers(1, &ComponentVisualizationVertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, ComponentVisualizationVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(FVector3),
            nullptr);
        glBindVertexArray(0);
        return true;
    }

    void DestroyRenderTarget()
    {
        if (DepthRenderbuffer != 0)
        {
            glDeleteRenderbuffers(1, &DepthRenderbuffer);
            DepthRenderbuffer = 0;
        }
        if (ColorTexture != 0)
        {
            glDeleteTextures(1, &ColorTexture);
            ColorTexture = 0;
        }
        if (PickingTexture != 0)
        {
            glDeleteTextures(1, &PickingTexture);
            PickingTexture = 0;
        }
        if (Framebuffer != 0)
        {
            glDeleteFramebuffers(1, &Framebuffer);
            Framebuffer = 0;
        }
        Width = 0;
        Height = 0;
        PickHandles.clear();
    }
};

FSceneViewportRenderer::FSceneViewportRenderer()
    : Impl(std::make_unique<FImpl>())
{
}

FSceneViewportRenderer::~FSceneViewportRenderer() = default;

bool FSceneViewportRenderer::Initialize(FOpenGLProcLoader Loader)
{
    if (Impl->bInitialized)
    {
        return true;
    }
    const int LoadedVersion =
        Loader != nullptr ? gladLoadGL(reinterpret_cast<GLADloadfunc>(Loader)) : 0;
    if (LoadedVersion == 0)
    {
        PICO_LOG(LogRender, Error, "GLAD failed to load the active OpenGL context");
        return false;
    }
    if (!GLAD_GL_VERSION_3_3)
    {
        PICO_LOG(LogRender, Error, "OpenGL 3.3 is unavailable");
        return false;
    }
    if (!Impl->CreateProgramAndGeometry())
    {
        Shutdown();
        return false;
    }
    Impl->bInitialized = true;
    PICO_LOG(
        LogRender,
        Info,
        "OpenGL {}.{} scene viewport renderer initialized through GLAD",
        GLAD_VERSION_MAJOR(LoadedVersion),
        GLAD_VERSION_MINOR(LoadedVersion));
    return true;
}

void FSceneViewportRenderer::Shutdown()
{
    Impl->DestroyRenderTarget();
    for (FImpl::FStaticMeshGpuResource& Mesh : Impl->StaticMeshes)
    {
        if (Mesh.IndexBuffer != 0)
        {
            glDeleteBuffers(1, &Mesh.IndexBuffer);
        }
        if (Mesh.VertexBuffer != 0)
        {
            glDeleteBuffers(1, &Mesh.VertexBuffer);
        }
        if (Mesh.VertexArray != 0)
        {
            glDeleteVertexArrays(1, &Mesh.VertexArray);
        }
    }
    Impl->StaticMeshes.clear();
    for (FImpl::FTextureGpuResource& Texture : Impl->Textures)
    {
        if (Texture.Texture != 0)
        {
            glDeleteTextures(1, &Texture.Texture);
        }
    }
    Impl->Textures.clear();
    if (Impl->ComponentVisualizationVertexBuffer != 0)
    {
        glDeleteBuffers(1, &Impl->ComponentVisualizationVertexBuffer);
        Impl->ComponentVisualizationVertexBuffer = 0;
    }
    if (Impl->ComponentVisualizationVertexArray != 0)
    {
        glDeleteVertexArrays(1, &Impl->ComponentVisualizationVertexArray);
        Impl->ComponentVisualizationVertexArray = 0;
    }
    if (Impl->GridVertexBuffer != 0)
    {
        glDeleteBuffers(1, &Impl->GridVertexBuffer);
        Impl->GridVertexBuffer = 0;
    }
    if (Impl->GridVertexArray != 0)
    {
        glDeleteVertexArrays(1, &Impl->GridVertexArray);
        Impl->GridVertexArray = 0;
    }
    if (Impl->CubeIndexBuffer != 0)
    {
        glDeleteBuffers(1, &Impl->CubeIndexBuffer);
        Impl->CubeIndexBuffer = 0;
    }
    if (Impl->CubeOutlineIndexBuffer != 0)
    {
        glDeleteBuffers(1, &Impl->CubeOutlineIndexBuffer);
        Impl->CubeOutlineIndexBuffer = 0;
    }
    if (Impl->CubeVertexBuffer != 0)
    {
        glDeleteBuffers(1, &Impl->CubeVertexBuffer);
        Impl->CubeVertexBuffer = 0;
    }
    if (Impl->CubeVertexArray != 0)
    {
        glDeleteVertexArrays(1, &Impl->CubeVertexArray);
        Impl->CubeVertexArray = 0;
    }
    if (Impl->CubeOutlineVertexArray != 0)
    {
        glDeleteVertexArrays(1, &Impl->CubeOutlineVertexArray);
        Impl->CubeOutlineVertexArray = 0;
    }
    if (Impl->Program != 0)
    {
        glDeleteProgram(Impl->Program);
        Impl->Program = 0;
    }
    Impl->bInitialized = false;
}

void FSceneViewportRenderer::InvalidateStaticMesh(const FAssetPath& AssetPath)
{
    const auto Found = std::find_if(
        Impl->StaticMeshes.begin(),
        Impl->StaticMeshes.end(),
        [&AssetPath](const FImpl::FStaticMeshGpuResource& Resource)
        {
            return Resource.AssetPath == AssetPath;
        });
    if (Found == Impl->StaticMeshes.end())
    {
        return;
    }
    if (Found->IndexBuffer != 0)
    {
        glDeleteBuffers(1, &Found->IndexBuffer);
    }
    if (Found->VertexBuffer != 0)
    {
        glDeleteBuffers(1, &Found->VertexBuffer);
    }
    if (Found->VertexArray != 0)
    {
        glDeleteVertexArrays(1, &Found->VertexArray);
    }
    Impl->StaticMeshes.erase(Found);
}

bool FSceneViewportRenderer::Resize(uint32 Width, uint32 Height)
{
    if (!Impl->bInitialized || Width == 0 || Height == 0)
    {
        return false;
    }
    if (Impl->Width == Width && Impl->Height == Height)
    {
        return true;
    }

    Impl->DestroyRenderTarget();
    Impl->Width = Width;
    Impl->Height = Height;
    glGenFramebuffers(1, &Impl->Framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, Impl->Framebuffer);

    glGenTextures(1, &Impl->ColorTexture);
    glBindTexture(GL_TEXTURE_2D, Impl->ColorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        static_cast<GLsizei>(Width),
        static_cast<GLsizei>(Height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        Impl->ColorTexture,
        0);

    glGenTextures(1, &Impl->PickingTexture);
    glBindTexture(GL_TEXTURE_2D, Impl->PickingTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32UI,
        static_cast<GLsizei>(Width),
        static_cast<GLsizei>(Height),
        0,
        GL_RED_INTEGER,
        GL_UNSIGNED_INT,
        nullptr);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT1,
        GL_TEXTURE_2D,
        Impl->PickingTexture,
        0);
    constexpr std::array<GLenum, 2> DrawBuffers {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1
    };
    glDrawBuffers(
        static_cast<GLsizei>(DrawBuffers.size()),
        DrawBuffers.data());

    glGenRenderbuffers(1, &Impl->DepthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, Impl->DepthRenderbuffer);
    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH24_STENCIL8,
        static_cast<GLsizei>(Width),
        static_cast<GLsizei>(Height));
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        Impl->DepthRenderbuffer);

    const bool bComplete =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!bComplete)
    {
        PICO_LOG(LogRender, Error, "Scene viewport framebuffer is incomplete");
        Impl->DestroyRenderTarget();
    }
    return bComplete;
}

bool FSceneViewportRenderer::Render(
    PWorld* World,
    FAssetRegistry& AssetRegistry,
    FAssetManager& AssetManager,
    const FSceneView& View,
    std::span<const FObjectHandle> SelectedObjects,
    bool bDrawComponentVisualizations)
{
    if (!Impl->bInitialized || Impl->Framebuffer == 0 || World == nullptr)
    {
        return false;
    }

    const float Aspect = static_cast<float>(Impl->Width) / static_cast<float>(Impl->Height);
    const FMatrix4 Projection = BuildSceneProjectionMatrix(View, Aspect);
    const FMatrix4 ViewMatrix = BuildSceneViewMatrix(View);
    const FMatrix4 ViewProjection = Projection * ViewMatrix;

    glBindFramebuffer(GL_FRAMEBUFFER, Impl->Framebuffer);
    glViewport(0, 0, static_cast<GLsizei>(Impl->Width), static_cast<GLsizei>(Impl->Height));
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    constexpr std::array<GLfloat, 4> ClearColor {
        0.075f,
        0.085f,
        0.095f,
        1.0f
    };
    glClearBufferfv(GL_COLOR, 0, ClearColor.data());
    constexpr GLuint EmptyPickingId = 0;
    glClearBufferuiv(GL_COLOR, 1, &EmptyPickingId);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(Impl->Program);
    Impl->PickHandles.clear();

    const GLint ViewProjectionLocation =
        glGetUniformLocation(Impl->Program, "ViewProjection");
    const GLint ModelLocation = glGetUniformLocation(Impl->Program, "Model");
    const GLint ColorLocation = glGetUniformLocation(Impl->Program, "BaseColor");
    const GLint CameraLocation = glGetUniformLocation(Impl->Program, "CameraPosition");
    const GLint MetallicLocation = glGetUniformLocation(Impl->Program, "Metallic");
    const GLint RoughnessLocation = glGetUniformLocation(Impl->Program, "Roughness");
    const GLint TextureLocation = glGetUniformLocation(Impl->Program, "BaseColorTexture");
    const GLint UseTextureLocation = glGetUniformLocation(Impl->Program, "UseBaseColorTexture");
    const GLint LightingLocation = glGetUniformLocation(Impl->Program, "UseLighting");
    const GLint DirectionalEnabledLocation =
        glGetUniformLocation(Impl->Program, "DirectionalLightEnabled");
    const GLint DirectionalDirectionLocation =
        glGetUniformLocation(Impl->Program, "DirectionalLightDirection");
    const GLint DirectionalColorLocation =
        glGetUniformLocation(Impl->Program, "DirectionalLightColor");
    const GLint DirectionalIntensityLocation =
        glGetUniformLocation(Impl->Program, "DirectionalLightIntensity");
    const GLint PointLightCountLocation =
        glGetUniformLocation(Impl->Program, "PointLightCount");
    const GLint PickingIdLocation = glGetUniformLocation(Impl->Program, "PickingId");
    glUniformMatrix4fv(
        ViewProjectionLocation,
        1,
        GL_TRUE,
        ViewProjection.GetData());

    const FMatrix4 Identity = FMatrix4::Identity;
    glUniformMatrix4fv(ModelLocation, 1, GL_TRUE, Identity.GetData());
    glUniform3f(ColorLocation, 0.25f, 0.29f, 0.31f);
    glUniform3f(CameraLocation, View.Position.X, View.Position.Y, View.Position.Z);
    glUniform1f(MetallicLocation, 0.0f);
    glUniform1f(RoughnessLocation, 0.8f);
    glUniform1i(TextureLocation, 0);
    glUniform1i(UseTextureLocation, 0);
    const FSceneLighting SceneLighting = GatherSceneLighting(World);
    const FDirectionalLightData& Directional = SceneLighting.DirectionalLight;
    glUniform1i(DirectionalEnabledLocation, Directional.bEnabled ? 1 : 0);
    glUniform3f(
        DirectionalDirectionLocation,
        Directional.Direction.X,
        Directional.Direction.Y,
        Directional.Direction.Z);
    glUniform3f(
        DirectionalColorLocation,
        Directional.Color.X,
        Directional.Color.Y,
        Directional.Color.Z);
    glUniform1f(DirectionalIntensityLocation, Directional.Intensity);
    glUniform1i(
        PointLightCountLocation,
        static_cast<GLint>(SceneLighting.PointLightCount));
    for (std::size_t Index = 0; Index < SceneLighting.PointLightCount; ++Index)
    {
        const FPointLightData& Point = SceneLighting.PointLights[Index];
        const std::string Suffix = "[" + std::to_string(Index) + "]";
        glUniform3f(
            glGetUniformLocation(
                Impl->Program, ("PointLightPositions" + Suffix).c_str()),
            Point.Position.X,
            Point.Position.Y,
            Point.Position.Z);
        glUniform3f(
            glGetUniformLocation(
                Impl->Program, ("PointLightColors" + Suffix).c_str()),
            Point.Color.X,
            Point.Color.Y,
            Point.Color.Z);
        glUniform1f(
            glGetUniformLocation(
                Impl->Program, ("PointLightIntensities" + Suffix).c_str()),
            Point.Intensity);
        glUniform1f(
            glGetUniformLocation(
                Impl->Program, ("PointLightRadii" + Suffix).c_str()),
            Point.AttenuationRadius);
    }
    glUniform1i(LightingLocation, 0);
    glUniform1ui(PickingIdLocation, 0);
    glBindVertexArray(Impl->GridVertexArray);
    glLineWidth(1.0f);
    glDrawArrays(GL_LINES, 0, Impl->GridVertexCount);

    glUniform1i(LightingLocation, 1);

    const auto GetStaticMeshResource =
        [this, &AssetRegistry, &AssetManager](const FAssetPath& AssetPath)
            -> FImpl::FStaticMeshGpuResource*
        {
            const std::shared_ptr<const FStaticMeshData> Mesh =
                AssetManager.LoadStaticMesh(AssetPath, AssetRegistry);
            if (Mesh == nullptr)
            {
                return nullptr;
            }
            auto Found = std::find_if(
                Impl->StaticMeshes.begin(),
                Impl->StaticMeshes.end(),
                [&AssetPath](const FImpl::FStaticMeshGpuResource& Resource)
                {
                    return Resource.AssetPath == AssetPath;
                });
            if (Found != Impl->StaticMeshes.end() && Found->Source == Mesh)
            {
                return &*Found;
            }
            if (Found == Impl->StaticMeshes.end())
            {
                Impl->StaticMeshes.push_back({});
                Found = std::prev(Impl->StaticMeshes.end());
                Found->AssetPath = AssetPath;
                glGenVertexArrays(1, &Found->VertexArray);
                glGenBuffers(1, &Found->VertexBuffer);
                glGenBuffers(1, &Found->IndexBuffer);
            }
            Found->Source = Mesh;
            Found->IndexCount = static_cast<GLsizei>(Mesh->Indices.size());
            glBindVertexArray(Found->VertexArray);
            glBindBuffer(GL_ARRAY_BUFFER, Found->VertexBuffer);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<std::ptrdiff_t>(
                    Mesh->Vertices.size() * sizeof(FStaticMeshVertex)),
                Mesh->Vertices.data(),
                GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Found->IndexBuffer);
            glBufferData(
                GL_ELEMENT_ARRAY_BUFFER,
                static_cast<std::ptrdiff_t>(
                    Mesh->Indices.size() * sizeof(uint32)),
                Mesh->Indices.data(),
                GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(
                0,
                3,
                GL_FLOAT,
                GL_FALSE,
                sizeof(FStaticMeshVertex),
                reinterpret_cast<const void*>(
                    offsetof(FStaticMeshVertex, Position)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(
                1,
                3,
                GL_FLOAT,
                GL_FALSE,
                sizeof(FStaticMeshVertex),
                reinterpret_cast<const void*>(
                    offsetof(FStaticMeshVertex, Normal)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(
                2,
                2,
                GL_FLOAT,
                GL_FALSE,
                sizeof(FStaticMeshVertex),
                reinterpret_cast<const void*>(offsetof(FStaticMeshVertex, TexCoord)));
            return &*Found;
        };

    const auto GetTextureResource =
        [this, &AssetRegistry, &AssetManager](const FAssetPath& AssetPath) -> GLuint
        {
            const std::shared_ptr<const FTextureData> Texture =
                AssetManager.LoadTexture(AssetPath, AssetRegistry);
            if (Texture == nullptr) return 0;
            auto Found = std::find_if(
                Impl->Textures.begin(),
                Impl->Textures.end(),
                [&AssetPath](const FImpl::FTextureGpuResource& Resource)
                {
                    return Resource.AssetPath == AssetPath;
                });
            if (Found != Impl->Textures.end() && Found->Source == Texture)
            {
                return Found->Texture;
            }
            if (Found == Impl->Textures.end())
            {
                Impl->Textures.push_back({});
                Found = std::prev(Impl->Textures.end());
                Found->AssetPath = AssetPath;
                glGenTextures(1, &Found->Texture);
            }
            Found->Source = Texture;
            glBindTexture(GL_TEXTURE_2D, Found->Texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_SRGB8_ALPHA8,
                static_cast<GLsizei>(Texture->Width),
                static_cast<GLsizei>(Texture->Height),
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                Texture->Pixels.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            return Found->Texture;
        };

    for (PLevel* Level : World->GetLevels())
    {
        if (Level == nullptr)
        {
            continue;
        }
        for (PActor* Actor : Level->GetActors())
        {
            if (Actor == nullptr)
            {
                continue;
            }
            for (PActorComponent* Component : Actor->GetComponents())
            {
                if (Component == nullptr
                    || !Component->IsA(PPrimitiveComponent::StaticClass()))
                {
                    continue;
                }

                PPrimitiveComponent* Primitive =
                    static_cast<PPrimitiveComponent*>(Component);
                if (!Primitive->IsVisible())
                {
                    continue;
                }

                GLsizei IndexCount = 0;
                FMaterialData Material;
                GLuint BaseColorTexture = 0;
                bool bHasMaterial = false;
                FTransform ModelTransform = Primitive->GetWorldTransform();
                if (Component->IsA(PCubeComponent::StaticClass()))
                {
                    PCubeComponent* Cube = static_cast<PCubeComponent*>(Component);
                    ModelTransform.Scale = ModelTransform.Scale * Cube->GetExtent();
                    glBindVertexArray(Impl->CubeVertexArray);
                    IndexCount = static_cast<GLsizei>(CubeIndices.size());
                }
                else if (Component->IsA(PStaticMeshComponent::StaticClass()))
                {
                    PStaticMeshComponent* StaticMesh =
                        static_cast<PStaticMeshComponent*>(Component);
                    FImpl::FStaticMeshGpuResource* Resource =
                        GetStaticMeshResource(StaticMesh->GetStaticMeshAsset());
                    if (Resource == nullptr)
                    {
                        continue;
                    }
                    glBindVertexArray(Resource->VertexArray);
                    IndexCount = Resource->IndexCount;
                    const std::shared_ptr<const FMaterialData> LoadedMaterial =
                        AssetManager.LoadMaterial(
                            StaticMesh->GetMaterialAsset(), AssetRegistry);
                    if (LoadedMaterial != nullptr)
                    {
                        bHasMaterial = true;
                        Material = *LoadedMaterial;
                        if (Material.BaseColorTexture.IsValid())
                        {
                            BaseColorTexture =
                                GetTextureResource(Material.BaseColorTexture);
                        }
                    }
                }
                else
                {
                    continue;
                }

                const FMatrix4 Model = ModelTransform.ToMatrix();
                const FVector3 Color = bHasMaterial
                    ? Material.BaseColor : Primitive->GetColor();
                Impl->PickHandles.push_back(Primitive->GetHandle());
                const GLuint PickingId =
                    static_cast<GLuint>(Impl->PickHandles.size());
                glUniformMatrix4fv(ModelLocation, 1, GL_TRUE, Model.GetData());
                glUniform3f(ColorLocation, Color.X, Color.Y, Color.Z);
                glUniform1f(MetallicLocation, Material.Metallic);
                glUniform1f(RoughnessLocation, Material.Roughness);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, BaseColorTexture);
                glUniform1i(UseTextureLocation, BaseColorTexture != 0 ? 1 : 0);
                glUniform1ui(PickingIdLocation, PickingId);
                glDrawElements(
                    GL_TRIANGLES,
                    IndexCount,
                    GL_UNSIGNED_INT,
                    nullptr);

                const bool bSelected = std::find(
                        SelectedObjects.begin(),
                        SelectedObjects.end(),
                        Primitive->GetHandle()) != SelectedObjects.end()
                    || std::find(
                        SelectedObjects.begin(),
                        SelectedObjects.end(),
                        Actor->GetHandle()) != SelectedObjects.end();
                if (bSelected)
                {
                    FTransform OutlineTransform = ModelTransform;
                    OutlineTransform.Scale *= 1.02f;
                    const FMatrix4 OutlineModel = OutlineTransform.ToMatrix();
                    glUniformMatrix4fv(
                        ModelLocation,
                        1,
                        GL_TRUE,
                        OutlineModel.GetData());
                    glUniform3f(ColorLocation, 1.0f, 1.0f, 1.0f);
                    glUniform1i(UseTextureLocation, 0);
                    glUniform1i(LightingLocation, 0);
                    const bool bCube = Component->IsA(PCubeComponent::StaticClass());
                    if (bCube)
                    {
                        glBindVertexArray(Impl->CubeOutlineVertexArray);
                    }
                    else
                    {
                        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                    }
                    glLineWidth(2.0f);
                    glDrawElements(
                        bCube ? GL_LINES : GL_TRIANGLES,
                        bCube
                            ? static_cast<GLsizei>(CubeOutlineIndices.size())
                            : IndexCount,
                        GL_UNSIGNED_INT,
                        nullptr);
                    if (!bCube)
                    {
                        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                    }
                    glUniform1i(LightingLocation, 1);
                }
            }
        }
    }

    if (bDrawComponentVisualizations)
    {
        glUniformMatrix4fv(ModelLocation, 1, GL_TRUE, Identity.GetData());
        glUniform1i(UseTextureLocation, 0);
        glUniform1i(LightingLocation, 0);
        glBindVertexArray(Impl->ComponentVisualizationVertexArray);
        for (PLevel* Level : World->GetLevels())
        {
            if (Level == nullptr) continue;
            for (PActor* Actor : Level->GetActors())
            {
                if (Actor == nullptr) continue;
                for (PActorComponent* Component : Actor->GetComponents())
                {
                    const PSceneComponent* SceneComponent = Component != nullptr
                        && Component->IsA(PSceneComponent::StaticClass())
                        ? static_cast<const PSceneComponent*>(Component) : nullptr;
                    const bool bSelected = std::find(
                            SelectedObjects.begin(),
                            SelectedObjects.end(),
                            Component->GetHandle()) != SelectedObjects.end()
                        || std::find(
                            SelectedObjects.begin(),
                            SelectedObjects.end(),
                            Actor->GetHandle()) != SelectedObjects.end();
                    FComponentVisualization Visualization =
                        BuildComponentVisualization(
                            SceneComponent,
                            View,
                            Aspect,
                            bSelected);
                    if (Visualization.Vertices.empty()) continue;

                    const FVector3 Color = bSelected
                        ? FVector3::OneVector : Visualization.Color;
                    Impl->PickHandles.push_back(Component->GetHandle());
                    const GLuint PickingId =
                        static_cast<GLuint>(Impl->PickHandles.size());
                    glBindBuffer(
                        GL_ARRAY_BUFFER,
                        Impl->ComponentVisualizationVertexBuffer);
                    glBufferData(
                        GL_ARRAY_BUFFER,
                        static_cast<std::ptrdiff_t>(
                            Visualization.Vertices.size() * sizeof(FVector3)),
                        Visualization.Vertices.data(),
                        GL_DYNAMIC_DRAW);
                    glUniform3f(ColorLocation, Color.X, Color.Y, Color.Z);
                    glUniform1ui(PickingIdLocation, PickingId);
                    glLineWidth(bSelected ? 3.0f : 2.0f);
                    glDrawArrays(
                        GL_LINES,
                        0,
                        static_cast<GLsizei>(Visualization.Vertices.size()));
                }
            }
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool FSceneViewportRenderer::PresentToBackBuffer(uint32 Width, uint32 Height) const
{
    if (!Impl->bInitialized
        || Impl->Framebuffer == 0
        || Width == 0
        || Height == 0)
    {
        return false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, Impl->Framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(
        0,
        0,
        static_cast<GLint>(Impl->Width),
        static_cast<GLint>(Impl->Height),
        0,
        0,
        static_cast<GLint>(Width),
        static_cast<GLint>(Height),
        GL_COLOR_BUFFER_BIT,
        GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

FObjectHandle FSceneViewportRenderer::Pick(uint32 X, uint32 Y) const
{
    if (!Impl->bInitialized
        || Impl->Framebuffer == 0
        || X >= Impl->Width
        || Y >= Impl->Height)
    {
        return {};
    }

    GLuint PickingId = 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, Impl->Framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glReadPixels(
        static_cast<GLint>(X),
        static_cast<GLint>(Y),
        1,
        1,
        GL_RED_INTEGER,
        GL_UNSIGNED_INT,
        &PickingId);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    if (PickingId == 0 || PickingId > Impl->PickHandles.size())
    {
        return {};
    }
    return Impl->PickHandles[PickingId - 1];
}

uint32 FSceneViewportRenderer::GetColorTexture() const
{
    return Impl->ColorTexture;
}

uint32 FSceneViewportRenderer::GetWidth() const
{
    return Impl->Width;
}

uint32 FSceneViewportRenderer::GetHeight() const
{
    return Impl->Height;
}

bool FSceneViewportRenderer::IsInitialized() const
{
    return Impl->bInitialized;
}
}
