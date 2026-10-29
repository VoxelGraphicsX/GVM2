#ifndef CAMERA_CONTROLLER_HPP
#define CAMERA_CONTROLLER_HPP

#include "UGL.h"
using namespace UGL;
#include "Quaternion.hpp"

#include <algorithm>
#include <cmath>

float3 rotateVector(float4 r, float3 v)
{
    float4 r_c = qmul(r, float4(-1, -1, -1, 1));
    return qmul(r, qmul(float4(v, 0), r_c)).xyz;
}

/// <summary>
/// 一个简易的使用「左手系 + Reversed Z」相机的示例：
/// - 不做任何图形 API 的特判，
/// - CPU 坐标系/深度只使用一种统一方式，
/// - 在渲染时可在 Shader 或 RHI 层添加额外 fix-up 矩阵来适配目标实现。
/// </summary>
class CameraController
{
public:
    /// <summary>
    /// 构造：左手系 + ReversedZ
    /// </summary>
    /// <param name="fovYDeg">FOV (度数)</param>
    /// <param name="aspect">宽高比</param>
    /// <param name="nearZ">近裁面距离</param>
    /// <param name="farZ">远裁面距离</param>
    /// <param name="initPos">相机初始位置</param>
    CameraController(float fovYDeg = 60.0f, float aspect = 16.0f / 9.0f, float nearZ = 0.1f, float farZ = 1000.0f, const float3 &initPos = float3(0, 0, 0))
        : mFovYDeg(fovYDeg)
        , mAspectRatio(aspect)
        , mNearZ(nearZ)
        , mFarZ(farZ)
        , mPosition(initPos)
        , mOrientation(float4(0, 0, 0, 1)) // 单位旋转
        , mTargetPosition(initPos)
        , mTargetOrientation(float4(0, 0, 0, 1))
        , mIsViewDirty(true)
        , mIsProjDirty(true)
        , mPositionSmoothFactor(10.0f)
        , mOrientationSmoothFactor(10.0f)
    {
        UpdateViewMatrix();
        UpdateProjectionMatrix();
    }

    //------------------------------------------------
    // 参数设置
    //------------------------------------------------
    void SetFovYDeg(float fovDeg)
    {
        mFovYDeg = fovDeg;
        mIsProjDirty = true;
    }
    void SetAspectRatio(float aspect)
    {
        mAspectRatio = aspect;
        mIsProjDirty = true;
    }
    void SetNearZ(float nearZ)
    {
        mNearZ = nearZ;
        mIsProjDirty = true;
    }
    void SetFarZ(float farZ)
    {
        mFarZ = farZ;
        mIsProjDirty = true;
    }

    //------------------------------------------------
    // 获取相机状态
    //------------------------------------------------
    float3 GetPosition() const
    {
        return mPosition;
    }
    float4 GetOrientation() const
    {
        return mOrientation;
    }

    float4x4 GetViewMatrix() const
    {
        return mViewMatrix;
    }
    float4x4 GetProjectionMatrix() const
    {
        return mProjMatrix;
    }

    //------------------------------------------------
    // 设置目标(用于平滑插值)
    //------------------------------------------------
    void SetTargetPosition(const float3 &pos)
    {
        mTargetPosition = pos;
        mIsViewDirty = true;
    }
    void SetTargetOrientation(const float4 &q)
    {
        mTargetOrientation = normalize(q);
        mIsViewDirty = true;
    }

    //------------------------------------------------
    // 旋转操作
    //------------------------------------------------
    /// <summary>
    /// 在相机自身 X 轴上做 Pitch (抬头/低头)
    /// </summary>
    void AddLocalPitch(float deg)
    {
        float rad = DegToRad(deg);
        float3 localX = rotateVector(mTargetOrientation, float3(1, 0, 0));
        auto q = rotate_angle_axis(rad, localX);
        mTargetOrientation = normalize(q * mTargetOrientation);
        mIsViewDirty = true;
    }

    /// <summary>
    /// 在全局 Y 轴上做 Yaw (左右转头)
    /// </summary>
    void AddGlobalYaw(float deg)
    {
        float rad = DegToRad(deg);
        auto q = rotate_angle_axis(rad, float3(0, 1, 0));
        mTargetOrientation = normalize(q * mTargetOrientation);
        mIsViewDirty = true;
    }

    //------------------------------------------------
    // 移动操作
    //------------------------------------------------
    /// <summary>
    /// 向前/后移动 (相机的 +Z/-Z 方向)
    /// </summary>
    void MoveForward(float distance)
    {
        // forward = orientation*(0,0,1) (LH相机面向+Z)
        float3 fwd = rotateVector(mTargetOrientation, float3(0, 0, 1));
        mTargetPosition += fwd * distance;
        mIsViewDirty = true;
    }

    /// <summary>
    /// 向右/左移动 (相机的 +X/-X 方向)
    /// </summary>
    void MoveRight(float distance)
    {
        float3 rgt = rotateVector(mTargetOrientation, float3(1, 0, 0));
        mTargetPosition += rgt * distance;
        mIsViewDirty = true;
    }

    /// <summary>
    /// 向上/下移动 (可选世界Y或局部Y)
    /// </summary>
    void MoveUp(float distance)
    {
        // 这里演示世界Y
        mTargetPosition += float3(0, 1, 0) * distance;
        mIsViewDirty = true;
    }

    //------------------------------------------------
    // 平滑因子
    //------------------------------------------------
    void SetPositionSmoothFactor(float factor)
    {
        mPositionSmoothFactor = factor;
    }
    void SetOrientationSmoothFactor(float factor)
    {
        mOrientationSmoothFactor = factor;
    }

    //------------------------------------------------
    // 每帧更新
    //------------------------------------------------
    void Update(float deltaTime)
    {
        // 1) 位置插值
        {
            float blendPos = 1.0f - std::exp(-mPositionSmoothFactor * deltaTime);
            mPosition = mPosition + (mTargetPosition - mPosition) * blendPos;
        }
        // 2) 旋转插值 (Slerp)
        {
            float blendRot = 1.0f - std::exp(-mOrientationSmoothFactor * deltaTime);
            mOrientation = q_slerp(mOrientation, mTargetOrientation, blendRot);
            mOrientation = normalize(mOrientation);
        }

        // 更新矩阵
        if (mIsProjDirty)
        {
            UpdateProjectionMatrix();
        }
        if (mIsViewDirty)
        {
            UpdateViewMatrix();
        }
    }

private:
    /// <summary>
    /// 更新投影矩阵（左手系 + ReversedZ）
    ///
    /// 确保 nearZ 对应 depth=1.0, farZ 对应 depth=0.0
    /// 并且把相机看向 +Z
    ///
    /// 常见LH(传统Z=0~1)是:
    /// [ w,  0,  0,  0 ]
    /// [ 0,  h,  0,  0 ]
    /// [ 0,  0,  f/(f-n), 1 ]
    /// [ 0,  0,  -fn/(f-n), 0 ]
    ///
    /// 反转Z => near->1, far->0 => 令:
    /// [ w,  0,  0,  0 ]
    /// [ 0,  h,  0,  0 ]
    /// [ 0,  0,  n/(n-f), 1 ]
    /// [ 0,  0,  f*n/(n-f), 0 ]
    ///
    /// 需要在渲染管线中使用Greater/GreaterEqual深度测试，并清深度=0
    /// </summary>
    void UpdateProjectionMatrix()
    {
        float fovYRad = DegToRad(mFovYDeg);
        float aspect = mAspectRatio;
        float nearZ = mNearZ;
        float farZ = mFarZ;

        float tanHalfFov = std::tan(fovYRad * 0.5f);
        float h = 1.0f / tanHalfFov;
        float w = h / aspect;

        // Reversed Z for LH
        float fn = (nearZ - farZ); // 注意 (n < f) => negative
        float A = nearZ / fn;
        float B = (farZ * nearZ) / fn; // 也将是负值

        mProjMatrix = float4x4(w, 0.0f, 0.0f, 0.0f, 0.0f, h, 0.0f, 0.0f, 0.0f, 0.0f, A, 1.0f, 0.0f, 0.0f, B, 0.0f);

        mIsProjDirty = false;
    }

    /// <summary>
    /// 更新视图矩阵（LH）
    ///
    /// 这里 forward= orientation*(0,0,1)，即看向+Z
    /// up = orientation*(0,1,0)
    /// center= eye+forward
    /// 视图矩阵可用 lookAtLH:
    /// Xaxis = normalize(cross(up, (center-eye)))
    /// ...
    ///
    /// 也可用RH写法，关键是要与投影矩阵保持一致
    /// 这里简单用 cross(forward, up) * -Z 之类都可以
    /// </summary>
    void UpdateViewMatrix()
    {
        float3 forward = rotateVector(mOrientation, float3(0, 0, 1));
        float3 up = rotateVector(mOrientation, float3(0, 1, 0));
        float3 eye = mPosition;
        float3 center = eye + forward;

        // 典型 lookAtLH:
        // zaxis = normalize(center - eye)
        // xaxis = normalize(cross(up, zaxis))
        // yaxis = cross(zaxis, xaxis)
        float3 zaxis = normalize(center - eye); // 正Z
        float3 xaxis = normalize(cross(up, zaxis));
        float3 yaxis = cross(zaxis, xaxis);

        mViewMatrix = float4x4(xaxis.x, yaxis.x, zaxis.x, 0.0f, xaxis.y, yaxis.y, zaxis.y, 0.0f, xaxis.z, yaxis.z, zaxis.z, 0.0f, -dot(xaxis, eye), -dot(yaxis, eye), -dot(zaxis, eye), 1.0f);

        mIsViewDirty = false;
    }

    inline float DegToRad(float deg) const
    {
        return deg * 3.1415926535f / 180.0f;
    }

private:
    // -------- 投影参数 -----------
    float mFovYDeg;
    float mAspectRatio;
    float mNearZ;
    float mFarZ;

    // -------- 相机的状态 -----------
    float3 mPosition;
    float4 mOrientation;

    // -------- 目标状态(平滑插值用) -----------
    float3 mTargetPosition;
    float4 mTargetOrientation;

    // -------- 矩阵缓存 -----------
    float4x4 mViewMatrix;
    float4x4 mProjMatrix;
    bool mIsViewDirty;
    bool mIsProjDirty;

    // -------- 平滑插值因子 -----------
    float mPositionSmoothFactor;
    float mOrientationSmoothFactor;
};


#endif // CAMERA_CONTROLLER_HPP
