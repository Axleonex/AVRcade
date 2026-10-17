using Avrcade.YapyapVr;
using Xunit;
namespace YapyapVrControls.Tests;
public sealed class VrFocusAndFallbackTests
{
    [Theory]
    [InlineData(false, true, true, true)]
    [InlineData(true, true, false, false)]
    [InlineData(false, true, false, false)]
    [InlineData(true, false, false, true)]
    [InlineData(false, false, true, false)]
    public void HeadsetSceneOwnsVrFocusAndDashboardStillSuspendsIt(bool desktop, bool active, bool scene, bool expected)
    {
        Assert.Equal(expected, VrFocusPolicy.GameHasFocus(desktop, active, scene));
    }

    [Theory]
    [InlineData(true, false, false, false, VrButtons.East)]
    [InlineData(false, true, false, false, VrButtons.North)]
    [InlineData(false, false, true, false, VrButtons.South)]
    [InlineData(false, false, false, true, VrButtons.West)]
    public void QuestFaceButtonsKeepTheirActionsAcrossInputFallback(bool x, bool y, bool a, bool b, VrButtons expected)
    {
        var left = new VrControllerState(0,0,0,0,0,0,0,0,x,y,false,false,false,false);
        var right = new VrControllerState(0,0,0,0,0,0,0,0,false,false,a,b,false,false);
        Assert.Equal(expected, YapyapControlMap.Map(left,right).Buttons);
    }
}
