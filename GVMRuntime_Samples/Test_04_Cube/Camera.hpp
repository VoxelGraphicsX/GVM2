#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "UGL.h"
using namespace UGL;
struct Camera
{
    float4x4 proj;
    float4x4 view;
    float4x4 projInv;
    float4x4 viewInv;
};
struct CameraBindGroup : public IBindGroup
{
    constructor(UniformBuffer<Camera> camBuffer [[Binding0]])
    {
    }
};

float4x4 PerspectiveLH(float fovY, float aspect, float zFar, float zNear)
{
    float yScale = 1.0 / tan(fovY / 2.0); // 1 / tan(θ/2)
    float xScale = yScale / aspect;       // 宽高比

    float4x4 matrix = (float4x4)0;
    matrix[0][0] = xScale;                         // 缩放X轴
    matrix[1][1] = yScale;                         // 缩放Y轴
    matrix[2][2] = zFar / (zFar - zNear);          // Z轴范围从0到1
    matrix[2][3] = 1.0;                            // 投影转换的标志位
    matrix[3][2] = -zNear * zFar / (zFar - zNear); // 深度值计算

    return matrix;
}

#endif // CAMERA_HPP