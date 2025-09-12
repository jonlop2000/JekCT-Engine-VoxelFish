#pragma once
#include <bx/math.h>
#include <cmath>

struct FlyCamera {
    float pos[3] = {0.0f, 0.0f, -5.0f};
    float yaw = 0.0f;    // radians
    float pitch = 0.0f;  // radians
    float fov = 60.0f;   // degrees
    float nearZ = 0.1f, farZ = 200.0f;

    void buildViewProj(float view[16], float proj[16], float aspect, bool homogeneousDepth) const {
        const float cy = std::cos(yaw),  sy = std::sin(yaw);
        const float cp = std::cos(pitch), sp = std::sin(pitch);

        const bx::Vec3 eye = { pos[0], pos[1], pos[2] };
        const bx::Vec3 fwd = { cy*cp, sp, sy*cp };
        const bx::Vec3 at  = { eye.x + fwd.x, eye.y + fwd.y, eye.z + fwd.z };
        const bx::Vec3 up  = { 0.0f, 1.0f, 0.0f };

        bx::mtxLookAt(view, eye, at, up);
        bx::mtxProj(proj, fov, aspect, nearZ, farZ, homogeneousDepth);
    }
};
