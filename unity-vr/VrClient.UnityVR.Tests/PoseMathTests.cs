using System;
using System.Numerics;
using VrClient.UnityVR.Logic;
using Xunit;

public class PoseMathTests
{
    private const float Tol = 1e-5f;

    [Fact]
    public void Identity_pose_gives_identity_view()
    {
        var view = PoseMath.ViewFromPose(Vector3.Zero, Quaternion.Identity);
        Assert.True(Approx(view, Matrix4x4.Identity));
    }

    [Fact]
    public void Plus_X_position_translates_the_view_by_minus_X()
    {
        var view = PoseMath.ViewFromPose(new Vector3(1, 0, 0), Quaternion.Identity);
        // The world origin, seen from an eye at +X, sits at -X in eye space.
        var seen = Vector3.Transform(Vector3.Zero, view);
        Assert.True(Math.Abs(seen.X - (-1f)) < Tol);
        Assert.True(Math.Abs(seen.Y) < Tol);
        Assert.True(Math.Abs(seen.Z) < Tol);
    }

    [Fact]
    public void Ninety_degree_yaw_points_forward_along_minus_X()
    {
        // +90 degrees about +Y (right-handed) rotates forward (-Z) to -X. Documented sign: -X.
        var q = Quaternion.CreateFromAxisAngle(Vector3.UnitY, MathF.PI / 2f);
        var fwd = PoseMath.ForwardFromOrientation(q);
        Assert.True(Math.Abs(fwd.X - (-1f)) < Tol);
        Assert.True(Math.Abs(fwd.Y) < Tol);
        Assert.True(Math.Abs(fwd.Z) < Tol);
    }

    [Fact]
    public void Symmetric_fov_projects_near_plane_centre_to_ndc_zero()
    {
        var a = MathF.PI / 4f; // 45 degrees
        var proj = PoseMath.ProjectionFromFov(-a, a, a, -a, 0.1f, 100f);
        var clip = Vector4.Transform(new Vector4(0, 0, -0.1f, 1f), proj);
        Assert.True(Math.Abs(clip.X / clip.W) < Tol);
        Assert.True(Math.Abs(clip.Y / clip.W) < Tol);
    }

    [Fact]
    public void Symmetric_fov_projects_right_near_edge_to_ndc_plus_one()
    {
        var a = MathF.PI / 4f;                 // tan(45) = 1, so the right edge at near is x = near
        var proj = PoseMath.ProjectionFromFov(-a, a, a, -a, 0.1f, 100f);
        var clip = Vector4.Transform(new Vector4(0.1f, 0, -0.1f, 1f), proj);
        Assert.True(Math.Abs(clip.X / clip.W - 1f) < Tol);
    }

    private static bool Approx(Matrix4x4 x, Matrix4x4 y)
    {
        float[] a = { x.M11,x.M12,x.M13,x.M14, x.M21,x.M22,x.M23,x.M24, x.M31,x.M32,x.M33,x.M34, x.M41,x.M42,x.M43,x.M44 };
        float[] b = { y.M11,y.M12,y.M13,y.M14, y.M21,y.M22,y.M23,y.M24, y.M31,y.M32,y.M33,y.M34, y.M41,y.M42,y.M43,y.M44 };
        for (var i = 0; i < 16; i++)
            if (Math.Abs(a[i] - b[i]) >= Tol) return false;
        return true;
    }
}
