#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/** Checks the public GLM target without including or linking GVM runtime headers. */
int main()
{
    const auto projection = glm::perspective(1.0f, 1.0f, 0.1f, 100.0f);
    return projection[2][3] == 1.0f && sizeof(glm::vec3) == 3 * sizeof(float) ? 0 : 1;
}
