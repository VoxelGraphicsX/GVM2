#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <Details/UGL.Types-glm.h>

/** Verifies GVM projection conventions when application GLM includes come first. */
bool glmFirstProjectionMatches()
{
    const auto projection = glm::perspective(1.0f, 1.0f, 0.1f, 100.0f);
    const auto nearPoint = projection * glm::vec4(0.0f, 0.0f, 0.1f, 1.0f);
    return projection[2][3] == 1.0f && glm::abs(nearPoint.z / nearPoint.w) < 0.00001f;
}
