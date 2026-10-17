using System;

namespace Avrcade.YapyapVr;

public static class SteadycamMath
{
    /// Time-based damping, so 60 and 120 Hz converge at the same rate.
    public static float Blend(float deltaSeconds, float timeConstantSeconds = 0.12f)
    {
        if (float.IsNaN(deltaSeconds) || float.IsInfinity(deltaSeconds) || deltaSeconds <= 0f) return 0f;
        if (float.IsNaN(timeConstantSeconds) || float.IsInfinity(timeConstantSeconds) || timeConstantSeconds <= 0f) return 1f;
        return (float)(1.0 - Math.Exp(-deltaSeconds / timeConstantSeconds));
    }

    public static bool ShouldReset(bool initialized, bool sourceChanged, float distance, float frameGap)
        => !initialized || sourceChanged || distance > 2f || frameGap > 0.5f;
}
