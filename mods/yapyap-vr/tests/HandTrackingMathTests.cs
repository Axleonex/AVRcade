using System;
using System.Numerics;
using Avrcade.YapyapVr;
using Xunit;

namespace YapyapVrControls.Tests;

public sealed class HandTrackingMathTests
{
    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void VisiblePalmCenterNotItemAttachmentLandsAtTheController(bool left)
    {
        var middle = new Vector3(left ? 0.00878318f : -0.00878312f, 0.1117982f, 0.0232337f);
        var palm = middle * 0.5f;
        var attachment = left ? new Vector3(-0.027f, 0.087f, -0.068f) : new Vector3(0.0408413f, 0.0528409f, -0.084883f);
        Assert.True(HandTrackingMath.TryPalmCenter(middle, out var anatomicalPalm));
        // Inside the actual skin's wrist/palm region, unlike the item anchor.
        Assert.InRange(anatomicalPalm.Y, 0.009f, 0.104f);
        Assert.InRange(anatomicalPalm.Z, -0.042f, 0.057f);
        Assert.True(Vector3.Distance(anatomicalPalm, attachment) > 0.07f);
        var head = Pose(Vector3.Zero);
        var controller = Pose(new Vector3(0.3f, -0.3f, 0.4f));
        var rotation = Quaternion.CreateFromYawPitchRoll(0.3f, -0.4f, 0.5f);
        Assert.True(HandTrackingMath.TryToWorldGrip(head, controller, head, Vector3.Zero,
            rotation, anatomicalPalm, out var wrist));
        Near(controller.Position, wrist.Position + Vector3.Transform(palm, wrist.Rotation));
    }

    [Fact]
    public void KnuckleBiasedReferencePullsVisiblePalmBackFromControllerGrip()
    {
        var middle = new Vector3(0.00878318f, 0.1117982f, 0.0232337f);
        var controller = Pose(new Vector3(0.3f, -0.3f, 0.4f));
        var head = Pose(Vector3.Zero);
        Assert.True(HandTrackingMath.TryWristAlignment(middle,
            new Vector3(-0.02642866f, 0.03246836f, 0.0446704f), Quaternion.Identity, true, out var rotation));
        Assert.True(HandTrackingMath.TryToWorldGrip(head, controller, head, Vector3.Zero,
            rotation, middle * 0.7f, out var oldWrist));
        var oldVisiblePalm = oldWrist.Position + Vector3.Transform(middle * 0.5f, oldWrist.Rotation);
        Assert.InRange(controller.Position.Z - oldVisiblePalm.Z, 0.02f, 0.025f);
        Assert.True(HandTrackingMath.TryPalmCenter(middle, out var palm));
        Assert.True(HandTrackingMath.TryToWorldGrip(head, controller, head, Vector3.Zero,
            rotation, palm, out var wrist));
        Near(controller.Position, wrist.Position + Vector3.Transform(middle * 0.5f, wrist.Rotation));
        Assert.Equal(oldWrist.Rotation, wrist.Rotation);
    }

    [Fact]
    public void StereoAnchorPreservesControllerSpacingAfterHeadLeanAndGameTurn()
    {
        var center = new Vector3(10.04f, 1.6f, 20.03f);
        var yaw = Quaternion.CreateFromAxisAngle(Vector3.UnitY, 1.2f);
        var eyeOffset = Vector3.Transform(Vector3.UnitX * 0.032f, yaw);
        Assert.True(HandTrackingMath.TryStereoEyeCenter(center - eyeOffset, center + eyeOffset, out var eyeCenter));
        Near(center, eyeCenter);
        var trackingHead = Pose(new Vector3(0.04f, 1.6f, 0.03f));
        var left = Pose(trackingHead.Position + new Vector3(-0.35f, -0.4f, 0.4f));
        var right = Pose(trackingHead.Position + new Vector3(0.35f, -0.4f, 0.4f));
        var worldHead = Pose(eyeCenter, yaw);
        Assert.True(HandTrackingMath.TryToWorld(trackingHead, left, worldHead, Vector3.Zero, Quaternion.Identity, out var worldLeft));
        Assert.True(HandTrackingMath.TryToWorld(trackingHead, right, worldHead, Vector3.Zero, Quaternion.Identity, out var worldRight));
        Assert.InRange(Vector3.Distance(worldLeft.Position, worldRight.Position), 0.6999f, 0.7001f);
        Near(eyeCenter + Vector3.Transform(left.Position - trackingHead.Position, yaw), worldLeft.Position);
    }

    [Theory]
    [InlineData(0f, 0f, 0f)]
    [InlineData(float.NaN, 0f, 0f)]
    [InlineData(float.PositiveInfinity, 0f, 0f)]
    [InlineData(1f, 0f, 0f)]
    public void InvalidStereoEyesDoNotOverrideTheCameraAnchor(float x, float y, float z)
        => Assert.False(HandTrackingMath.TryStereoEyeCenter(Vector3.Zero, new Vector3(x, y, z), out _));

    [Fact]
    public void HandGripPositionCorrectsRawOriginWithoutChangingConfirmedRotation()
    {
        var raw = Pose(new Vector3(0.2f, -0.3f, 0.3f), Quaternion.CreateFromYawPitchRoll(0.4f, -0.2f, 0.1f));
        var grip = Pose(new Vector3(0.2f, -0.4f, 0.45f), Quaternion.CreateFromYawPitchRoll(0.7f, 0.1f, 0.5f));
        Assert.True(HandTrackingMath.TryControllerGripPose(raw, grip, out var controller));
        Near(grip.Position, controller.Position);
        Assert.Equal(raw.Rotation, controller.Rotation);
        Assert.False(HandTrackingMath.TryControllerGripPose(raw, Pose(new Vector3(10, 0, 0)), out _));
        Assert.False(HandTrackingMath.TryPalmCenter(Vector3.Zero, out _));
    }
    [Theory]
    [InlineData(-0.02699992f, 0.08700035f, -0.06799958f, 1f)]
    [InlineData(0.04084133f, 0.05284094f, -0.08488304f, 1f)]
    [InlineData(-0.02699992f, 0.08700035f, -0.06799958f, 0.8f)]
    [InlineData(0.04084133f, 0.05284094f, -0.08488304f, 0.8f)]
    public void ControllerGripTargetsNativePalmAttachmentInsteadOfWristJoint(float x, float y, float z, float scale)
    {
        var head = Pose(new Vector3(2, 1.6f, 4));
        var controller = Pose(head.Position + new Vector3(0.3f, -0.4f, 0.4f),
            Quaternion.CreateFromYawPitchRoll(0.5f, -0.3f, 0.1f));
        var worldHead = Pose(new Vector3(10, 3, 20),
            Quaternion.CreateFromAxisAngle(Vector3.UnitY, 1.2f));
        var wristOffset = Quaternion.CreateFromYawPitchRoll(-0.7f, 0.2f, 0.9f);
        var nativeGrip = new Vector3(x, y, z) * scale;
        Assert.True(HandTrackingMath.TryToWorldGrip(head, controller, worldHead,
            Vector3.Zero, wristOffset, nativeGrip, out var wrist));
        Assert.True(HandTrackingMath.TryToWorld(head, controller, worldHead,
            Vector3.Zero, Quaternion.Identity, out var trackedGrip));
        Near(trackedGrip.Position, wrist.Position + Vector3.Transform(nativeGrip, wrist.Rotation));
        // The wrist keeps the already-correct controller-to-hand rotation.
        Assert.True(Math.Abs(Quaternion.Dot(wrist.Rotation,
            Quaternion.Normalize(trackedGrip.Rotation * wristOffset))) > 0.9999f);
    }

    [Theory]
    [InlineData(float.NaN, 0f, 0f)]
    [InlineData(0f, 0f, 1f)]
    public void InvalidNativeGripCannotMoveTheWrist(float x, float y, float z)
    {
        var head = Pose(Vector3.Zero);
        Assert.False(HandTrackingMath.TryToWorldGrip(head, head, head, Vector3.Zero,
            Quaternion.Identity, new Vector3(x, y, z), out _));
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void GripBasisKeepsBackOfHandOnControllerUpperSide(bool left)
    {
        // Open hand, fingers forward, thumb toward the opposite hand.
        var finger = Vector3.UnitZ;
        var thumb = left ? Vector3.UnitX : -Vector3.UnitX;
        Assert.True(HandTrackingMath.TryWristAlignment(finger, thumb, Quaternion.Identity, left, out var offset));
        var controller = Quaternion.CreateFromYawPitchRoll(0.4f, -0.3f, 0.7f);
        Near(Vector3.Transform(Vector3.UnitY, controller),
            Vector3.Transform(Vector3.UnitY, controller * offset));
        Near(Vector3.Transform(Vector3.UnitZ, controller),
            Vector3.Transform(Vector3.UnitZ, controller * offset));
    }

    [Theory]
    [InlineData(1f)]
    [InlineData(0.1f)]
    public void NativeSmallFingerGeometryProducesRealWristAlignment(float scale)
    {
        // Serialized WizKid left middle/thumb root offsets from sharedassets2.
        var finger = new Vector3(0.00878318f, 0.11179825f, 0.02323368f) * scale;
        var thumb = new Vector3(-0.02642866f, 0.03246836f, 0.0446704f) * scale;
        Assert.True(HandTrackingMath.TryWristAlignment(finger, thumb, Quaternion.Identity, true, out var offset));
        // Aligned wrist carries its finger direction along the controller's forward axis.
        Near(Vector3.UnitZ, Vector3.Transform(Vector3.Normalize(finger), offset));
        Assert.True(Math.Abs(Quaternion.Dot(offset, Quaternion.Identity)) < 0.99f);
    }

    [Fact]
    public void DegenerateFingerGeometryDefersToNativeAlignment()
    {
        Assert.False(HandTrackingMath.TryWristAlignment(Vector3.Zero, Vector3.UnitX, Quaternion.Identity, true, out _));
        Assert.False(HandTrackingMath.TryWristAlignment(Vector3.UnitZ, Vector3.UnitZ, Quaternion.Identity, false, out _));
        Assert.False(HandTrackingMath.TryWristAlignment(new Vector3(float.NaN, 0, 0), Vector3.UnitX, Quaternion.Identity, true, out _));
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void WristAlignmentDoesNotDependOnAvatarWorldRotation(bool left)
    {
        var finger = Vector3.Normalize(new Vector3(0.01f, 0.112f, 0.023f));
        var thumb = Vector3.Normalize(new Vector3(left ? -0.026f : 0.026f, 0.032f, 0.045f));
        var native = Quaternion.CreateFromYawPitchRoll(1.3f, -0.4f, 0.2f);
        Assert.True(HandTrackingMath.TryWristAlignment(Vector3.Transform(finger, native),
            Vector3.Transform(thumb, native), native, left, out var offset));
        Near(Vector3.UnitZ, Vector3.Transform(finger, offset));
    }

    private static HandPose Pose(Vector3 p, Quaternion? q = null) => new(p, q ?? Quaternion.Identity);
    private static void Near(Vector3 expected, Vector3 actual) =>
        Assert.InRange(Vector3.Distance(expected, actual), 0f, 0.0001f);

    [Fact]
    public void OpenVrTranslationAndRotationConvertHandedness()
    {
        Assert.True(HandTrackingMath.TryFromOpenVr(0, 0, 1, 0.3f, 0, 1, 0, 1.2f,
            -1, 0, 0, -0.4f, out var pose));
        Near(new Vector3(0.3f, 1.2f, 0.4f), pose.Position);
        Near(-Vector3.UnitX, Vector3.Transform(Vector3.UnitZ, pose.Rotation));
    }

    [Fact]
    public void GameYawRotatesHandPositionAndOrientationExactlyOnce()
    {
        var headRotation = Quaternion.CreateFromAxisAngle(Vector3.UnitY, 0.7f);
        var gameYaw = Quaternion.CreateFromAxisAngle(Vector3.UnitY, (float)Math.PI / 2);
        var head = Pose(new Vector3(3, 2, 4), headRotation);
        var hand = Pose(head.Position + new Vector3(0.2f, -0.3f, 0.5f));
        Assert.True(HandTrackingMath.TryToWorld(head, hand,
            Pose(new Vector3(10, 5, 20), gameYaw * headRotation), Vector3.Zero,
            Quaternion.Identity, out var result));
        Near(new Vector3(10.5f, 4.7f, 19.8f), result.Position);
        Near(Vector3.UnitX, Vector3.Transform(Vector3.UnitZ, result.Rotation));
    }

    [Fact]
    public void TurningHeadDoesNotOrbitStationaryHands()
    {
        var hand = Pose(new Vector3(0.4f, 1.3f, 0.5f));
        var head = Pose(new Vector3(0, 1.6f, 0));
        Assert.True(HandTrackingMath.TryToWorld(head, hand, head, Vector3.Zero, Quaternion.Identity, out var before));
        head = Pose(head.Position, Quaternion.CreateFromAxisAngle(Vector3.UnitY, 1.2f));
        Assert.True(HandTrackingMath.TryToWorld(head, hand, head, Vector3.Zero, Quaternion.Identity, out var after));
        Near(before.Position, after.Position);
        Near(Vector3.UnitZ, Vector3.Transform(Vector3.UnitZ, after.Rotation));
    }

    [Fact]
    public void RecenteredTrackingOriginKeepsBothHandsInTheSameWorldPlace()
    {
        var head = Pose(new Vector3(0, 1.6f, 0));
        var hand = Pose(new Vector3(-0.4f, 1.2f, 0.6f));
        var worldHead = Pose(new Vector3(7, 3, 12));
        Assert.True(HandTrackingMath.TryToWorld(head, hand, worldHead, Vector3.Zero, Quaternion.Identity, out var a));
        var shift = new Vector3(40, -9, 70);
        Assert.True(HandTrackingMath.TryToWorld(Pose(head.Position + shift), Pose(hand.Position + shift),
            worldHead, Vector3.Zero, Quaternion.Identity, out var b));
        Near(a.Position, b.Position);
    }

    [Fact]
    public void GripOffsetFollowsControllerRotation()
    {
        var hand = Pose(Vector3.Zero, Quaternion.CreateFromAxisAngle(Vector3.UnitY, (float)Math.PI / 2));
        Assert.True(HandTrackingMath.TryToWorld(Pose(Vector3.Zero), hand, Pose(Vector3.Zero),
            new Vector3(0, 0, 0.1f), Quaternion.Identity, out var result));
        Near(new Vector3(0.1f, 0, 0), result.Position);
    }

    [Fact]
    public void InvalidOrImplausiblePosesCannotReachTheRig()
    {
        var head = Pose(Vector3.Zero);
        foreach (var hand in new[] { Pose(new Vector3(float.NaN, 0, 0)),
            Pose(new Vector3(float.PositiveInfinity, 0, 0)), Pose(Vector3.Zero, default(Quaternion)),
            Pose(new Vector3(3, 0, 0)) })
            Assert.False(HandTrackingMath.TryToWorld(head, hand, head, Vector3.Zero, Quaternion.Identity, out _));
        Assert.False(HandTrackingMath.TryFromOpenVr(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, out _));
        Assert.False(HandTrackingMath.TryFromOpenVr(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, out _));
    }

    [Theory]
    [InlineData(false, true, true, true, true, true, 0f)]
    [InlineData(true, false, true, true, true, true, 0f)]
    [InlineData(true, true, false, true, true, true, 0f)]
    [InlineData(true, true, true, false, true, true, 0f)]
    [InlineData(true, true, true, true, false, true, 0f)]
    [InlineData(true, true, true, true, true, false, 0f)]
    [InlineData(true, true, true, true, true, true, 0.26f)]
    [InlineData(true, true, true, true, true, true, -0.01f)]
    [InlineData(true, true, true, true, true, true, float.NaN)]
    public void RemoteDeadSpecialStateDashboardDisconnectedOrStaleHandsUseNativeAnimation(
        bool local, bool alive, bool native, bool scene, bool connected, bool valid, float age)
        => Assert.False(HandTrackingMath.CanTrack(local, alive, native, scene, connected, valid, age));

    [Fact]
    public void FreshLocalTrackedPoseIsAccepted() =>
        Assert.True(HandTrackingMath.CanTrack(true, true, true, true, true, true, 0.02f));
}
