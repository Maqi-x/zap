#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace zap::sema
{
    struct FunctionSymbol
    {
        std::string name;
        bool isExtern_ = false;
        bool isStatic_ = false;
        bool isPublic_ = false;
    };

    class SymbolTable
    {
    private:
        std::unordered_map<std::string, std::shared_ptr<FunctionSymbol>> functions_;

    public:
        bool found_main = false;
        void addFunction(FunctionSymbol &&func);
        std::shared_ptr<FunctionSymbol> getFunction(const std::string &name);
        void analyze();
    };
} // namespace sema