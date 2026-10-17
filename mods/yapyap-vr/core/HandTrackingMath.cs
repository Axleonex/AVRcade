using System;
using System.Numerics;

namespace Avrcade.YapyapVr;

public readonly struct HandPose
{
    public Vector3 Position { get; }
    public Quaternion Rotation { get; }
    public HandPose(Vector3 position, Quaternion rotation)
    {
        Position = position;
        Rotation = rotation;
    }
}

/// Coordinate conversion and validation, independent of the Unity/player lifecycle.
public static class HandTrackingMath
{
    public static bool TryPalmCenter(Vector3 wristToMiddleKnuckle, out Vector3 palm)
    {
        palm = default;
        if (!IsValid(new HandPose(wristToMiddleKnuckle, Quaternion.Identity)) ||
            wristToMiddleKnuckle.LengthSquared() < 0.00000001f ||
            wristToMiddleKnuckle.LengthSquared() > 0.25f) return false;
        // Locate the visible palm, not the knuckles. The previous 0.7 reference
        // put the palm midpoint ~2cm behind the physical controller grip.
        palm = wristToMiddleKnuckle * 0.5f;
        return true;
    }

    public static bool TryStereoEyeCenter(Vector3 leftEye, Vector3 rightEye, out Vector3 center)
    {
        center = default;
        if (!IsValid(new HandPose(leftEye, Quaternion.Identity)) ||
            !IsValid(new HandPose(rightEye, Quaternion.Identity))) return false;
        var separation = Vector3.DistanceSquared(leftEye, rightEye);
        if (separation < 0.0001f || separation > 0.04f) return false;
        center = leftEye + (rightEye - leftEye) * 0.5f;
        return IsValid(new HandPose(center, Quaternion.Identity));
    }

    public static bool TryControllerGripPose(HandPose raw, HandPose grip, out HandPose controller)
    {
        controller = default;
        if (!IsValid(raw) || !IsValid(grip) || Vector3.DistanceSquared(raw.Position, grip.Position) > 0.25f) return false;
        // Keep the rotation already confirmed by the user, while locating the
        // physical grip via the driver's handgrip frame rather than raw origin.
        controller = new HandPose(grip.Position, raw.Rotation);
        return true;
    }

    public static bool TryWristAlignment(Vector3 fingerDirection, Vector3 thumbDirection,
        Quaternion nativeRotation, bool left, out Quaternion offset)
    {
        offset = Quaternion.Identity;
        if (!IsValid(new HandPose(fingerDirection, nativeRotation)) ||
            !IsValid(new HandPose(thumbDirection, Quaternion.Identity)) ||
            fingerDirection.LengthSquared() < 0.00000001f || thumbDirection.LengthSquared() < 0.00000001f) return false;
        var forward = Vector3.Normalize(fingerDirection);
        // Test angular degeneracy on unit vectors. Raw cross products have
        // units of m²/m³; a fixed cutoff rejected this avatar's small fingers.
        // The controller's upper side corresponds to the back of the hand.
        // The opposite cross-product order used the palm normal and rolled
        // both native wrists 180 degrees around their finger direction.
        var up = Vector3.Cross(Vector3.Normalize(thumbDirection), forward) * (left ? -1f : 1f);
        if (up.LengthSquared() < 0.000001f) return false;
        var right = Vector3.Normalize(Vector3.Cross(up, forward));
        up = Vector3.Cross(forward, right);
        var matrix = new Matrix4x4(right.X, right.Y, right.Z, 0,
            up.X, up.Y, up.Z, 0, forward.X, forward.Y, forward.Z, 0, 0, 0, 0, 1);
        var grip = Quaternion.Normalize(Quaternion.CreateFromRotationMatrix(matrix));
        offset = Quaternion.Normalize(Quaternion.Inverse(grip) * nativeRotation);
        return IsValid(new HandPose(Vector3.Zero, offset));
    }

    public static bool IsFinite(float value) => !float.IsNaN(value) && !float.IsInfinity(value);
    public static bool IsValid(HandPose pose) =>
        IsFinite(pose.Position.X) && IsFinite(pose.Position.Y) && IsFinite(pose.Position.Z) &&
        IsFinite(pose.Rotation.X) && IsFinite(pose.Rotation.Y) &&
        IsFinite(pose.Rotation.Z) && IsFinite(pose.Rotation.W) &&
        Math.Abs(pose.Rotation.LengthSquared() - 1f) < 0.01f;

    public static bool CanTrack(bool localPlayer, bool alive, bool nativeHandState,
        bool sceneInput, bool connected, bool poseValid, float ageSeconds) =>
        localPlayer && alive && nativeHandState && sceneInput && connected && poseValid &&
        IsFinite(ageSeconds) && ageSeconds >= 0f && ageSeconds <= 0.25f;

    // OpenVR's 3x4 right-handed matrix becomes a Unity left-handed pose.
    public static bool TryFromOpenVr(float m0, float m1, float m2, float m3,
        float m4, float m5, float m6, float m7, float m8, float m9, float m10, float m11,
        out HandPose pose)
    {
        pose = default;
        var right = new Vector3(m0, m4, -m8);
        var up = new Vector3(m1, m5, -m9);
        var forward = new Vector3(-m2, -m6, m10);
        if (!Unit(right) || !Unit(up) || !Unit(forward) ||
            Math.Abs(Vector3.Dot(right, up)) > 0.01f ||
            Math.Abs(Vector3.Dot(right, forward)) > 0.01f ||
            Math.Abs(Vector3.Dot(up, forward)) > 0.01f ||
            Vector3.Dot(Vector3.Cross(right, up), forward) < 0.99f) return false;
        var matrix = new Matrix4x4(right.X, right.Y, right.Z, 0,
            up.X, up.Y, up.Z, 0, forward.X, forward.Y, forward.Z, 0, 0, 0, 0, 1);
        pose = new HandPose(new Vector3(m3, m7, -m11),
            Quaternion.Normalize(Quaternion.CreateFromRotationMatrix(matrix)));
        return IsValid(pose);
    }

    public static bool TryToWorld(HandPose head, HandPose hand, HandPose worldHead,
        Vector3 gripOffset, Quaternion wristOffset, out HandPose worldHand)
    {
        worldHand = default;
        if (!IsValid(head) || !IsValid(hand) || !IsValid(worldHead) ||
            !IsValid(new HandPose(gripOffset, wristOffset)) ||
            Vector3.DistanceSquared(hand.Position, head.Position) > 6.25f ||
            gripOffset.LengthSquared() > 0.25f) return false;
        var originRotation = Quaternion.Normalize(worldHead.Rotation * Quaternion.Inverse(head.Rotation));
        var handRotation = Quaternion.Normalize(originRotation * hand.Rotation);
        worldHand = new HandPose(worldHead.Position +
            Vector3.Transform(hand.Position - head.Position, originRotation) +
            Vector3.Transform(gripOffset, handRotation),
            Quaternion.Normalize(handRotation * wristOffset));
        return IsValid(worldHand);
    }

    public static bool TryToWorldGrip(HandPose head, HandPose controller, HandPose worldHead,
        Vector3 controllerOffset, Quaternion wristOffset, Vector3 nativeGripOffset, out HandPose worldWrist)
    {
        worldWrist = default;
        if (!IsValid(new HandPose(nativeGripOffset, Quaternion.Identity)) ||
            nativeGripOffset.LengthSquared() > 0.25f ||
            !TryToWorld(head, controller, worldHead, controllerOffset, wristOffset, out var grip)) return false;
        // Controller poses locate the held grip in the palm, not the avatar's
        // wrist joint. Solve the wrist so its authored grip attachment lands
        // on that tracked point, using the already-correct wrist rotation.
        worldWrist = new HandPose(grip.Position - Vector3.Transform(nativeGripOffset, grip.Rotation), grip.Rotation);
        return IsValid(worldWrist);
    }

    private static bool Unit(Vector3 v) => IsFinite(v.X) && IsFinite(v.Y) && IsFinite(v.Z) &&
        Math.Abs(v.LengthSquared() - 1f) < 0.01f;
}
