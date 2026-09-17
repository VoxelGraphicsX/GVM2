#pragma once

#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/Legacy/MSL/MSLWaveBuiltinAnalyzer.hpp>

#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>

#include <optional>
#include <string>

namespace UGLC::CodeGen::MSL
{
    /**
     * @brief Validates and lowers MSL-specific shader builtin calls.
     *
     * The DSL exposes backend-neutral shader builtins, while Metal requires a
     * mixture of native functions, helper calls, and hidden entry parameters.
     * This class keeps that policy out of `MSLVisitor`'s generic call lowering.
     *
     * Example DSL -> MSL:
     *
     * ```cpp
     * if (alpha < 0.5f) { UGL::discard_fragment(); }
     * uint lane = UGL::WaveGetLaneIndex();
     * auto mask = UGL::WaveMatch(value);
     * ```
     *
     * ```metal
     * if (alpha < 0.5f) { discard_fragment(); }
     * uint lane = __uglc_hidden_wave_lane_index;
     * uint4 mask = UGLC_WaveMatch(value,
     *                            __uglc_hidden_wave_lane_index,
     *                            __uglc_hidden_wave_lane_count);
     * ```
     */
    class MSLShaderBuiltinTranslator
    {
    public:
        /** @brief Creates a builtin translator backed by visitor and wave analysis services. */
        MSLShaderBuiltinTranslator(BaseASTVisitor &visitor, MSLWaveBuiltinAnalyzer &waveBuiltinAnalyzer);

        /**
         * @brief Returns backend-native MSL when a call is a handled DSL shader builtin.
         *
         * Some builtins only need validation here and intentionally return
         * `std::nullopt` so the generic call path can continue to emit the
         * existing helper-call form with any required injected wave arguments.
         */
        [[nodiscard]] std::optional<std::string> tryTranslateCallExpr(const clang::CallExpr *expr,
                                                                       const clang::FunctionDecl *callee,
                                                                       const clang::FunctionDecl *entryFunction,
                                                                       const clang::FunctionDecl *currentFunction) const;

    private:
        /** @brief Owning visitor service object used for recursive argument translation. */
        BaseASTVisitor &mVisitor;

        /** @brief Analyzer that provides hidden wave builtin value names. */
        MSLWaveBuiltinAnalyzer &mWaveBuiltinAnalyzer;
    };
} // namespace UGLC::CodeGen::MSL
