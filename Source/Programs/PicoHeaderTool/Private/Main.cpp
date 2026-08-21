#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct FToken
{
    std::string Text;
    int Line = 1;
    int Column = 1;
};

struct FProperty
{
    std::string Name;
    std::vector<std::string> Specifiers;
    std::string RepNotify;
    std::string ReplicationCondition;
    std::string AssetReferenceType;
};

struct FFunction
{
    std::string Name;
    std::vector<std::string> Specifiers;
    std::vector<std::string> Parameters;
};

struct FClass
{
    std::string Namespace;
    std::string Name;
    std::string Super;
    int BodyLine = 0;
    std::vector<FProperty> Properties;
    std::vector<FFunction> Functions;
};

struct FOptions
{
    std::filesystem::path Input;
    std::filesystem::path HeaderOutput;
    std::filesystem::path SourceOutput;
    std::string IncludePath;
    std::string FileId;
};

class FDiagnostics
{
public:
    explicit FDiagnostics(std::filesystem::path InFile) : File(std::move(InFile)) {}

    void Error(const FToken& Token, std::string_view Code, std::string_view Message)
    {
        std::cerr << File.string() << '(' << Token.Line << ',' << Token.Column
                  << "): error " << Code << ": " << Message << '\n';
        bFailed = true;
    }

    bool Failed() const { return bFailed; }

private:
    std::filesystem::path File;
    bool bFailed = false;
};

std::optional<FOptions> ParseOptions(int ArgC, char** ArgV)
{
    FOptions Result;
    for (int Index = 1; Index + 1 < ArgC; Index += 2)
    {
        const std::string_view Key = ArgV[Index];
        const std::string Value = ArgV[Index + 1];
        if (Key == "--input") Result.Input = Value;
        else if (Key == "--header-output") Result.HeaderOutput = Value;
        else if (Key == "--source-output") Result.SourceOutput = Value;
        else if (Key == "--include") Result.IncludePath = Value;
        else if (Key == "--file-id") Result.FileId = Value;
        else return std::nullopt;
    }
    if (Result.Input.empty() || Result.HeaderOutput.empty() || Result.SourceOutput.empty()
        || Result.IncludePath.empty() || Result.FileId.empty())
    {
        return std::nullopt;
    }
    return Result;
}

std::vector<FToken> Tokenize(std::string_view Source)
{
    std::vector<FToken> Tokens;
    int Line = 1;
    int Column = 1;
    std::size_t Index = 0;
    auto Advance = [&](char Character)
    {
        if (Character == '\n') { ++Line; Column = 1; }
        else { ++Column; }
        ++Index;
    };

    while (Index < Source.size())
    {
        const char Character = Source[Index];
        if (std::isspace(static_cast<unsigned char>(Character)))
        {
            Advance(Character);
            continue;
        }
        if (Character == '/' && Index + 1 < Source.size() && Source[Index + 1] == '/')
        {
            while (Index < Source.size() && Source[Index] != '\n') Advance(Source[Index]);
            continue;
        }
        if (Character == '/' && Index + 1 < Source.size() && Source[Index + 1] == '*')
        {
            Advance('/'); Advance('*');
            while (Index + 1 < Source.size() && !(Source[Index] == '*' && Source[Index + 1] == '/'))
                Advance(Source[Index]);
            if (Index + 1 < Source.size()) { Advance('*'); Advance('/'); }
            continue;
        }
        if (Character == '#')
        {
            while (Index < Source.size() && Source[Index] != '\n') Advance(Source[Index]);
            continue;
        }

        FToken Token;
        Token.Line = Line;
        Token.Column = Column;
        if (std::isalpha(static_cast<unsigned char>(Character)) || Character == '_')
        {
            while (Index < Source.size()
                && (std::isalnum(static_cast<unsigned char>(Source[Index])) || Source[Index] == '_'))
            {
                Token.Text += Source[Index];
                Advance(Source[Index]);
            }
        }
        else if (Character == ':' && Index + 1 < Source.size() && Source[Index + 1] == ':')
        {
            Token.Text = "::";
            Advance(':'); Advance(':');
        }
        else
        {
            Token.Text.assign(1, Character);
            Advance(Character);
        }
        Tokens.push_back(std::move(Token));
    }
    return Tokens;
}

std::size_t FindMatching(const std::vector<FToken>& Tokens, std::size_t Open, std::string_view Left, std::string_view Right)
{
    int Depth = 0;
    for (std::size_t Index = Open; Index < Tokens.size(); ++Index)
    {
        if (Tokens[Index].Text == Left) ++Depth;
        else if (Tokens[Index].Text == Right && --Depth == 0) return Index;
    }
    return Tokens.size();
}

std::vector<std::string> ParseSpecifiers(
    const std::vector<FToken>& Tokens,
    std::size_t Macro,
    std::size_t& OutAfter,
    FDiagnostics& Diagnostics,
    const std::set<std::string>& Allowed)
{
    std::vector<std::string> Result;
    if (Macro + 1 >= Tokens.size() || Tokens[Macro + 1].Text != "(")
    {
        Diagnostics.Error(Tokens[Macro], "PHT1001", "expected '(' after reflection annotation");
        OutAfter = Macro + 1;
        return Result;
    }
    const std::size_t Close = FindMatching(Tokens, Macro + 1, "(", ")");
    if (Close == Tokens.size())
    {
        Diagnostics.Error(Tokens[Macro], "PHT1002", "unterminated reflection annotation");
        OutAfter = Tokens.size();
        return Result;
    }
    for (std::size_t Index = Macro + 2; Index < Close; ++Index)
    {
        if (Tokens[Index].Text == ",") continue;
        if (!Allowed.contains(Tokens[Index].Text))
            Diagnostics.Error(Tokens[Index], "PHT1003", "unknown specifier '" + Tokens[Index].Text + "'");
        else
            Result.push_back(Tokens[Index].Text);
    }
    OutAfter = Close + 1;
    return Result;
}

FProperty ParsePropertySpecifiers(
    const std::vector<FToken>& Tokens,
    std::size_t Macro,
    std::size_t& OutAfter,
    FDiagnostics& Diagnostics)
{
    FProperty Result;
    if (Macro + 1 >= Tokens.size() || Tokens[Macro + 1].Text != "(")
    {
        Diagnostics.Error(Tokens[Macro], "PHT1001",
            "expected '(' after reflection annotation");
        OutAfter = Macro + 1;
        return Result;
    }
    const std::size_t Close = FindMatching(Tokens, Macro + 1, "(", ")");
    if (Close == Tokens.size())
    {
        Diagnostics.Error(Tokens[Macro], "PHT1002",
            "unterminated reflection annotation");
        OutAfter = Tokens.size();
        return Result;
    }
    const std::set<std::string> Flags = {
        "Transient", "ReadOnly", "Replicated", "NotEditable",
        "NotSerializable"};
    for (std::size_t Index = Macro + 2; Index < Close; ++Index)
    {
        const std::string& Specifier = Tokens[Index].Text;
        if (Specifier == ",") continue;
        if (Flags.contains(Specifier))
        {
            Result.Specifiers.push_back(Specifier);
            continue;
        }
        if (Specifier == "InitialOnly" || Specifier == "OwnerOnly"
            || Specifier == "SkipOwner")
        {
            if (!Result.ReplicationCondition.empty())
            {
                Diagnostics.Error(Tokens[Index], "PHT3003",
                    "a property can have only one replication condition");
            }
            Result.ReplicationCondition = Specifier;
            continue;
        }
        if (Specifier == "RepNotify" && Index + 2 < Close
            && Tokens[Index + 1].Text == "=")
        {
            Result.RepNotify = Tokens[Index + 2].Text;
            Index += 2;
            continue;
        }
        if (Specifier == "Asset" && Index + 2 < Close
            && Tokens[Index + 1].Text == "=")
        {
            Result.AssetReferenceType = Tokens[Index + 2].Text;
            Index += 2;
            continue;
        }
        Diagnostics.Error(Tokens[Index], "PHT1003",
            "unknown specifier '" + Specifier + "'");
    }
    if ((!Result.RepNotify.empty()
            || !Result.ReplicationCondition.empty())
        && std::find(Result.Specifiers.begin(), Result.Specifiers.end(),
            "Replicated") == Result.Specifiers.end())
    {
        Diagnostics.Error(Tokens[Macro], "PHT3004",
            "RepNotify and replication conditions require Replicated");
    }
    OutAfter = Close + 1;
    return Result;
}

std::string QualifiedName(const std::vector<FToken>& Tokens, std::size_t Begin, std::size_t End)
{
    std::string Result;
    for (std::size_t Index = Begin; Index < End; ++Index)
    {
        if (Tokens[Index].Text == "public" || Tokens[Index].Text == "protected" || Tokens[Index].Text == "private") continue;
        Result += Tokens[Index].Text;
    }
    return Result;
}

std::vector<std::string> ParseParameters(const std::vector<FToken>& Tokens, std::size_t Open, std::size_t Close)
{
    std::vector<std::string> Result;
    std::size_t Segment = Open + 1;
    int Nested = 0;
    auto AddSegment = [&](std::size_t Begin, std::size_t End)
    {
        if (Begin >= End || (End == Begin + 1 && Tokens[Begin].Text == "void")) return;
        for (std::size_t Index = End; Index-- > Begin;)
        {
            const std::string& Text = Tokens[Index].Text;
            if (!Text.empty() && (std::isalpha(static_cast<unsigned char>(Text[0])) || Text[0] == '_')
                && Text != "const")
            {
                Result.push_back(Text);
                return;
            }
        }
    };
    for (std::size_t Index = Open + 1; Index <= Close; ++Index)
    {
        if (Index == Close || (Tokens[Index].Text == "," && Nested == 0))
        {
            AddSegment(Segment, Index);
            Segment = Index + 1;
        }
        else if (Tokens[Index].Text == "(" || Tokens[Index].Text == "<") ++Nested;
        else if (Tokens[Index].Text == ")" || Tokens[Index].Text == ">") --Nested;
    }
    return Result;
}

std::vector<FClass> Parse(const std::vector<FToken>& Tokens, FDiagnostics& Diagnostics)
{
    std::vector<FClass> Classes;
    std::string CurrentNamespace;
    int BraceDepth = 0;
    std::vector<std::pair<int, std::string>> Namespaces;

    for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
    {
        if (Tokens[Index].Text == "namespace" && Index + 2 < Tokens.size() && Tokens[Index + 2].Text == "{")
        {
            CurrentNamespace = Tokens[Index + 1].Text;
            Namespaces.emplace_back(BraceDepth + 1, CurrentNamespace);
        }
        if (Tokens[Index].Text == "{") ++BraceDepth;
        else if (Tokens[Index].Text == "}")
        {
            --BraceDepth;
            while (!Namespaces.empty() && Namespaces.back().first > BraceDepth)
                Namespaces.pop_back();
            CurrentNamespace = Namespaces.empty() ? std::string() : Namespaces.back().second;
        }

        if (Tokens[Index].Text != "PCLASS") continue;
        std::size_t Cursor = 0;
        ParseSpecifiers(Tokens, Index, Cursor, Diagnostics, {});
        if (Cursor >= Tokens.size() || (Tokens[Cursor].Text != "class" && Tokens[Cursor].Text != "struct"))
        {
            Diagnostics.Error(Tokens[Index], "PHT2001", "PCLASS must be followed by a class or struct declaration");
            continue;
        }
        const std::size_t ClassKeyword = Cursor++;
        while (Cursor < Tokens.size() && (Tokens[Cursor].Text == "final" || Tokens[Cursor].Text == "PICO_API")) ++Cursor;
        if (Cursor >= Tokens.size()) continue;
        FClass Class;
        Class.Namespace = CurrentNamespace;
        Class.Name = Tokens[Cursor++].Text;
        if (Cursor < Tokens.size() && Tokens[Cursor].Text == "final") ++Cursor;
        if (Cursor >= Tokens.size() || Tokens[Cursor].Text != ":")
        {
            Diagnostics.Error(Tokens[ClassKeyword], "PHT2002", "reflected class requires one native superclass");
            continue;
        }
        const std::size_t SuperBegin = ++Cursor;
        while (Cursor < Tokens.size() && Tokens[Cursor].Text != "{") ++Cursor;
        Class.Super = QualifiedName(Tokens, SuperBegin, Cursor);
        if (Cursor == Tokens.size())
        {
            Diagnostics.Error(Tokens[ClassKeyword], "PHT2003", "unterminated class declaration");
            continue;
        }
        const std::size_t BodyClose = FindMatching(Tokens, Cursor, "{", "}");
        if (BodyClose == Tokens.size())
        {
            Diagnostics.Error(Tokens[Cursor], "PHT2004", "unterminated class body");
            continue;
        }

        int ClassDepth = 1;
        for (std::size_t Member = Cursor + 1; Member < BodyClose; ++Member)
        {
            if (Tokens[Member].Text == "{") { ++ClassDepth; continue; }
            if (Tokens[Member].Text == "}") { --ClassDepth; continue; }
            if (ClassDepth != 1) continue;
            if (Tokens[Member].Text == "GENERATED_BODY")
            {
                Class.BodyLine = Tokens[Member].Line;
                continue;
            }
            if (Tokens[Member].Text == "PPROPERTY")
            {
                std::size_t After = 0;
                FProperty Property = ParsePropertySpecifiers(
                    Tokens, Member, After, Diagnostics);
                std::size_t End = After;
                while (End < BodyClose && Tokens[End].Text != ";") ++End;
                std::size_t NameEnd = After;
                while (NameEnd < End && Tokens[NameEnd].Text != "=") ++NameEnd;
                for (std::size_t Candidate = NameEnd; Candidate-- > After;)
                {
                    const auto& Text = Tokens[Candidate].Text;
                    if (!Text.empty() && (std::isalpha(static_cast<unsigned char>(Text[0])) || Text[0] == '_'))
                    { Property.Name = Text; break; }
                }
                if (Property.Name.empty()) Diagnostics.Error(Tokens[Member], "PHT3001", "could not determine reflected property name");
                else Class.Properties.push_back(std::move(Property));
                Member = End;
            }
            else if (Tokens[Member].Text == "PFUNCTION")
            {
                std::size_t After = 0;
                FFunction Function;
                Function.Specifiers = ParseSpecifiers(
                    Tokens, Member, After, Diagnostics,
                    {"Callable", "Pure", "Server", "Client", "NetMulticast", "Reliable"});
                std::size_t Open = After;
                while (Open < BodyClose && Tokens[Open].Text != "(" && Tokens[Open].Text != ";") ++Open;
                if (Open == BodyClose || Tokens[Open].Text != "(" || Open == After)
                {
                    Diagnostics.Error(Tokens[Member], "PHT4001", "PFUNCTION must annotate a member function declaration");
                    continue;
                }
                Function.Name = Tokens[Open - 1].Text;
                const std::size_t Close = FindMatching(Tokens, Open, "(", ")");
                if (Close == Tokens.size() || Close >= BodyClose)
                {
                    Diagnostics.Error(Tokens[Member], "PHT4002", "unterminated reflected function parameter list");
                    continue;
                }
                Function.Parameters = ParseParameters(Tokens, Open, Close);
                const auto HasSpecifier = [&Function](std::string_view Value)
                {
                    return std::find(
                        Function.Specifiers.begin(),
                        Function.Specifiers.end(),
                        Value) != Function.Specifiers.end();
                };
                const int DirectionCount =
                    static_cast<int>(HasSpecifier("Server"))
                    + static_cast<int>(HasSpecifier("Client"))
                    + static_cast<int>(HasSpecifier("NetMulticast"));
                if (DirectionCount > 1)
                {
                    Diagnostics.Error(Tokens[Member], "PHT4003",
                        "Server, Client, and NetMulticast are mutually exclusive");
                }
                if (HasSpecifier("Reliable") && DirectionCount == 0)
                {
                    Diagnostics.Error(Tokens[Member], "PHT4004",
                        "Reliable requires Server, Client, or NetMulticast");
                }
                if (HasSpecifier("Pure") && DirectionCount != 0)
                {
                    Diagnostics.Error(Tokens[Member], "PHT4005",
                        "network functions cannot be Pure");
                }
                Class.Functions.push_back(std::move(Function));
                Member = Close;
            }
        }
        if (Class.BodyLine == 0)
            Diagnostics.Error(Tokens[ClassKeyword], "PHT2005", "reflected class is missing GENERATED_BODY()");
        Classes.push_back(std::move(Class));
        Index = BodyClose;
    }
    return Classes;
}

std::string PropertyFlags(const FProperty& Property)
{
    bool Editable = true;
    bool Serializable = true;
    std::vector<std::string> Flags;
    for (const auto& Specifier : Property.Specifiers)
    {
        if (Specifier == "NotEditable") Editable = false;
        else if (Specifier == "NotSerializable") Serializable = false;
        else Flags.push_back(Specifier);
    }
    if (Editable) Flags.insert(Flags.begin(), "Editable");
    if (Serializable) Flags.push_back("Serializable");
    std::string Result = "::Pico::EPropertyFlags::None";
    for (const auto& Flag : Flags) Result += " | ::Pico::EPropertyFlags::" + Flag;
    return Result;
}

std::string PropertyMetadata(const FProperty& Property)
{
    std::string Result = "([]() { ::Pico::FPropertyMetadata Metadata; ";
    Result += "Metadata.Flags = " + PropertyFlags(Property) + "; ";
    if (!Property.ReplicationCondition.empty())
    {
        Result += "Metadata.ReplicationCondition = "
            "::Pico::EReplicationCondition::"
            + Property.ReplicationCondition + "; ";
    }
    if (!Property.RepNotify.empty())
    {
        Result += "Metadata.RepNotifyFunction = ::Pico::FName(\""
            + Property.RepNotify + "\"); ";
    }
    if (!Property.AssetReferenceType.empty())
    {
        Result += "Metadata.AssetReferenceType = ::Pico::EAssetReferenceType::"
            + Property.AssetReferenceType + "; ";
    }
    Result += "return Metadata; }())";
    return Result;
}

std::string FunctionFlags(const FFunction& Function)
{
    std::vector<std::string> Flags = Function.Specifiers;
    if (Flags.empty()) Flags.push_back("Callable");
    if (std::find(Flags.begin(), Flags.end(), "Pure") != Flags.end()
        && std::find(Flags.begin(), Flags.end(), "Callable") == Flags.end())
        Flags.insert(Flags.begin(), "Callable");
    std::string Result = "::Pico::EFunctionFlags::None";
    for (const auto& Flag : Flags) Result += " | ::Pico::EFunctionFlags::" + Flag;
    return Result;
}

std::string GenerateHeader(const FOptions& Options, const std::vector<FClass>& Classes)
{
    std::ostringstream Out;
    Out << "#pragma once\n\n#undef PICO_CURRENT_FILE_ID\n#define PICO_CURRENT_FILE_ID " << Options.FileId << "\n\n";
    for (const auto& Class : Classes)
        Out << "#define " << Options.FileId << '_' << Class.BodyLine
            << "_GENERATED_BODY PICO_DECLARE_CLASS(" << Class.Name << ", "
            << Class.Super << ")\n\n";
    return Out.str();
}

std::string GenerateSource(const FOptions& Options, const std::vector<FClass>& Classes)
{
    std::ostringstream Out;
    Out << "#include \"" << Options.IncludePath << "\"\n\n#include <utility>\n#include <vector>\n\n";
    std::string OpenNamespace;
    for (const auto& Class : Classes)
    {
        if (Class.Namespace != OpenNamespace)
        {
            if (!OpenNamespace.empty()) Out << "}\n\n";
            OpenNamespace = Class.Namespace;
            if (!OpenNamespace.empty()) Out << "namespace " << OpenNamespace << "\n{\n";
        }
        Out << "PICO_DEFINE_CLASS(" << Class.Name << ")\n\n";
        Out << "bool " << Class.Name << "::RegisterProperties(::Pico::PClass& Class)\n{\n";
        if (Class.Properties.empty() && Class.Functions.empty())
        {
            Out << "    (void)Class;\n";
        }
        if (!Class.Properties.empty())
        {
            Out << "    std::vector<::Pico::PProperty> Properties;\n";
            for (const auto& Property : Class.Properties)
            {
                if (Property.Specifiers.empty()
                    && Property.AssetReferenceType.empty())
                    Out << "    PICO_ADD_PROPERTY(Properties, " << Property.Name << ");\n";
                else
                    Out << "    PICO_ADD_PROPERTY_METADATA(Properties, " << Property.Name
                        << ", " << PropertyMetadata(Property) << ");\n";
            }
            Out << "    if (!Class.AddProperties(std::move(Properties))) return false;\n";
        }
        if (!Class.Functions.empty())
        {
            Out << "\n    std::vector<::Pico::PFunction> Functions;\n";
            for (const auto& Function : Class.Functions)
            {
                Out << "    PICO_ADD_FUNCTION(Functions, " << Function.Name << ", " << FunctionFlags(Function);
                for (const auto& Parameter : Function.Parameters)
                    Out << ", ::Pico::FName(\"" << Parameter << "\")";
                Out << ");\n";
            }
            Out << "    if (!Class.AddFunctions(std::move(Functions))) return false;\n";
        }
        Out << "    return true;\n}\n\n";
    }
    if (!OpenNamespace.empty()) Out << "}\n";
    return Out.str();
}

bool WriteIfChanged(const std::filesystem::path& Path, const std::string& Content)
{
    std::ifstream Existing(Path, std::ios::binary);
    const std::string Old((std::istreambuf_iterator<char>(Existing)), std::istreambuf_iterator<char>());
    if (Old == Content) return true;
    std::error_code Error;
    std::filesystem::create_directories(Path.parent_path(), Error);
    std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
    Output << Content;
    return Output.good();
}
}

int main(int ArgC, char** ArgV)
{
    const auto Options = ParseOptions(ArgC, ArgV);
    if (!Options)
    {
        std::cerr << "usage: PicoHeaderTool --input <header> --header-output <generated.h> "
                     "--source-output <gen.cpp> --include <include path> --file-id <identifier>\n";
        return 2;
    }
    std::ifstream Input(Options->Input, std::ios::binary);
    if (!Input)
    {
        std::cerr << Options->Input.string() << "(1,1): error PHT0001: could not open input file\n";
        return 1;
    }
    const std::string Source((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    FDiagnostics Diagnostics(Options->Input);
    const std::vector<FClass> Classes = Parse(Tokenize(Source), Diagnostics);
    if (Classes.empty() && !Diagnostics.Failed())
    {
        std::cerr << Options->Input.string() << "(1,1): error PHT0002: no PCLASS declarations found\n";
        return 1;
    }
    if (Diagnostics.Failed()) return 1;
    if (!WriteIfChanged(Options->HeaderOutput, GenerateHeader(*Options, Classes))
        || !WriteIfChanged(Options->SourceOutput, GenerateSource(*Options, Classes)))
    {
        std::cerr << Options->Input.string() << "(1,1): error PHT0003: could not write generated files\n";
        return 1;
    }
    std::cout << "PicoHeaderTool: generated " << Classes.size() << " reflected class(es) from "
              << Options->Input.string() << '\n';
    return 0;
}
