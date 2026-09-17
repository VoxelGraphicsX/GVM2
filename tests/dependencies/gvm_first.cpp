#include <Details/UGL.Types-glm.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/** Verifies GVM projection conventions when its host math header comes first. */
bool gvmFirstProjectionMatches()
{
    const auto projection = glm::perspective(1.0f, 1.0f, 0.1f, 100.0f);
    const auto farPoint = projection * glm::vec4(0.0f, 0.0f, 100.0f, 1.0f);
    return projection[2][3] == 1.0f && glm::abs(farPoint.z / farPoint.w - 1.0f) < 0.00001f;
}
