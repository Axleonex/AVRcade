using BepInEx.Logging;
using VrClient.UnityVR.Logic;

namespace VrClient.UnityVR;

public abstract class GameModule
{
    public virtual void OnBootstrap(ManualLogSource log, VrGameConfig config) { }
    public virtual void OnFrame(VrInput input) { }
}

public sealed class DefaultGameModule : GameModule { }
