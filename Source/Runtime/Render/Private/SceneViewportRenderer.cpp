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

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace Pico
{
namespace
{
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_RGBA8 0x8058
#define GL_CLAMP_TO_EDGE 0x812F
#endif

using FGlGenVertexArrays = void (APIENTRY*)(GLsizei, GLuint*);
using FGlBindVertexArray = void (APIENTRY*)(GLuint);
using FGlDeleteVertexArrays = void (APIENTRY*)(GLsizei, const GLuint*);
using FGlGenBuffers = void (APIENTRY*)(GLsizei, GLuint*);
using FGlBindBuffer = void (APIENTRY*)(GLenum, GLuint);
using FGlBufferData = void (APIENTRY*)(GLenum, std::ptrdiff_t, const void*, GLenum);
using FGlDeleteBuffers = void (APIENTRY*)(GLsizei, const GLuint*);
using FGlEnableVertexAttribArray = void (APIENTRY*)(GLuint);
using FGlVertexAttribPointer = void (APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using FGlCreateShader = GLuint (APIENTRY*)(GLenum);
using FGlShaderSource = void (APIENTRY*)(GLuint, GLsizei, const char* const*, const GLint*);
using FGlCompileShader = void (APIENTRY*)(GLuint);
using FGlGetShaderiv = void (APIENTRY*)(GLuint, GLenum, GLint*);
using FGlGetShaderInfoLog = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using FGlDeleteShader = void (APIENTRY*)(GLuint);
using FGlCreateProgram = GLuint (APIENTRY*)();
using FGlAttachShader = void (APIENTRY*)(GLuint, GLuint);
using FGlLinkProgram = void (APIENTRY*)(GLuint);
using FGlGetProgramiv = void (APIENTRY*)(GLuint, GLenum, GLint*);
using FGlGetProgramInfoLog = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using FGlDeleteProgram = void (APIENTRY*)(GLuint);
using FGlUseProgram = void (APIENTRY*)(GLuint);
using FGlGetUniformLocation = GLint (APIENTRY*)(GLuint, const char*);
using FGlUniformMatrix4fv = void (APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
using FGlUniform3f = void (APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat);
using FGlUniform1i = void (APIENTRY*)(GLint, GLint);
using FGlGenFramebuffers = void (APIENTRY*)(GLsizei, GLuint*);
using FGlBindFramebuffer = void (APIENTRY*)(GLenum, GLuint);
using FGlFramebufferTexture2D = void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using FGlCheckFramebufferStatus = GLenum (APIENTRY*)(GLenum);
using FGlDeleteFramebuffers = void (APIENTRY*)(GLsizei, const GLuint*);
using FGlGenRenderbuffers = void (APIENTRY*)(GLsizei, GLuint*);
using FGlBindRenderbuffer = void (APIENTRY*)(GLenum, GLuint);
using FGlRenderbufferStorage = void (APIENTRY*)(GLenum, GLenum, GLsizei, GLsizei);
using FGlFramebufferRenderbuffer = void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
using FGlDeleteRenderbuffers = void (APIENTRY*)(GLsizei, const GLuint*);

template <typename T>
bool LoadProcedure(FOpenGLProcLoader Loader, const char* Name, T& OutProcedure)
{
    OutProcedure = reinterpret_cast<T>(Loader(Name));
    if (OutProcedure == nullptr)
    {
        PICO_LOG(LogRender, Error, "OpenGL procedure '{}' is unavailable", Name);
        return false;
    }
    return true;
}

FMatrix4 MakePerspective(float FieldOfViewDegrees, float Aspect, float NearPlane, float FarPlane)
{
    const float HalfFovRadians = DegreesToRadians(FieldOfViewDegrees) * 0.5f;
    const float FocalLength = 1.0f / std::tan(HalfFovRadians);

    FMatrix4 Result;
    Result[0][0] = FocalLength / Aspect;
    Result[1][1] = FocalLength;
    Result[2][2] = (FarPlane + NearPlane) / (NearPlane - FarPlane);
    Result[2][3] = (2.0f * FarPlane * NearPlane) / (NearPlane - FarPlane);
    Result[3][2] = -1.0f;
    return Result;
}

FMatrix4 MakeLookAt(const FVector3& Eye, const FVector3& Target, const FVector3& Up)
{
    const FVector3 Forward = (Target - Eye).GetSafeNormal();
    FVector3 Right = FVector3::Cross(Forward, Up).GetSafeNormal();
    if (Right.IsNearlyZero())
    {
        Right = FVector3::RightVector;
    }
    const FVector3 CameraUp = FVector3::Cross(Right, Forward).GetSafeNormal();

    FMatrix4 Result = FMatrix4::Identity;
    Result[0][0] = Right.X;
    Result[0][1] = Right.Y;
    Result[0][2] = Right.Z;
    Result[0][3] = -FVector3::Dot(Right, Eye);
    Result[1][0] = CameraUp.X;
    Result[1][1] = CameraUp.Y;
    Result[1][2] = CameraUp.Z;
    Result[1][3] = -FVector3::Dot(CameraUp, Eye);
    Result[2][0] = -Forward.X;
    Result[2][1] = -Forward.Y;
    Result[2][2] = -Forward.Z;
    Result[2][3] = FVector3::Dot(Forward, Eye);
    return Result;
}

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
}

struct FSceneViewportRenderer::FImpl
{
    FGlGenVertexArrays GenVertexArrays = nullptr;
    FGlBindVertexArray BindVertexArray = nullptr;
    FGlDeleteVertexArrays DeleteVertexArrays = nullptr;
    FGlGenBuffers GenBuffers = nullptr;
    FGlBindBuffer BindBuffer = nullptr;
    FGlBufferData BufferData = nullptr;
    FGlDeleteBuffers DeleteBuffers = nullptr;
    FGlEnableVertexAttribArray EnableVertexAttribArray = nullptr;
    FGlVertexAttribPointer VertexAttribPointer = nullptr;
    FGlCreateShader CreateShader = nullptr;
    FGlShaderSource ShaderSource = nullptr;
    FGlCompileShader CompileShader = nullptr;
    FGlGetShaderiv GetShaderiv = nullptr;
    FGlGetShaderInfoLog GetShaderInfoLog = nullptr;
    FGlDeleteShader DeleteShader = nullptr;
    FGlCreateProgram CreateProgram = nullptr;
    FGlAttachShader AttachShader = nullptr;
    FGlLinkProgram LinkProgram = nullptr;
    FGlGetProgramiv GetProgramiv = nullptr;
    FGlGetProgramInfoLog GetProgramInfoLog = nullptr;
    FGlDeleteProgram DeleteProgram = nullptr;
    FGlUseProgram UseProgram = nullptr;
    FGlGetUniformLocation GetUniformLocation = nullptr;
    FGlUniformMatrix4fv UniformMatrix4fv = nullptr;
    FGlUniform3f Uniform3f = nullptr;
    FGlUniform1i Uniform1i = nullptr;
    FGlGenFramebuffers GenFramebuffers = nullptr;
    FGlBindFramebuffer BindFramebuffer = nullptr;
    FGlFramebufferTexture2D FramebufferTexture2D = nullptr;
    FGlCheckFramebufferStatus CheckFramebufferStatus = nullptr;
    FGlDeleteFramebuffers DeleteFramebuffers = nullptr;
    FGlGenRenderbuffers GenRenderbuffers = nullptr;
    FGlBindRenderbuffer BindRenderbuffer = nullptr;
    FGlRenderbufferStorage RenderbufferStorage = nullptr;
    FGlFramebufferRenderbuffer FramebufferRenderbuffer = nullptr;
    FGlDeleteRenderbuffers DeleteRenderbuffers = nullptr;

    GLuint Program = 0;
    GLuint CubeVertexArray = 0;
    GLuint CubeVertexBuffer = 0;
    GLuint CubeIndexBuffer = 0;
    GLuint GridVertexArray = 0;
    GLuint GridVertexBuffer = 0;
    GLsizei GridVertexCount = 0;
    GLuint Framebuffer = 0;
    GLuint ColorTexture = 0;
    GLuint DepthRenderbuffer = 0;
    uint32 Width = 0;
    uint32 Height = 0;
    bool bInitialized = false;

    bool Load(FOpenGLProcLoader Loader)
    {
        return Loader != nullptr
            && LoadProcedure(Loader, "glGenVertexArrays", GenVertexArrays)
            && LoadProcedure(Loader, "glBindVertexArray", BindVertexArray)
            && LoadProcedure(Loader, "glDeleteVertexArrays", DeleteVertexArrays)
            && LoadProcedure(Loader, "glGenBuffers", GenBuffers)
            && LoadProcedure(Loader, "glBindBuffer", BindBuffer)
            && LoadProcedure(Loader, "glBufferData", BufferData)
            && LoadProcedure(Loader, "glDeleteBuffers", DeleteBuffers)
            && LoadProcedure(Loader, "glEnableVertexAttribArray", EnableVertexAttribArray)
            && LoadProcedure(Loader, "glVertexAttribPointer", VertexAttribPointer)
            && LoadProcedure(Loader, "glCreateShader", CreateShader)
            && LoadProcedure(Loader, "glShaderSource", ShaderSource)
            && LoadProcedure(Loader, "glCompileShader", CompileShader)
            && LoadProcedure(Loader, "glGetShaderiv", GetShaderiv)
            && LoadProcedure(Loader, "glGetShaderInfoLog", GetShaderInfoLog)
            && LoadProcedure(Loader, "glDeleteShader", DeleteShader)
            && LoadProcedure(Loader, "glCreateProgram", CreateProgram)
            && LoadProcedure(Loader, "glAttachShader", AttachShader)
            && LoadProcedure(Loader, "glLinkProgram", LinkProgram)
            && LoadProcedure(Loader, "glGetProgramiv", GetProgramiv)
            && LoadProcedure(Loader, "glGetProgramInfoLog", GetProgramInfoLog)
            && LoadProcedure(Loader, "glDeleteProgram", DeleteProgram)
            && LoadProcedure(Loader, "glUseProgram", UseProgram)
            && LoadProcedure(Loader, "glGetUniformLocation", GetUniformLocation)
            && LoadProcedure(Loader, "glUniformMatrix4fv", UniformMatrix4fv)
            && LoadProcedure(Loader, "glUniform3f", Uniform3f)
            && LoadProcedure(Loader, "glUniform1i", Uniform1i)
            && LoadProcedure(Loader, "glGenFramebuffers", GenFramebuffers)
            && LoadProcedure(Loader, "glBindFramebuffer", BindFramebuffer)
            && LoadProcedure(Loader, "glFramebufferTexture2D", FramebufferTexture2D)
            && LoadProcedure(Loader, "glCheckFramebufferStatus", CheckFramebufferStatus)
            && LoadProcedure(Loader, "glDeleteFramebuffers", DeleteFramebuffers)
            && LoadProcedure(Loader, "glGenRenderbuffers", GenRenderbuffers)
            && LoadProcedure(Loader, "glBindRenderbuffer", BindRenderbuffer)
            && LoadProcedure(Loader, "glRenderbufferStorage", RenderbufferStorage)
            && LoadProcedure(Loader, "glFramebufferRenderbuffer", FramebufferRenderbuffer)
            && LoadProcedure(Loader, "glDeleteRenderbuffers", DeleteRenderbuffers);
    }

    GLuint Compile(GLenum Type, const char* Source)
    {
        const GLuint Shader = CreateShader(Type);
        ShaderSource(Shader, 1, &Source, nullptr);
        CompileShader(Shader);

        GLint bCompiled = GL_FALSE;
        GetShaderiv(Shader, GL_COMPILE_STATUS, &bCompiled);
        if (bCompiled == GL_TRUE)
        {
            return Shader;
        }

        GLint Length = 0;
        GetShaderiv(Shader, GL_INFO_LOG_LENGTH, &Length);
        std::string Log(static_cast<std::size_t>(std::max(Length, 1)), '\0');
        GetShaderInfoLog(Shader, Length, nullptr, Log.data());
        PICO_LOG(LogRender, Error, "OpenGL shader compilation failed: {}", Log);
        DeleteShader(Shader);
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
out vec4 FragColor;
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
}
)";

        const GLuint VertexShader = Compile(GL_VERTEX_SHADER, VertexSource);
        const GLuint FragmentShader = Compile(GL_FRAGMENT_SHADER, FragmentSource);
        if (VertexShader == 0 || FragmentShader == 0)
        {
            if (VertexShader != 0)
            {
                DeleteShader(VertexShader);
            }
            if (FragmentShader != 0)
            {
                DeleteShader(FragmentShader);
            }
            return false;
        }

        Program = CreateProgram();
        AttachShader(Program, VertexShader);
        AttachShader(Program, FragmentShader);
        LinkProgram(Program);
        DeleteShader(VertexShader);
        DeleteShader(FragmentShader);

        GLint bLinked = GL_FALSE;
        GetProgramiv(Program, GL_LINK_STATUS, &bLinked);
        if (bLinked != GL_TRUE)
        {
            GLint Length = 0;
            GetProgramiv(Program, GL_INFO_LOG_LENGTH, &Length);
            std::string Log(static_cast<std::size_t>(std::max(Length, 1)), '\0');
            GetProgramInfoLog(Program, Length, nullptr, Log.data());
            PICO_LOG(LogRender, Error, "OpenGL program linking failed: {}", Log);
            return false;
        }

        GenVertexArrays(1, &CubeVertexArray);
        BindVertexArray(CubeVertexArray);
        GenBuffers(1, &CubeVertexBuffer);
        BindBuffer(GL_ARRAY_BUFFER, CubeVertexBuffer);
        BufferData(
            GL_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(CubeVertices.size() * sizeof(float)),
            CubeVertices.data(),
            GL_STATIC_DRAW);
        GenBuffers(1, &CubeIndexBuffer);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, CubeIndexBuffer);
        BufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(CubeIndices.size() * sizeof(unsigned int)),
            CubeIndices.data(),
            GL_STATIC_DRAW);
        EnableVertexAttribArray(0);
        VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        EnableVertexAttribArray(1);
        VertexAttribPointer(
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

        GenVertexArrays(1, &GridVertexArray);
        BindVertexArray(GridVertexArray);
        GenBuffers(1, &GridVertexBuffer);
        BindBuffer(GL_ARRAY_BUFFER, GridVertexBuffer);
        BufferData(
            GL_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(GridVertices.size() * sizeof(float)),
            GridVertices.data(),
            GL_STATIC_DRAW);
        EnableVertexAttribArray(0);
        VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        EnableVertexAttribArray(1);
        VertexAttribPointer(
            1,
            3,
            GL_FLOAT,
            GL_FALSE,
            6 * sizeof(float),
            reinterpret_cast<const void*>(3 * sizeof(float)));
        BindVertexArray(0);
        return true;
    }

    void DestroyRenderTarget()
    {
        if (DepthRenderbuffer != 0)
        {
            DeleteRenderbuffers(1, &DepthRenderbuffer);
            DepthRenderbuffer = 0;
        }
        if (ColorTexture != 0)
        {
            glDeleteTextures(1, &ColorTexture);
            ColorTexture = 0;
        }
        if (Framebuffer != 0)
        {
            DeleteFramebuffers(1, &Framebuffer);
            Framebuffer = 0;
        }
        Width = 0;
        Height = 0;
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
    if (!Impl->Load(Loader) || !Impl->CreateProgramAndGeometry())
    {
        Shutdown();
        return false;
    }
    Impl->bInitialized = true;
    PICO_LOG(LogRender, Info, "OpenGL scene viewport renderer initialized");
    return true;
}

void FSceneViewportRenderer::Shutdown()
{
    Impl->DestroyRenderTarget();
    if (Impl->GridVertexBuffer != 0)
    {
        Impl->DeleteBuffers(1, &Impl->GridVertexBuffer);
        Impl->GridVertexBuffer = 0;
    }
    if (Impl->GridVertexArray != 0)
    {
        Impl->DeleteVertexArrays(1, &Impl->GridVertexArray);
        Impl->GridVertexArray = 0;
    }
    if (Impl->CubeIndexBuffer != 0)
    {
        Impl->DeleteBuffers(1, &Impl->CubeIndexBuffer);
        Impl->CubeIndexBuffer = 0;
    }
    if (Impl->CubeVertexBuffer != 0)
    {
        Impl->DeleteBuffers(1, &Impl->CubeVertexBuffer);
        Impl->CubeVertexBuffer = 0;
    }
    if (Impl->CubeVertexArray != 0)
    {
        Impl->DeleteVertexArrays(1, &Impl->CubeVertexArray);
        Impl->CubeVertexArray = 0;
    }
    if (Impl->Program != 0)
    {
        Impl->DeleteProgram(Impl->Program);
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
    Impl->GenFramebuffers(1, &Impl->Framebuffer);
    Impl->BindFramebuffer(GL_FRAMEBUFFER, Impl->Framebuffer);

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
    Impl->FramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        Impl->ColorTexture,
        0);

    Impl->GenRenderbuffers(1, &Impl->DepthRenderbuffer);
    Impl->BindRenderbuffer(GL_RENDERBUFFER, Impl->DepthRenderbuffer);
    Impl->RenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH24_STENCIL8,
        static_cast<GLsizei>(Width),
        static_cast<GLsizei>(Height));
    Impl->FramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        Impl->DepthRenderbuffer);

    const bool bComplete =
        Impl->CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    Impl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!bComplete)
    {
        PICO_LOG(LogRender, Error, "Scene viewport framebuffer is incomplete");
        Impl->DestroyRenderTarget();
    }
    return bComplete;
}

bool FSceneViewportRenderer::Render(PWorld* World, const FSceneView& View)
{
    if (!Impl->bInitialized || Impl->Framebuffer == 0 || World == nullptr)
    {
        return false;
    }

    const float Aspect = static_cast<float>(Impl->Width) / static_cast<float>(Impl->Height);
    const FMatrix4 Projection = MakePerspective(
        View.VerticalFieldOfViewDegrees,
        Aspect,
        View.NearPlane,
        View.FarPlane);
    const FMatrix4 ViewMatrix = MakeLookAt(View.Position, View.Target, View.Up);
    const FMatrix4 ViewProjection = Projection * ViewMatrix;

    Impl->BindFramebuffer(GL_FRAMEBUFFER, Impl->Framebuffer);
    glViewport(0, 0, static_cast<GLsizei>(Impl->Width), static_cast<GLsizei>(Impl->Height));
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glClearColor(0.075f, 0.085f, 0.095f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Impl->UseProgram(Impl->Program);

    const GLint ViewProjectionLocation =
        Impl->GetUniformLocation(Impl->Program, "ViewProjection");
    const GLint ModelLocation = Impl->GetUniformLocation(Impl->Program, "Model");
    const GLint ColorLocation = Impl->GetUniformLocation(Impl->Program, "BaseColor");
    const GLint LightingLocation = Impl->GetUniformLocation(Impl->Program, "UseLighting");
    Impl->UniformMatrix4fv(
        ViewProjectionLocation,
        1,
        GL_TRUE,
        ViewProjection.GetData());

    const FMatrix4 Identity = FMatrix4::Identity;
    Impl->UniformMatrix4fv(ModelLocation, 1, GL_TRUE, Identity.GetData());
    Impl->Uniform3f(ColorLocation, 0.25f, 0.29f, 0.31f);
    Impl->Uniform1i(LightingLocation, 0);
    Impl->BindVertexArray(Impl->GridVertexArray);
    glLineWidth(1.0f);
    glDrawArrays(GL_LINES, 0, Impl->GridVertexCount);

    Impl->BindVertexArray(Impl->CubeVertexArray);
    Impl->Uniform1i(LightingLocation, 1);
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
                Impl->UniformMatrix4fv(ModelLocation, 1, GL_TRUE, Model.GetData());
                Impl->Uniform3f(ColorLocation, Color.X, Color.Y, Color.Z);
                glDrawElements(
                    GL_TRIANGLES,
                    static_cast<GLsizei>(CubeIndices.size()),
                    GL_UNSIGNED_INT,
                    nullptr);
            }
        }
    }

    Impl->BindVertexArray(0);
    Impl->UseProgram(0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    Impl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
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
