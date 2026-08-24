#pragma once

#include <ufbx.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace MipsyncEngine {

inline glm::vec3 UfbxToGlm(const ufbx_vec3& v) { return { (float)v.x, (float)v.y, (float)v.z }; }

inline glm::mat4 UfbxMatrixToGlm(const ufbx_matrix& m) {
    glm::mat4 r(1.0f);
    for (int c = 0; c < 3; ++c) {
        r[c][0] = (float)m.cols[c].x;
        r[c][1] = (float)m.cols[c].y;
        r[c][2] = (float)m.cols[c].z;
    }
    r[3][0] = (float)m.m03;
    r[3][1] = (float)m.m13;
    r[3][2] = (float)m.m23;
    return r;
}

inline glm::mat4 UfbxTransformToGlm(const ufbx_transform& t) {
    return UfbxMatrixToGlm(ufbx_transform_to_matrix(&t));
}

} // namespace MipsyncEngine
