#include "Pico/Render/SceneViewportRenderer.h"

#include "Pico/Core/Log.h"
#include "Pico/Core/Math/MathUtility.h"
#include "Pico/Core/Math/Matrix4.h"
#include "Pico/Core/Math/Transform.h"
#include "Pico/Engine/Actor.h"
#include "Pico/Engine/ActorComponent.h"
#include "Pico/Engine/CubeComponent.h"
#include "Pico/Engine/Level.h"
#include "Pico/Engine/World.h"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
}

struct FSceneViewportRenderer::FImpl
{
    GLuint Program = 0;
    GLuint CubeVertexArray = 0;
    GLuint CubeVertexBuffer = 0;
    GLuint CubeIndexBuffer = 0;
    GLuint CubeOutlineVertexArray = 0;
    GLuint CubeOutlineIndexBuffer = 0;
    GLuint GridVertexArray = 0;
    GLuint GridVertexBuffer = 0;
    GLsizei GridVertexCount = 0;
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
uniform mat4 ViewProjection;
uniform mat4 Model;
out vec3 WorldNormal;
void main()
{
    vec4 WorldPosition = Model * vec4(InPosition, 1.0);
    WorldNormal = mat3(Model) * InNormal;
    gl_Position = ViewProjection * WorldPosition;
}
)";
        static constexpr char FragmentSource[] = R"(
#version 330 core
in vec3 WorldNormal;
uniform vec3 BaseColor;
uniform int UseLighting;
uniform uint PickingId;
layout(location = 0) out vec4 FragColor;
layout(location = 1) out uint FragPickingId;
void main()
{
    float Lighting = 1.0;
    if (UseLighting != 0)
    {
        vec3 Normal = normalize(WorldNormal);
        vec3 LightDirection = normalize(vec3(0.45, -0.55, 0.8));
        Lighting = 0.32 + 0.68 * max(dot(Normal, LightDirection), 0.0);
    }
    FragColor = vec4(BaseColor * Lighting, 1.0);
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
    const FSceneView& View,
    std::span<const FObjectHandle> SelectedObjects)
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
    const GLint LightingLocation = glGetUniformLocation(Impl->Program, "UseLighting");
    const GLint PickingIdLocation = glGetUniformLocation(Impl->Program, "PickingId");
    glUniformMatrix4fv(
        ViewProjectionLocation,
        1,
        GL_TRUE,
        ViewProjection.GetData());

    const FMatrix4 Identity = FMatrix4::Identity;
    glUniformMatrix4fv(ModelLocation, 1, GL_TRUE, Identity.GetData());
    glUniform3f(ColorLocation, 0.25f, 0.29f, 0.31f);
    glUniform1i(LightingLocation, 0);
    glUniform1ui(PickingIdLocation, 0);
    glBindVertexArray(Impl->GridVertexArray);
    glLineWidth(1.0f);
    glDrawArrays(GL_LINES, 0, Impl->GridVertexCount);

    glBindVertexArray(Impl->CubeVertexArray);
    glUniform1i(LightingLocation, 1);
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
                if (Component == nullptr || !Component->IsA(PCubeComponent::StaticClass()))
                {
                    continue;
                }

                PCubeComponent* Cube = static_cast<PCubeComponent*>(Component);
                if (!Cube->IsVisible())
                {
                    continue;
                }

                FTransform ModelTransform = Cube->GetWorldTransform();
                ModelTransform.Scale = ModelTransform.Scale * Cube->GetExtent();
                const FMatrix4 Model = ModelTransform.ToMatrix();
                const FVector3 Color = Cube->GetColor();
                Impl->PickHandles.push_back(Cube->GetHandle());
                const GLuint PickingId =
                    static_cast<GLuint>(Impl->PickHandles.size());
                glUniformMatrix4fv(ModelLocation, 1, GL_TRUE, Model.GetData());
                glUniform3f(ColorLocation, Color.X, Color.Y, Color.Z);
                glUniform1ui(PickingIdLocation, PickingId);
                glDrawElements(
                    GL_TRIANGLES,
                    static_cast<GLsizei>(CubeIndices.size()),
                    GL_UNSIGNED_INT,
                    nullptr);

                const bool bSelected = std::find(
                        SelectedObjects.begin(),
                        SelectedObjects.end(),
                        Cube->GetHandle()) != SelectedObjects.end()
                    || std::find(
                        SelectedObjects.begin(),
                        SelectedObjects.end(),
                        Actor->GetHandle()) != SelectedObjects.end();
                if (bSelected)
                {
                    FTransform OutlineTransform = Cube->GetWorldTransform();
                    OutlineTransform.Scale =
                        OutlineTransform.Scale * Cube->GetExtent() * 1.02f;
                    const FMatrix4 OutlineModel = OutlineTransform.ToMatrix();
                    glUniformMatrix4fv(
                        ModelLocation,
                        1,
                        GL_TRUE,
                        OutlineModel.GetData());
                    glUniform3f(ColorLocation, 1.0f, 1.0f, 1.0f);
                    glUniform1i(LightingLocation, 0);
                    glBindVertexArray(Impl->CubeOutlineVertexArray);
                    glLineWidth(2.0f);
                    glDrawElements(
                        GL_LINES,
                        static_cast<GLsizei>(CubeOutlineIndices.size()),
                        GL_UNSIGNED_INT,
                        nullptr);
                    glBindVertexArray(Impl->CubeVertexArray);
                    glUniform1i(LightingLocation, 1);
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
