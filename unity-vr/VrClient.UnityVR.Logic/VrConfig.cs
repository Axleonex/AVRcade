using System;
using System.IO;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text;

namespace VrClient.UnityVR.Logic;

/// The per-game data contract the framework reads at runtime. It intentionally
/// uses the framework JSON serializer so the Unity plugin does not need to ship
/// System.Text.Json and its dependency graph into an older Mono runtime.
public sealed class VrGameConfig
{
    public VrGameConfig(
        string cameraObjectName,
        bool duplicateCamera,
        float worldScale,
        string[] disableObjects,
        string uiMode)
    {
        CameraObjectName = cameraObjectName;
        DuplicateCamera = duplicateCamera;
        WorldScale = worldScale;
        DisableObjects = disableObjects;
        UiMode = uiMode;
    }

    public string CameraObjectName { get; }
    public bool DuplicateCamera { get; }
    public float WorldScale { get; }
    public string[] DisableObjects { get; }
    public string UiMode { get; }

    public static VrGameConfig Load(string jsonPath) => Parse(File.ReadAllText(jsonPath));

    public static VrGameConfig Parse(string json)
    {
        var serializer = new DataContractJsonSerializer(typeof(WireConfig));
        using var stream = new MemoryStream(Encoding.UTF8.GetBytes(json));
        var wire = (WireConfig?)serializer.ReadObject(stream) ?? new WireConfig();
        return new VrGameConfig(
            wire.CameraObjectName ?? "",
            wire.DuplicateCamera ?? false,
            wire.WorldScale ?? 1f,
            wire.DisableObjects ?? Array.Empty<string>(),
            wire.UiMode ?? "follow");
    }

    [DataContract]
    private sealed class WireConfig
    {
        [DataMember(EmitDefaultValue = false)] public string? CameraObjectName { get; set; }
        [DataMember(EmitDefaultValue = false)] public bool? DuplicateCamera { get; set; }
        [DataMember(EmitDefaultValue = false)] public float? WorldScale { get; set; }
        [DataMember(EmitDefaultValue = false)] public string[]? DisableObjects { get; set; }
        [DataMember(EmitDefaultValue = false)] public string? UiMode { get; set; }
    }
}
