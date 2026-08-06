#include "Pico/AssetImport/StaticMeshImporter.h"

#include "tiny_obj_loader.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace Pico
{
namespace
{
void ReportError(EStaticMeshImportError* OutError, EStaticMeshImportError Error)
{
    if (OutError != nullptr)
    {
        *OutError = Error;
    }
}

FVector3 ConvertVector(
    float X,
    float Y,
    float Z,
    const FStaticMeshImportOptions& Options,
    bool bApplyScale)
{
    const float Scale = bApplyScale ? Options.UniformScale : 1.0f;
    return Options.bConvertYUpToZUp
        ? FVector3(X * Scale, -Z * Scale, Y * Scale)
        : FVector3(X * Scale, Y * Scale, Z * Scale);
}
}

bool ImportObjStaticMesh(
    const std::filesystem::path& SourceFile,
    const FStaticMeshImportOptions& Options,
    FStaticMeshImportResult& OutResult,
    EStaticMeshImportError* OutError)
{
    ReportError(OutError, EStaticMeshImportError::None);
    if (SourceFile.empty()
        || !std::isfinite(Options.UniformScale)
        || Options.UniformScale <= 0.0f)
    {
        ReportError(OutError, EStaticMeshImportError::InvalidArgument);
        return false;
    }

    tinyobj::ObjReaderConfig Config;
    Config.triangulate = true;
    Config.vertex_color = false;
    Config.mtl_search_path = SourceFile.parent_path().string();
    tinyobj::ObjReader Reader;
    if (!Reader.ParseFromFile(SourceFile.string(), Config))
    {
        ReportError(
            OutError,
            Reader.Error().empty()
                ? EStaticMeshImportError::SourceOpenFailed
                : EStaticMeshImportError::ParseFailed);
        return false;
    }

    const tinyobj::attrib_t& Attributes = Reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& Shapes = Reader.GetShapes();
    FStaticMeshData Mesh;
    std::map<std::tuple<int, int, int>, uint32> VertexLookup;
    bool bNeedsGeneratedNormals = false;
    for (const tinyobj::shape_t& Shape : Shapes)
    {
        const uint32 FirstIndex = static_cast<uint32>(Mesh.Indices.size());
        for (const tinyobj::index_t& Index : Shape.mesh.indices)
        {
            if (Index.vertex_index < 0
                || static_cast<std::size_t>(Index.vertex_index) * 3 + 2
                    >= Attributes.vertices.size())
            {
                ReportError(OutError, EStaticMeshImportError::InvalidMesh);
                return false;
            }
            const auto Key = std::make_tuple(
                Index.vertex_index,
                Index.normal_index,
                Index.texcoord_index);
            const auto Existing = VertexLookup.find(Key);
            if (Existing != VertexLookup.end())
            {
                Mesh.Indices.push_back(Existing->second);
                continue;
            }

            const std::size_t PositionOffset =
                static_cast<std::size_t>(Index.vertex_index) * 3;
            FStaticMeshVertex Vertex;
            Vertex.Position = ConvertVector(
                Attributes.vertices[PositionOffset],
                Attributes.vertices[PositionOffset + 1],
                Attributes.vertices[PositionOffset + 2],
                Options,
                true);
            if (Index.normal_index >= 0)
            {
                const std::size_t NormalOffset =
                    static_cast<std::size_t>(Index.normal_index) * 3;
                if (NormalOffset + 2 >= Attributes.normals.size())
                {
                    ReportError(OutError, EStaticMeshImportError::InvalidMesh);
                    return false;
                }
                Vertex.Normal = ConvertVector(
                    Attributes.normals[NormalOffset],
                    Attributes.normals[NormalOffset + 1],
                    Attributes.normals[NormalOffset + 2],
                    Options,
                    false).GetSafeNormal();
            }
            else
            {
                bNeedsGeneratedNormals = true;
            }
            if (Index.texcoord_index >= 0)
            {
                const std::size_t TexCoordOffset =
                    static_cast<std::size_t>(Index.texcoord_index) * 2;
                if (TexCoordOffset + 1 >= Attributes.texcoords.size())
                {
                    ReportError(OutError, EStaticMeshImportError::InvalidMesh);
                    return false;
                }
                const float V = Attributes.texcoords[TexCoordOffset + 1];
                Vertex.TexCoord = FVector2(
                    Attributes.texcoords[TexCoordOffset],
                    Options.bFlipTexCoordV ? 1.0f - V : V);
            }
            const uint32 NewIndex = static_cast<uint32>(Mesh.Vertices.size());
            Mesh.Vertices.push_back(Vertex);
            VertexLookup.emplace(Key, NewIndex);
            Mesh.Indices.push_back(NewIndex);
        }
        const uint32 IndexCount =
            static_cast<uint32>(Mesh.Indices.size()) - FirstIndex;
        if (IndexCount > 0)
        {
            Mesh.Sections.push_back(FStaticMeshSection {
                FirstIndex,
                IndexCount,
                Shape.name.empty() ? "Default" : Shape.name
            });
        }
    }

    if (Mesh.Vertices.empty() || Mesh.Indices.empty())
    {
        ReportError(OutError, EStaticMeshImportError::EmptyMesh);
        return false;
    }
    if (bNeedsGeneratedNormals)
    {
        for (FStaticMeshVertex& Vertex : Mesh.Vertices)
        {
            Vertex.Normal = FVector3::ZeroVector;
        }
        for (std::size_t Index = 0; Index + 2 < Mesh.Indices.size(); Index += 3)
        {
            FStaticMeshVertex& A = Mesh.Vertices[Mesh.Indices[Index]];
            FStaticMeshVertex& B = Mesh.Vertices[Mesh.Indices[Index + 1]];
            FStaticMeshVertex& C = Mesh.Vertices[Mesh.Indices[Index + 2]];
            const FVector3 FaceNormal = FVector3::Cross(
                B.Position - A.Position,
                C.Position - A.Position).GetSafeNormal();
            A.Normal += FaceNormal;
            B.Normal += FaceNormal;
            C.Normal += FaceNormal;
        }
        for (FStaticMeshVertex& Vertex : Mesh.Vertices)
        {
            Vertex.Normal = Vertex.Normal.GetSafeNormal();
        }
    }

    Mesh.Bounds.Min = Mesh.Vertices.front().Position;
    Mesh.Bounds.Max = Mesh.Vertices.front().Position;
    for (const FStaticMeshVertex& Vertex : Mesh.Vertices)
    {
        Mesh.Bounds.Min.X = std::min(Mesh.Bounds.Min.X, Vertex.Position.X);
        Mesh.Bounds.Min.Y = std::min(Mesh.Bounds.Min.Y, Vertex.Position.Y);
        Mesh.Bounds.Min.Z = std::min(Mesh.Bounds.Min.Z, Vertex.Position.Z);
        Mesh.Bounds.Max.X = std::max(Mesh.Bounds.Max.X, Vertex.Position.X);
        Mesh.Bounds.Max.Y = std::max(Mesh.Bounds.Max.Y, Vertex.Position.Y);
        Mesh.Bounds.Max.Z = std::max(Mesh.Bounds.Max.Z, Vertex.Position.Z);
    }
    if (!ValidateStaticMesh(Mesh))
    {
        ReportError(OutError, EStaticMeshImportError::InvalidMesh);
        return false;
    }

    FStaticMeshImportResult Result;
    Result.Mesh = std::move(Mesh);
    Result.Warning = Reader.Warning();
    OutResult = std::move(Result);
    return true;
}

bool ImportObjStaticMeshToFile(
    const std::filesystem::path& SourceFile,
    const std::filesystem::path& DestinationFile,
    const FStaticMeshImportOptions& Options,
    EStaticMeshImportError* OutError)
{
    if (DestinationFile.empty())
    {
        ReportError(OutError, EStaticMeshImportError::InvalidArgument);
        return false;
    }
    FStaticMeshImportResult Result;
    if (!ImportObjStaticMesh(SourceFile, Options, Result, OutError))
    {
        return false;
    }
    EStaticMeshError SaveError = EStaticMeshError::None;
    if (!SaveStaticMeshToFile(DestinationFile, Result.Mesh, &SaveError))
    {
        ReportError(OutError, EStaticMeshImportError::SaveFailed);
        return false;
    }
    ReportError(OutError, EStaticMeshImportError::None);
    return true;
}

std::string_view ToString(EStaticMeshImportError Error)
{
    switch (Error)
    {
    case EStaticMeshImportError::None: return "None";
    case EStaticMeshImportError::InvalidArgument: return "InvalidArgument";
    case EStaticMeshImportError::SourceOpenFailed: return "SourceOpenFailed";
    case EStaticMeshImportError::ParseFailed: return "ParseFailed";
    case EStaticMeshImportError::EmptyMesh: return "EmptyMesh";
    case EStaticMeshImportError::InvalidMesh: return "InvalidMesh";
    case EStaticMeshImportError::SaveFailed: return "SaveFailed";
    }
    return "Unknown";
}
}
