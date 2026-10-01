using BepInEx.Logging;
using UnityEngine;
using VrClient.UnityVR.Logic;

namespace VrClient.UnityVR;

internal sealed class VrDriver : MonoBehaviour
{
    private StereoRig? _rig;
    private VrInput? _input;
    private GameModule? _module;

    internal void Initialize(ManualLogSource log, VrGameConfig config, GameModule module)
    {
        _rig = new StereoRig(log, config);
        _input = new VrInput(log);
        _module = module;
        module.OnBootstrap(log, config);
        if (!_rig.Initialize())
        {
            enabled = false;
            return;
        }
        log.LogInfo("UXR-READY: stereo rig and tracked input loop active");
    }

    private void LateUpdate()
    {
        _rig?.UpdatePose();
        _input?.Poll();
        if (_input is not null)
            _module?.OnFrame(_input);
    }
}
