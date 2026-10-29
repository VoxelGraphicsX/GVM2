#include <CodeGen/CodeWriter.hpp>

#include <iostream>
#include <stdexcept>

int main()
{
    using UGLC::CodeGen::CodeWriter;

    CodeWriter shaderSourceWriter;
    shaderSourceWriter.appendLine("const UGLC::Generated::ShaderArtifact computeShaderArtifact = UGLC::Generated::MakeShaderArtifact(");
    shaderSourceWriter.appendLine("    __UGL__Global__MSLHeader + R\"(");
    shaderSourceWriter.appendRaw("kernel void computeMain() {}\n");
    shaderSourceWriter.appendLine(")\",");
    shaderSourceWriter.appendLine("    nullptr,");
    shaderSourceWriter.appendLine("    0");
    shaderSourceWriter.appendStatement("", ")");

    const std::string expectedShaderSource =
        "const UGLC::Generated::ShaderArtifact computeShaderArtifact = UGLC::Generated::MakeShaderArtifact(\n"
        "    __UGL__Global__MSLHeader + R\"(\n"
        "kernel void computeMain() {}\n"
        ")\",\n"
        "    nullptr,\n"
        "    0\n"
        ");\n";
    if (shaderSourceWriter.str() != expectedShaderSource)
    {
        throw std::runtime_error("CodeWriter changed embedded shader-artifact formatting");
    }

    CodeWriter statementWriter;
    statementWriter.appendStatement("    ", "pipeline.label = \"ComputePipeline\"");
    statementWriter.appendStatement("", "return value");

    const std::string expectedStatements =
        "    pipeline.label = \"ComputePipeline\";\n"
        "return value;\n";
    if (statementWriter.take() != expectedStatements)
    {
        throw std::runtime_error("CodeWriter changed statement formatting");
    }

    std::cout << "code_writer_formatting_ok\n";
    return 0;
}
