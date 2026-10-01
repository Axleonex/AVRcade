using System;
using System.Numerics;

namespace VrClient.UnityVR.Logic;

/// Game-agnostic VR pose + stereo math. Pure, deterministic, unit-tested — the
/// framework's correctness floor before it ever touches a headset. Uses
/// System.Numerics row-vector convention: transform a point with
/// Vector4.Transform(v, M) / Vector3.Transform(v, M).
public static class PoseMath
{
    /// World->eye (view) transform for a headset at `position` with `orientation`.
    /// The eye's world transform is Rotation then Translation; the view is its inverse,
    /// so Vector3.Transform(worldPoint, view) yields the point in eye-local space.
    public static Matrix4x4 ViewFromPose(Vector3 position, Quaternion orientation)
    {
        var world = Matrix4x4.CreateFromQuaternion(orientation) * Matrix4x4.CreateTranslation(position);
        Matrix4x4.Invert(world, out var view);
        return view;
    }

    /// The forward direction (world space) a headset with `orientation` is looking
    /// along. Unity/OpenXR forward is -Z rotated by the orientation.
    public static Vector3 ForwardFromOrientation(Quaternion orientation)
        => Vector3.Transform(-Vector3.UnitZ, orientation);

    /// OpenXR asymmetric-frustum projection from per-eye field-of-view ANGLES (radians,
    /// as OpenXR's XrFovf gives them: left/down negative, right/up positive), OpenGL
    /// clip-depth convention (-1..1). Row-vector layout so clip = Vector4.Transform(v, P).
    /// Mirrors Khronos xr_linear.h XrMatrix4x4f_CreateProjectionFov (GL).
    public static Matrix4x4 ProjectionFromFov(
        float angleLeft, float angleRight, float angleUp, float angleDown, float near, float far)
    {
        var tanLeft = MathF.Tan(angleLeft);
        var tanRight = MathF.Tan(angleRight);
        var tanUp = MathF.Tan(angleUp);
        var tanDown = MathF.Tan(angleDown);

        var tanWidth = tanRight - tanLeft;
        var tanHeight = tanUp - tanDown;
        var depth = far - near;

        // Column-major GL matrix Pgl transposed into System.Numerics row-vector form
        // (M.Mij = Pgl[j-1][i-1]); zeros elsewhere.
        var m = new Matrix4x4();
        m.M11 = 2f / tanWidth;
        m.M22 = 2f / tanHeight;
        m.M31 = (tanRight + tanLeft) / tanWidth;
        m.M32 = (tanUp + tanDown) / tanHeight;
        m.M33 = -(far + near) / depth;
        m.M34 = -1f;
        m.M43 = -(2f * far * near) / depth;
        return m;
    }
}
