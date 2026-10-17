using Avrcade.YapyapVr;
using Xunit;

namespace YapyapVrControls.Tests;

public sealed class SteadycamMathTests
{
    [Fact]
    public void SmoothingConvergesAtTheSameRateAcrossFrameRates()
    {
        static float Follow(int hz)
        {
            var result = 0f;
            for (var i = 0; i < hz; i++) result += (1f - result) * SteadycamMath.Blend(1f / hz);
            return result;
        }
        Assert.InRange(System.Math.Abs(Follow(60) - Follow(120)), 0f, 0.00001f);
        Assert.InRange(Follow(60), 0.99f, 1f);
    }

    [Theory]
    [InlineData(false, false, 0f, 0.01f, true)]
    [InlineData(true, true, 0f, 0.01f, true)]
    [InlineData(true, false, 3f, 0.01f, true)]
    [InlineData(true, false, 0f, 1f, true)]
    [InlineData(true, false, 0.2f, 0.01f, false)]
    public void StartsAndTeleportsSnapToCurrentPose(bool initialized, bool changed, float distance, float gap, bool reset)
        => Assert.Equal(reset, SteadycamMath.ShouldReset(initialized, changed, distance, gap));

    [Fact]
    public void InvalidFrameTimeCannotPropagateNanIntoTheCamera()
    {
        Assert.Equal(0f, SteadycamMath.Blend(float.NaN));
        Assert.Equal(0f, SteadycamMath.Blend(-1f));
        Assert.Equal(0f, SteadycamMath.Blend(float.PositiveInfinity));
    }
}
