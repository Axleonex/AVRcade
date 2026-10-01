using System;
using System.IO;
using System.Linq;
using BepInEx;
using VrClient.UnityVR.Logic;

namespace VrClient.UnityVR;

[BepInPlugin("com.avrcade.vrclient.unityvr", "VRClient UnityVR", "0.1.0")]
public sealed class Plugin : BaseUnityPlugin
{
    private VrDriver? _driver;

    private void Awake()
    {
        Logger.LogInfo("UXR-START: VRClient UnityVR 0.1.0");
        try
        {
            var config = LoadConfig();
            if (!VrBootstrap.TryStart(Logger))
                return;
            _driver = gameObject.AddComponent<VrDriver>();
            _driver.Initialize(Logger, config, new DefaultGameModule());
        }
        catch (Exception ex)
        {
            Logger.LogError($"UXR-BOOT-FAIL: plugin initialization: {ex.GetType().Name}: {ex.Message}");
        }
    }

    private static VrGameConfig LoadConfig()
    {
        var explicitSlug = Environment.GetEnvironmentVariable("VRCLIENT_UNITYVR_SLUG");
        var candidates = string.IsNullOrWhiteSpace(explicitSlug)
            ? Directory.GetFiles(Paths.ConfigPath, "vrclient-unityvr.*.json").OrderBy(x => x).ToArray()
            : new[] { Path.Combine(Paths.ConfigPath, $"vrclient-unityvr.{explicitSlug}.json") };
        var path = candidates.FirstOrDefault(File.Exists);
        return path is null
            ? new VrGameConfig("", false, 1f, Array.Empty<string>(), "follow")
            : VrGameConfig.Load(path);
    }

    private void OnDestroy()
    {
        if (_driver is not null)
            Destroy(_driver);
        VrBootstrap.Stop(Logger);
    }
}
