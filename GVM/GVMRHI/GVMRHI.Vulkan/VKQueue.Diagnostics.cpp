#include "VKQueue.Diagnostics.hpp"

#include <cstdio>

namespace GVM::RHI::Vulkan::QueueDiagnostics
{
    namespace
    {
        void appendFieldPrefix(eastl::string &text, eastl::string_view name)
        {
            text += ' ';
            text.append(name.data(), name.size());
            text += '=';
        }

        void appendUIntField(eastl::string &text, eastl::string_view name, uint64_t value)
        {
            appendFieldPrefix(text, name);
            text += eastl::to_string(value);
        }

        void appendSizeField(eastl::string &text, eastl::string_view name, size_t value)
        {
            appendUIntField(text, name, static_cast<uint64_t>(value));
        }

        void appendBoolField(eastl::string &text, eastl::string_view name, bool value)
        {
            appendFieldPrefix(text, name);
            text += value ? "true" : "false";
        }
        void appendCStringField(eastl::string &text, eastl::string_view name, const char *value)
        {
            appendFieldPrefix(text, name);
            text += value != nullptr ? value : "<null>";
        }

        void appendF64Field(eastl::string &text, eastl::string_view name, double value)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.3f", value);
            appendCStringField(text, name, buffer);
        }
    } // namespace

    eastl::string buildRetiredSubmissionLogMessage(
        const eastl::string &queueName,
        const VKSubmissionManager::InFlightSubmission &submission,
        double lifetimeMs)
    {
        eastl::string text = "event=queue_submission_retire";
        appendCStringField(text, "queue", queueName.c_str());
        appendUIntField(text, "submission_id", submission.submissionId);
        appendSizeField(text, "encoders", submission.encoders.size());
        appendSizeField(text, "retained_buffers", submission.diagnosticRetainedBufferCount);
        appendSizeField(text, "retained_textures", submission.diagnosticRetainedTextureCount);
        appendBoolField(text, "present_internal", submission.internalPresentSubmit);
        appendF64Field(text, "lifetime_ms", lifetimeMs);
        return text;
    }

    eastl::string buildTrackedSubmissionLogMessage(
        const eastl::string &queueName,
        uint64_t submissionId,
        size_t encoderCount,
        size_t totalPassCount,
        size_t retainedBufferCount,
        size_t retainedTextureCount,
        size_t inFlightAfter,
        bool internalPresentSubmit)
    {
        eastl::string text = "event=queue_track_submission";
        appendCStringField(text, "queue", queueName.c_str());
        appendUIntField(text, "submission_id", submissionId);
        appendSizeField(text, "encoders", encoderCount);
        appendSizeField(text, "total_passes_pending", totalPassCount);
        appendSizeField(text, "retained_buffers", retainedBufferCount);
        appendSizeField(text, "retained_textures", retainedTextureCount);
        appendSizeField(text, "in_flight_after", inFlightAfter);
        appendBoolField(text, "present_internal", internalPresentSubmit);
        return text;
    }
} // namespace GVM::RHI::Vulkan::QueueDiagnostics
