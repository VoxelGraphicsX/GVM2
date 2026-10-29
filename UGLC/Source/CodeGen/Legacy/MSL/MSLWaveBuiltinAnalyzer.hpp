#pragma once

#include <CodeGen/BaseASTVisitor.hpp>

#include <clang/AST/Decl.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace UGLC::CodeGen::MSL
{
    /**
     * @brief Computes the hidden Metal wave-builtin parameters required by MSL helpers.
     *
     * DSL calls such as `UGL::WaveGetLaneIndex()` and `UGL::WaveMatch(value)`
     * do not lower to ordinary function calls in MSL. The analyzer scans the
     * shader helper graph, propagates transitive wave requirements, and assigns
     * collision-free hidden parameter names so `MSLVisitor` can emit signatures
     * and calls without changing the visible DSL function shape.
     *
     * Example DSL -> MSL:
     *
     * ```cpp
     * uint lane = UGL::WaveGetLaneIndex();
     * auto mask = UGL::WaveMatch(value);
     * ```
     *
     * ```metal
     * uint lane = __uglc_hidden_wave_lane_index;
     * uint4 mask = UGLC_WaveMatch(value,
     *                            __uglc_hidden_wave_lane_index,
     *                            __uglc_hidden_wave_lane_count);
     * ```
     */
    class MSLWaveBuiltinAnalyzer
    {
    public:
        /**
         * @brief Direct and transitive wave values required by one emitted function.
         */
        struct Requirements
        {
            bool laneIndex = false;
            bool laneCount = false;
            bool explicitLaneIndexQuery = false;
            bool explicitLaneCountQuery = false;

            /**
             * @brief Returns true when any hidden wave parameter must be available.
             */
            [[nodiscard]] bool any() const;

            /**
             * @brief Returns true when the source directly queried lane index/count.
             */
            [[nodiscard]] bool usesExplicitQueries() const;
        };

        /**
         * @brief Clears all previously analyzed shader-unit state.
         */
        void reset();

        /**
         * @brief Scans the shader unit and computes transitive wave requirements.
         *
         * @param shaderDefs Declarations that will be emitted into the shader unit.
         * @param shaderClassDecl Owning shader class used for hidden-name collision checks.
         * @param mainFunc Selected shader entry point.
         * @param visitor Visitor service used for DSL attribute checks and diagnostics.
         */
        void analyze(const std::vector<const clang::Decl *> &shaderDefs,
                     const clang::CXXRecordDecl *shaderClassDecl,
                     const clang::FunctionDecl *mainFunc,
                     const BaseASTVisitor &visitor);

        /**
         * @brief Returns the already-computed requirements for one function.
         */
        [[nodiscard]] Requirements getRequirements(const clang::FunctionDecl *func) const;

        /**
         * @brief Appends hidden wave parameters to an emitted MSL function signature.
         */
        void appendInjectedParams(std::vector<std::string> &params,
                                  const Requirements &requirements,
                                  bool entryPoint) const;

        /**
         * @brief Appends forwarded hidden arguments for a helper call.
         */
        void appendInjectedArgs(const clang::FunctionDecl *calleeDecl,
                                const clang::FunctionDecl *currentFunction,
                                std::vector<std::string> &args) const;

        /**
         * @brief Returns the hidden value name visible inside the current function.
         */
        [[nodiscard]] std::string getCurrentBuiltinValueName(const clang::FunctionDecl *currentFunction, bool laneIndex) const;

    private:
        std::unordered_map<const clang::FunctionDecl *, Requirements> mRequirementsByFunction;
        std::string mLaneIndexHiddenName;
        std::string mLaneCountHiddenName;
    };
} // namespace UGLC::CodeGen::MSL
