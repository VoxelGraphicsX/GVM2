#ifndef GAUSSIAN_SPLATTING_SHARED_CAMERA_HPP
#define GAUSSIAN_SPLATTING_SHARED_CAMERA_HPP

#include "UGL.h"
using namespace UGL;

struct Camera
{
    float4x4 proj;
    float4x4 view;
    float4x4 projInv;
    float4x4 viewInv;
};

struct CameraBindGroup final : public IBindGroup
{
    constructor(UniformBuffer<Camera> camBuffer [[Binding0]])
    {
    }
};

inline float4x4 PerspectiveLH(float fovY, float aspect, float zNear, float zFar)
{
    const float yScale = 1.0f / tan(fovY * 0.5f);
    const float xScale = yScale / aspect;

    float4x4 matrix = (float4x4)0;
    matrix[0][0] = xScale;
    matrix[1][1] = yScale;
    matrix[2][2] = zFar / (zFar - zNear);
    matrix[2][3] = 1.0f;
    matrix[3][2] = -(zNear * zFar) / (zFar - zNear);

    return matrix;
}

inline float4x4 PerspectiveReverseZLH(float fovY, float aspect, float zNear, float zFar)
{
    const float yScale = 1.0f / tan(fovY * 0.5f);
    const float xScale = yScale / aspect;

    float4x4 matrix = (float4x4)0;
    matrix[0][0] = xScale;
    matrix[1][1] = yScale;
    matrix[2][2] = zNear / (zNear - zFar);
    matrix[2][3] = 1.0f;
    matrix[3][2] = (zNear * zFar) / (zFar - zNear);

    return matrix;
}

#endif
