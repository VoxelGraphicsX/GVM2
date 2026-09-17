#include <CodeGen/UGLIR/UGLIRDump.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

/** Runs white-box checks for the UGLIR core dump contract. */
int main()
{
    using namespace UGLC::CodeGen::UGLIR;

    const SourceLocation moduleLocation{"tests/uglc/fixtures/compute-basic/ComputeBasic.hpp", 15, 1};
    const SourceLocation bindGroupLocation{"tests/uglc/fixtures/compute-basic/ComputeBasic.hpp", 8, 8};
    const SourceLocation computeLocation{"tests/uglc/fixtures/compute-basic/ComputeBasic.hpp", 18, 5};
    const SourceLocation parameterLocation{"tests/uglc/fixtures/compute-basic/ComputeBasic.hpp", 18, 22};
    const SourceLocation storeLocation{"tests/uglc/fixtures/compute-basic/ComputeBasic.hpp", 20, 9};
    const SourceLocation returnLocation{"tests/uglc/fixtures/compute-basic/ComputeBasic.hpp", 21, 9};

    Module module;
    module.name = "Phase1ComputeModule";
    module.sourceLocation = moduleLocation;
    module.types = {
        Type{
            .name = "void",
            .kind = TypeKind::Void,
            .sourceLocation = moduleLocation,
        },
        Type{
            .name = "u32",
            .kind = TypeKind::UInt,
            .scalarKind = ScalarKind::UInt,
            .bitWidth = 32,
            .sourceLocation = moduleLocation,
        },
        Type{
            .name = "uint3",
            .kind = TypeKind::Vector,
            .scalarKind = ScalarKind::UInt,
            .bitWidth = 32,
            .elementType = "u32",
            .vectorWidth = 3,
            .sourceLocation = parameterLocation,
        },
        Type{
            .name = "OutputValues",
            .kind = TypeKind::Buffer,
            .elementType = "u32",
            .accessMode = AccessMode::ReadWrite,
            .sourceLocation = bindGroupLocation,
        },
    };

    Expression dispatchThreadIDRef;
    dispatchThreadIDRef.kind = ExpressionKind::DeclRef;
    dispatchThreadIDRef.type = "uint3";
    dispatchThreadIDRef.name = "dispatchThreadID";
    dispatchThreadIDRef.sourceLocation = storeLocation;

    Expression dispatchThreadIDX;
    dispatchThreadIDX.kind = ExpressionKind::MemberRef;
    dispatchThreadIDX.type = "u32";
    dispatchThreadIDX.name = "x";
    dispatchThreadIDX.operands = {dispatchThreadIDRef};
    dispatchThreadIDX.sourceLocation = storeLocation;

    Expression outputValuesRef;
    outputValuesRef.kind = ExpressionKind::DeclRef;
    outputValuesRef.type = "OutputValues";
    outputValuesRef.name = "outputValues";
    outputValuesRef.sourceLocation = storeLocation;

    Expression outputValuesSubscript;
    outputValuesSubscript.kind = ExpressionKind::Subscript;
    outputValuesSubscript.type = "u32";
    outputValuesSubscript.operands = {outputValuesRef, dispatchThreadIDX};
    outputValuesSubscript.sourceLocation = storeLocation;

    Expression storeExpression;
    storeExpression.kind = ExpressionKind::Store;
    storeExpression.type = "void";
    storeExpression.operands = {outputValuesSubscript, dispatchThreadIDX};
    storeExpression.sourceLocation = storeLocation;

    Statement storeStatement;
    storeStatement.kind = StatementKind::Expression;
    storeStatement.expressions = {storeExpression};
    storeStatement.sourceLocation = storeLocation;

    Statement returnStatement;
    returnStatement.kind = StatementKind::Return;
    returnStatement.sourceLocation = returnLocation;

    Function computeFunction;
    computeFunction.name = "computeMain";
    computeFunction.returnType = "void";
    computeFunction.linkage = FunctionLinkage::Exported;
    computeFunction.stage = ShaderStage::Compute;
    computeFunction.entryKind = ShaderEntryKind::Compute;
    computeFunction.isEntryPoint = true;
    computeFunction.workgroupSize = {8, 1, 1};
    computeFunction.parameters = {
        FunctionParameter{
            .name = "dispatchThreadID",
            .type = "uint3",
            .semantic = "DispatchThreadID",
            .semanticKind = BuiltinSemanticKind::DispatchThreadID,
            .sourceLocation = parameterLocation,
        },
    };
    computeFunction.body = {storeStatement, returnStatement};
    computeFunction.sourceLocation = computeLocation;
    module.functions = {computeFunction};

    module.reflection.entryName = "computeMain";
    module.reflection.stage = ShaderStage::Compute;
    module.reflection.entryKind = ShaderEntryKind::Compute;
    module.reflection.workgroupSize = {8, 1, 1};
    module.reflection.artifactHash = "phase1-dump";
    module.reflection.resources = {
        ResourceBinding{
            .name = "outputValues",
            .kind = ResourceKind::StorageBuffer,
            .bindGroupIndex = 0,
            .bindingIndex = 0,
            .accessMode = AccessMode::ReadWrite,
            .elementType = "u32",
            .arrayCount = 1,
            .visibleStages = {ShaderStage::Compute},
            .sourceLocation = bindGroupLocation,
        },
    };

    const std::string expectedText = R"(module "Phase1ComputeModule" source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":15:1
  types
    type "void" kind void scalar_kind none access read source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":15:1
    type "u32" kind uint scalar_kind uint bit_width 32 access read source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":15:1
    type "uint3" kind vector scalar_kind uint bit_width 32 element "u32" vector_width 3 access read source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":18:22
    type "OutputValues" kind buffer scalar_kind none element "u32" access read_write source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":8:8
  functions
    function "computeMain" linkage exported stage compute entry_kind compute entry true return "void" source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":18:5
      workgroup_size 8 1 1
      params
        param "dispatchThreadID" type "uint3" semantic "DispatchThreadID" passing_mode value reference false const_reference false source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":18:22
      body
        stmt expr source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":20:9
          expr store type "void" intrinsic none construct_kind none source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":20:9
            expr subscript type "u32" intrinsic none construct_kind none source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":20:9
              expr decl_ref type "OutputValues" intrinsic none construct_kind none name "outputValues" source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":20:9
              expr member_ref type "u32" intrinsic none construct_kind none name "x" source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":20:9
                expr decl_ref type "uint3" intrinsic none construct_kind none name "dispatchThreadID" source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":20:9
            expr member_ref type "u32" intrinsic none construct_kind none name "x" source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":20:9
              expr decl_ref type "uint3" intrinsic none construct_kind none name "dispatchThreadID" source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":20:9
        stmt return source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":21:9
  reflection
    entry "computeMain" stage compute entry_kind compute
    workgroup_size 8 1 1
    artifact_hash "phase1-dump"
    stage_inputs
    stage_outputs
    resources
      resource "outputValues" kind storage_buffer set 0 binding 0 access read_write element "u32" array_count 1 texture_dimension "none" texture_format "unknown" multisampled false input_attachment 0 resource_role "none" resource_index 0 stages compute source "tests/uglc/fixtures/compute-basic/ComputeBasic.hpp":8:8
)";

    const std::string firstText = dumpModuleAsText(module);
    const std::string secondText = dumpModuleAsText(module);
    if (firstText != expectedText)
    {
        throw std::runtime_error("UGLIR text dump does not match the phase-1 golden output.");
    }
    if (firstText != secondText)
    {
        throw std::runtime_error("UGLIR text dump is not deterministic across repeated calls.");
    }

    const std::string firstJson = dumpModuleAsJson(module);
    const std::string secondJson = dumpModuleAsJson(module);
    if (firstJson != secondJson)
    {
        throw std::runtime_error("UGLIR JSON dump is not deterministic across repeated calls.");
    }

    const auto parsedJson = nlohmann::ordered_json::parse(firstJson);
    if (parsedJson.at("schemaVersion") != 1 ||
        parsedJson.at("name") != "Phase1ComputeModule" ||
        parsedJson.at("sourceLocation").at("line") != 15 ||
        parsedJson.at("types").at(2).at("kind") != "vector" ||
        parsedJson.at("types").at(2).at("scalarKind") != "uint" ||
        parsedJson.at("functions").at(0).at("entryKind") != "compute" ||
        parsedJson.at("functions").at(0).at("parameters").at(0).at("semantic") != "DispatchThreadID" ||
        parsedJson.at("functions").at(0).at("parameters").at(0).at("semanticKind") != "DispatchThreadID" ||
        parsedJson.at("functions").at(0).at("parameters").at(0).at("semanticIndex") != 0 ||
        parsedJson.at("functions").at(0).at("parameters").at(0).at("passingMode") != "value" ||
        parsedJson.at("functions").at(0).at("body").at(0).at("expressions").at(0).at("kind") != "store" ||
        parsedJson.at("functions").at(0).at("body").at(0).at("expressions").at(0).at("intrinsicKind") != "none" ||
        parsedJson.at("functions").at(0).at("body").at(0).at("expressions").at(0).at("constructKind") != "none" ||
        !parsedJson.at("functions").at(0).at("body").at(0).at("expressions").at(0).at("constructComponents").empty() ||
        parsedJson.at("types").at(2).at("resourceKind") != "unknown" ||
        parsedJson.at("types").at(2).at("textureDimension") != "none" ||
        parsedJson.at("types").at(2).at("textureFormat") != "unknown" ||
        parsedJson.at("types").at(2).at("isMultisampled") != false ||
        parsedJson.at("reflection").at("entryKind") != "compute" ||
        !parsedJson.at("reflection").at("stageInputs").empty() ||
        !parsedJson.at("reflection").at("stageOutputs").empty() ||
        parsedJson.at("reflection").at("resources").at(0).at("textureDimension") != "none" ||
        parsedJson.at("reflection").at("resources").at(0).at("textureFormat") != "unknown" ||
        parsedJson.at("reflection").at("resources").at(0).at("resourceIndex") != 0 ||
        parsedJson.at("reflection").at("resources").at(0).at("resourceRole") != "none" ||
        parsedJson.at("reflection").at("resources").at(0).at("kind") != "storage_buffer")
    {
        throw std::runtime_error("UGLIR JSON dump lost required source, type, function, statement, expression, or reflection fields.");
    }

    std::cout << "uglir_text_dump_ok\n";
    std::cout << "uglir_json_dump_ok\n";
    std::cout << "uglir_dump_determinism_ok\n";
    return 0;
}
