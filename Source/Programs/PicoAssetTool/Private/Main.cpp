#include "Pico/AssetImport/StaticMeshImporter.h"

#include <filesystem>
#include <iostream>
#include <string_view>

int main(int Argc, char** Argv)
{
    if (Argc != 4 || std::string_view(Argv[1]) != "import-obj")
    {
        std::cerr << "Usage: PicoAssetTool import-obj <source.obj> <destination.pmesh>\n";
        return 1;
    }
    const std::filesystem::path Destination(Argv[3]);
    if (Destination.extension() != ".pmesh")
    {
        std::cerr << "Destination must use the .pmesh extension\n";
        return 1;
    }
    Pico::EStaticMeshImportError Error = Pico::EStaticMeshImportError::None;
    if (!Pico::ImportObjStaticMeshToFile(Argv[2], Destination, {}, &Error))
    {
        std::cerr << "Import failed: " << Pico::ToString(Error) << '\n';
        return 1;
    }
    std::cout << "Imported " << Argv[2] << " -> " << Destination.string() << '\n';
    return 0;
}
