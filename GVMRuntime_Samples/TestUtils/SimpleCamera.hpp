#ifndef SIMPLE_CAMERA_HPP
#define SIMPLE_CAMERA_HPP

#include "UGL.h"
using namespace UGL;

#ifndef SIMPLE_CAMERA_SKIP_LWC_INCLUDE
#include "LWC.hpp"
#endif

namespace CameraMovement
{
    const int FORWARD = 1;
    const int BACKWARD = 2;
    const int LEFT = 4;
    const int RIGHT = 8;
} // namespace CameraMovement

namespace CameraType
{
    const int FPS = 1;
    const int TPS = 2;
} // namespace CameraType

// 一些默认值
const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = .00625f;
const float SENSITIVITY = 0.1f;
const float ZOOM = 1.0f;     // 如果用于 TPS，可以改成初始的 FOV，也可用作距离
const float DISTANCE = 5.0f; // TPS 相关，摄像机与目标点的默认距离

inline float rad(float deg)
{
    return deg * 3.14159265359f / 180.0f;
}

class SimpleCamera final
{
protected:
    const float3 mPos = float3(0.0f);
    float3 mFront;
    float3 mUp;
    float3 mRight;
    float3 mWorldUp;

    // 欧拉角
    float mYaw;
    float mPitch;

    // 相机选项
    float mMovementSpeed;
    float mMouseSensitivity;
    float mZoom; // 在 FPS 模式下可以表示 FOV；在 TPS 模式下也可以拿来表示平滑变焦
    float3 mVelocity = float3(0.0f);

    float mDistanceTarget = 0.f;
    const float mFrictionCoefficient = 0.99f; // 简易阻力系数

    float4x4 mViewMatrix;
    const float3 mTarget = float3(0.f);

    // LWCData mLWCData;
    double3 mDoublePos = double3(0.0);
    double3 mDoubleTarget = double3(0.0);

    // 相机类型（FPS / TPS）
    int mCameraType = 0;

    // TPS 相关，摄像机和 Target 的距离
    float mDistance;
    float mDistanceVel = 0.f;

public:
    // 构造函数
    void create(int cameraType, float3 Pos = float3(0.0f, 0.0f, 0.0f), float3 up = float3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH, float dist = DISTANCE)
    {
        mMovementSpeed = SPEED;
        mMouseSensitivity = SENSITIVITY;
        mZoom = ZOOM;
        mCameraType = cameraType;
        mDistance = dist;
        mDoublePos = double3(double(Pos.x), double(Pos.y), double(Pos.z));
        mDoubleTarget = mDoublePos;
        mWorldUp = up;
        mYaw = yaw;
        mPitch = pitch;

        // 初始时，Target 可以理解为“注视点”或玩家位置
        // 如果是 TPS，可以让它在初始时在摄像机前方 dist 距离处
        // mTarget = float3(mPos.x, mPos.y, mPos.z - 5.0f);
        // mLWCData.fromFloat3(0.f);

        updateCameraVectors();
    }

    float4x4 getViewMatrix() const
    {
        return mViewMatrix;
    }

    LWCData getLWCPos() const
    {
        LWCData mLWCData = LWCFromDouble3(mDoublePos);

        return mLWCData;
    }

    LWCData getLWCTarget() const
    {
        LWCData mLWCData = LWCFromDouble3(mDoubleTarget);

        return mLWCData;
    }

    double3 getDoublePos() const
    {
        return mDoublePos;
    }

    double3 getDoubleTarget() const
    {
        return mDoubleTarget;
    }

    /**
     * Sets the movement speed multiplier used by keyboard-driven camera motion.
     *
     * Use this from sample UI code when an interactive camera needs runtime speed tuning. Values are clamped to a
     * practical positive range so existing acceleration and damping code remains stable.
     */
    void setMovementSpeed(float movementSpeed)
    {
        mMovementSpeed = clamp(movementSpeed, 0.00001f, 1.0f);
    }

    /**
     * Returns the movement speed used by keyboard-driven camera motion.
     *
     * This value is the per-millisecond base speed consumed by processKeyboard, not a world-units-per-second value.
     */
    float getMovementSpeed() const
    {
        return mMovementSpeed;
    }

    /**
     * Returns the normalized forward direction used by the current camera update.
     */
    float3 getFrontDirection() const
    {
        return mFront;
    }

    /**
     * Returns the normalized up direction used by the current camera update.
     */
    float3 getUpDirection() const
    {
        return mUp;
    }

    // 处理键盘输入（前后左右）
    void processKeyboard(int direction, float deltaTime)
    {
        // “相当于移动键盘时相机 target 移动”
        // 这里保持原本的加速度/摩擦力逻辑，仅把移动作用在 mTarget 上

        float3 tempV = float3(0.f);
        if (direction != 0)
        {
            if (direction & CameraMovement::FORWARD)
                tempV += mFront; // 前
            if (direction & CameraMovement::BACKWARD)
                tempV -= mFront; // 后
            if (direction & CameraMovement::LEFT)
                tempV -= mRight; // 左
            if (direction & CameraMovement::RIGHT)
                tempV += mRight; // 右

            tempV = normalize(tempV) * mMovementSpeed;
        }
        // 简易减速 / 摩擦
        mVelocity = lerp(tempV, mVelocity, pow(1 - mFrictionCoefficient, deltaTime * 0.001f));

        // 让 mTarget 移动
        // mTarget += mVelocity * deltaTime;
        mDoubleTarget += double3(double(mVelocity.x), double(mVelocity.y), double(mVelocity.z)) * double(deltaTime);
    }

    // 处理鼠标移动，xoffset/yoffset 分别是鼠标在屏幕上移动的像素偏移
    void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true)
    {
        xoffset *= mMouseSensitivity;
        yoffset *= mMouseSensitivity;

        mYaw += xoffset;
        mPitch -= yoffset; // 一般往上拉鼠标，pitch 是变小的，所以这里用减号

        // 限制俯仰角，避免过度翻转
        if (constrainPitch)
        {
            mPitch = clamp((float)mPitch, -89.0f, 89.0f);
        }
    }

    // 鼠标滚轮
    void processMouseScroll(float yoffset, float deltaTime)
    {
        float tempV = -yoffset;
        // 如果想把滚轮当做“距离调节”，可直接操作 mDistance
        // 也可保留原逻辑，把 mZoom 当作 FOV 或平滑缩放用
        /*
        mZoom -= (float)yoffset;
        if (mZoom < 1.0f)  mZoom = 1.0f;
        if (mZoom > 45.0f) mZoom = 45.0f;
        */

        // 下面示例：滚轮用于调节相机和目标点之间的距离

        // mDistanceTarget -= yoffset;

        // mDistanceTarget = clamp(mDistanceTarget, 1.0f, 1000.f);

        // 让速度根据 (目标 - 当前) 做加速
        mDistanceVel = lerp(tempV, mDistanceVel, pow(1 - mFrictionCoefficient, deltaTime * 0.001f));
        // 用速度 * deltaTime 移动当前距离
        mDistance += mDistanceVel * deltaTime;

        mDistance = clamp(mDistance, 1.0f, 10000.f);
        // 速度做阻尼衰减
        // mDistanceVel *= 0.9f;

        // float smoothFactor = 0.1f; // 每帧 0.1f 的插值，或可随 deltaTime 调整
        // mDistance = lerp(mDistance, mDistanceTarget, pow(1 - mFrictionCoefficient, deltaTime * 0.01f));
    }

    // 每帧更新相机
    void update()
    {
        updateCameraVectors();
    }

private:
    // FPS 的更新逻辑
    void updateFPS()
    {
        // 在 FPS 模式下，直接让相机位置等于 target
        // （当然也可根据实际项目需要来决定相机与角色之间的相对关系）
        // mPos = mTarget;
        mDoublePos = mDoubleTarget;
        float3 floatDoublePos = float3((float)mDoublePos.x, (float)mDoublePos.y, (float)mDoublePos.z);

        // 计算新的 Front 向量
        float3 front;
        front.x = cos(rad(mYaw)) * cos(rad(mPitch));
        front.y = sin(rad(mPitch));
        front.z = sin(rad(mYaw)) * cos(rad(mPitch));
        mFront = normalize(front);

        // 更新 Right / Up
        mRight = normalize(cross(mFront, mWorldUp));
        mUp = normalize(cross(mRight, mFront));

        // 利用 LookAt
        mViewMatrix = lookAt((float3)floatDoublePos, (float3)floatDoublePos + mFront, mUp);
    }

    // TPS 的更新逻辑（关键）
    void updateTPS()
    {
        // 通过 yaw / pitch 得到“相机相对目标点”的偏移
        float3 offset;
        offset.x = cos(rad(mYaw)) * cos(rad(mPitch));
        offset.y = sin(rad(mPitch));
        offset.z = sin(rad(mYaw)) * cos(rad(mPitch));

        // 放大到指定的距离
        offset *= mDistance;

        // 相机位置 = 目标点 + 偏移
        // mPos = mTarget + offset;
        mDoublePos = mDoubleTarget + double3(double(offset.x), double(offset.y), double(offset.z));

        // 重新计算相机的前方向：看向 mTarget
        mFront = normalize(-offset);

        // 重新计算 Right / Up
        mRight = normalize(cross(mFront, mWorldUp));
        mUp = normalize(cross(mRight, mFront));

        // 计算视图矩阵
        mViewMatrix = lookAt(mPos, -offset, mUp);
    }

    // 根据相机类型分别更新
    void updateCameraVectors()
    {
        if (mCameraType == CameraType::FPS)
        {
            updateFPS();
        }
        else if (mCameraType == CameraType::TPS)
        {
            updateTPS();
        }
    }
};

#endif // SIMPLE_CAMERA_HPP
