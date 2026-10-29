#include <CodeGen/UGLIR/UGLIRVerifier.hpp>
#include <cstdio>

using namespace UGLC::CodeGen::UGLIR;

/** Creates the smallest valid compute module for negative structural IR tests. */
Module makeValidModule()
{
    Module module;
    module.name = "ValidationTest";
    module.reflection.stage = ShaderStage::Compute;
    module.reflection.entryKind = ShaderEntryKind::Compute;
    Function entry;
    entry.name = "compute";
    entry.returnType = "void";
    entry.stage = ShaderStage::Compute;
    entry.entryKind = ShaderEntryKind::Compute;
    entry.isEntryPoint = true;
    module.functions.push_back(entry);
    return module;
}

/** Checks malformed IR without involving either backend's code generation. */
int main()
{
    Module module = makeValidModule();
    if (!validateModule(module).empty()) { return 1; }
    module.functions.front().body.push_back(Statement{.kind = StatementKind::Continue});
    if (validateModule(module).empty()) { return 2; }
    module = makeValidModule();
    Expression expression{.kind = ExpressionKind::Binary, .type = "u32", .operatorName = "+"};
    Statement statement{.kind = StatementKind::Expression, .expressions = {expression}};
    module.functions.front().body.push_back(statement);
    if (validateModule(module).empty()) { return 3; }
    module = makeValidModule();
    expression = Expression{.kind = ExpressionKind::Unary, .type = "u32", .operatorName = "++", .isPostfix = true,
                            .operands = {Expression{.type = "u32", .value = "1"}}};
    module.functions.front().body.push_back(Statement{.kind = StatementKind::Expression, .expressions = {expression}});
    if (validateModule(module).empty()) { return 4; }
    module = makeValidModule();
    module.functions.front().returnType = "missing_type";
    if (validateModule(module).empty()) { return 5; }
    module = makeValidModule();
    module.functions.push_back(module.functions.front());
    if (validateModule(module).empty()) { return 6; }
    module = makeValidModule();
    module.reflection.resources = {ResourceBinding{.name = "first"}, ResourceBinding{.name = "second"}};
    if (validateModule(module).empty()) { return 7; }
    module = makeValidModule();
    module.types.push_back(Type{.name = "wide", .kind = TypeKind::UInt, .scalarKind = ScalarKind::UInt, .bitWidth = 64});
    if (validateModule(module).empty()) { return 8; }
    module = makeValidModule();
    module.types.push_back(Type{.name = "Cycle", .kind = TypeKind::Struct, .fields = {TypeField{.name = "self", .type = "Cycle"}}});
    if (validateModule(module).empty()) { return 9; }
    module = makeValidModule();
    module.functions.front().body.push_back(Statement{.kind = static_cast<StatementKind>(255)});
    if (validateModule(module).empty()) { return 10; }
    module = makeValidModule();
    expression = Expression{.kind = ExpressionKind::Call, .type = "void", .name = "undefined_function"};
    module.functions.front().body.push_back(Statement{.kind = StatementKind::Expression, .expressions = {expression}});
    if (validateModule(module).empty()) { return 11; }
    module = makeValidModule();
    Function callee;
    callee.name = "consume";
    callee.returnType = "void";
    callee.parameters.push_back(FunctionParameter{.name = "value", .type = "u32"});
    module.functions.push_back(callee);
    expression = Expression{.kind = ExpressionKind::Call, .type = "void", .name = "consume",
                            .operands = {Expression{.type = "u32", .value = "1"}}};
    module.functions.front().body.push_back(Statement{.kind = StatementKind::Expression, .expressions = {expression}});
    if (!validateModule(module).empty()) { return 12; }
    module.functions.front().body.front().expressions.front().operands.front().type = "f32";
    if (validateModule(module).empty()) { return 13; }
    module.functions.front().body.front().expressions.front().operands.front().type = "u32";
    module.functions.front().body.front().expressions.front().type = "u32";
    if (validateModule(module).empty()) { return 14; }
    std::puts("uglir_invariant_validation_ok");
    return 0;
}
