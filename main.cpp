#include <iostream>

#include "frontend/lexer/lexer.h"
#include "frontend/parser/parser.h"
#include "frontend/sema/sema.h"
#include "backend/C/codegen.h"
#define VERSION "pre alpha 0.1"

#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>

void help()
{
    std::cout << "Ignis V" << VERSION << std::endl;
    std::cout << "      -flag [<value>]" << std::endl;
    std::cout << "\n";
    std::cout << "      -c [<path>]" << std::endl;
    std::cout << "          compiles provied files\n";
    std::cout << "      -cc [<input>]... [<output>]" << std::endl;
    std::cout << "          compiles one or more files to C code\n";
    std::cout << "      -v\n";
    std::cout << "          displays installed version of Ignis\n";
    std::cout << "      -h\n";
    std::cout << "        displays this help\n";
}

// Helper function to load and parse a single file
NodeArena loadAndParseFile(const char *path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in)
    {
        std::cerr << "Failed to open file: " << path << std::endl;
        return NodeArena();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    Lexer lexer;
    auto toks = lexer.Tokenize(content);

    Parser parser;
    parser.Parse(&toks, content, path);

    return parser.arena;
}

// Helper function to merge arena with ID offset
void mergeArenaWithOffset(NodeArena &targetArena, const NodeArena &sourceArena, size_t idOffset)
{
    for (size_t i = 0; i < sourceArena.size(); ++i)
    {
        Node node = sourceArena.get(i);

        for (auto &bodyId : node.body)
        {
            bodyId += idOffset;
        }
        for (auto &argId : node.exprArgs)
        {
            argId += idOffset;
        }
        for (auto &fieldId : node.structFields)
        {
            fieldId.second += idOffset;
        }
        if (node.exprKind == ExprType::ExprFieldAccess && node.fieldObject < UINT32_MAX)
        {
            node.fieldObject += idOffset;
        }

        targetArena.create(node);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        help();
        return 0;
    }

    // CLI: -h, -v, -c <path>
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "-help") == 0)
        {
            help();
            return 0;
        }
        if (std::strcmp(a, "-v") == 0 || std::strcmp(a, "-version") == 0)
        {
            std::cout << VERSION << std::endl;
            return 0;
        }
        if (std::strcmp(a, "-c") == 0)
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: -c requires a path argument\n";
                return 1;
            }
            const char *path = argv[++i];
            std::ifstream in(path, std::ios::in | std::ios::binary);
            if (!in)
            {
                std::cerr << "Failed to open file: " << path << std::endl;
                return 2;
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            std::string content = ss.str();

            Lexer lexer;
            auto toks = lexer.Tokenize(content);

            for (auto &tok : toks)
            {
                std::cout << "type= " << tok.type << " value= " << tok.value << std::endl;
            }

            Parser parser;
            parser.Parse(&toks, content, path);
            SemanticAnalyzer sema;
            sema.Analyze(parser.arena, content, path);

            if (sema.foundMain == false)
            {
                std::cerr << "Error: 'main' function not found\n";
                return 4;
            }

            if (!parser.errors.empty())
            {
                std::cerr << "Parsing finished with " << parser.errors.size() << " error(s)\n";
                return 3;
            }

            return 0;
        }
        if (std::strcmp(a, "-cc") == 0)
        {
            if (i + 2 >= argc)
            {
                std::cerr << "Error: -cc requires at least one input file and one output path\n";
                return 1;
            }

            // Collect all input files until we find the last argument (output path)
            std::vector<std::string> inputPaths;
            const char *outputPath = nullptr;

            // The last argument is the output path
            outputPath = argv[argc - 1];

            // All arguments between -cc and the output path are input files
            for (int j = i + 1; j < argc - 1; ++j)
            {
                inputPaths.push_back(argv[j]);
            }

            if (inputPaths.empty())
            {
                std::cerr << "Error: -cc requires at least one input file\n";
                return 1;
            }

            // First, load and parse std.ign (standard library)
            std::ifstream stdIn("std.ign", std::ios::in | std::ios::binary);
            NodeArena stdArena;
            if (stdIn)
            {
                std::ostringstream ss;
                ss << stdIn.rdbuf();
                std::string stdContent = ss.str();

                Lexer lexer;
                auto stdToks = lexer.Tokenize(stdContent);

                Parser stdParser;
                stdArena = stdParser.Parse(&stdToks, stdContent, "std.ign");
            }

            // Merge all input files into a single arena
            NodeArena mergedArena;
            for (const auto &inputPath : inputPaths)
            {
                NodeArena fileArena = loadAndParseFile(inputPath.c_str());
                size_t idOffset = mergedArena.size();
                mergeArenaWithOffset(mergedArena, fileArena, idOffset);
            }

            // Merge std.ign into the final arena
            size_t idOffset = mergedArena.size();
            mergeArenaWithOffset(mergedArena, stdArena, idOffset);

            SemanticAnalyzer sema;
            sema.Analyze(mergedArena, "", "");

            if (sema.foundMain == false)
            {
                std::cerr << "Error: 'main' function not found\n";
                return 4;
            }

            // Note: We should check for parser errors from all files
            // For now, we'll just proceed with codegen

            // Compile to C
            ignis::backend::c::CodeGen codegen;
            codegen.generate(mergedArena, outputPath);

            std::string executablePath = std::string(outputPath);

            if (executablePath.length() > 2 && executablePath.substr(executablePath.length() - 2) == ".c")
            {
                executablePath = executablePath.substr(0, executablePath.length() - 2);
            }

            std::string compileCmd = std::string("gcc -o ") + executablePath + " " + outputPath + " ignis_std.c";
            std::cout << "Compiling C code with: " << compileCmd << std::endl;
            int compileResult = system(compileCmd.c_str());
            if (compileResult != 0)
            {
                std::cerr << "Error: C compilation failed with exit code " << compileResult << std::endl;
                return 5;
            }

            std::cout << "Successfully compiled to: " << executablePath << std::endl;

            return 0;
        }
    }

    help();
    return 0;
}