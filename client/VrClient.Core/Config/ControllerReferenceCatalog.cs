using System.Text.Json;
using System.Text.Json.Serialization;

namespace VrClient.Core.Config;

public sealed record ControllerBindingReference(
    [property: JsonPropertyName("slot")] string Slot,
    [property: JsonPropertyName("control")] string Control,
    [property: JsonPropertyName("action")] string Action,
    [property: JsonPropertyName("note")] string? Note = null)
{
    public bool IsAssigned => !string.Equals(Slot, "unassigned", StringComparison.OrdinalIgnoreCase);
}

public sealed record ControllerDeviceReference(
    [property: JsonPropertyName("id")] string Id,
    [property: JsonPropertyName("display_name")] string DisplayName,
    [property: JsonPropertyName("bindings")] IReadOnlyList<ControllerBindingReference> Bindings)
{
    private static readonly ControllerBindingReference Unassigned =
        new("unassigned", "", "Unassigned");

    public ControllerBindingReference LeftStick => At("left_stick");
    public ControllerBindingReference RightStick => At("right_stick");
    public ControllerBindingReference LeftPrimary => At("left_primary");
    public ControllerBindingReference LeftSecondary => At("left_secondary");
    public ControllerBindingReference RightPrimary => At("right_primary");
    public ControllerBindingReference RightSecondary => At("right_secondary");
    public ControllerBindingReference LeftTrigger => At("left_trigger");
    public ControllerBindingReference RightTrigger => At("right_trigger");
    public ControllerBindingReference LeftGrip => At("left_grip");
    public ControllerBindingReference RightGrip => At("right_grip");
    public ControllerBindingReference Menu => At("menu");
    public ControllerBindingReference BothSticks => At("both_sticks");
    public ControllerBindingReference BothGrips => At("both_grips");
    public Uri DiagramAssetUri => Id.ToLowerInvariant() switch
    {
        "quest-touch" => new Uri("avares://vrclient-app/Assets/Controllers/quest-touch.png"),
        "valve-index" => new Uri("avares://vrclient-app/Assets/Controllers/valve-index.png"),
        _ => new Uri("avares://vrclient-app/Assets/Controllers/quest-touch.png")
    };

    private ControllerBindingReference At(string slot) =>
        Bindings.FirstOrDefault(binding =>
            string.Equals(binding.Slot, slot, StringComparison.OrdinalIgnoreCase)) ?? Unassigned;
}

public sealed record ConversionCreditReference(
    [property: JsonPropertyName("creator")] string Creator,
    [property: JsonPropertyName("project")] string Project,
    [property: JsonPropertyName("project_url")] string ProjectUrl,
    [property: JsonPropertyName("controls_url")] string ControlsUrl);

public sealed record ControllerReference(
    [property: JsonPropertyName("verification_state")] string VerificationState,
    [property: JsonPropertyName("game_slug")] string GameSlug,
    [property: JsonPropertyName("title")] string Title,
    [property: JsonPropertyName("devices")] IReadOnlyList<ControllerDeviceReference> Devices,
    [property: JsonPropertyName("conversion_credit")] ConversionCreditReference? ConversionCredit = null,
    [property: JsonPropertyName("note")] string? Note = null)
{
    public string Guidance => Note ?? "Choose your controller below. These labels come from this game's active VR conversion profile.";
}

public static class ControllerReferenceCatalog
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public static ControllerReference? Load(string repoRoot, string gameSlug)
    {
        var path = Path.Combine(repoRoot, "config", "controller-maps", $"{gameSlug}.json");
        if (!File.Exists(path))
            return null;

        var reference = JsonSerializer.Deserialize<ControllerReference>(
            File.ReadAllText(path), JsonOptions);
        if (reference is null || reference.Devices.Count == 0 ||
            !(string.Equals(reference.VerificationState, "verified", StringComparison.OrdinalIgnoreCase) ||
              string.Equals(reference.VerificationState, "source_verified", StringComparison.OrdinalIgnoreCase) &&
              reference.ConversionCredit is not null && !string.IsNullOrWhiteSpace(reference.Note)) ||
            !string.Equals(reference.GameSlug, gameSlug, StringComparison.OrdinalIgnoreCase))
            return null;
        if (reference.ConversionCredit is { } credit &&
            (string.IsNullOrWhiteSpace(credit.Creator) ||
             string.IsNullOrWhiteSpace(credit.Project) ||
             !IsHttpsUrl(credit.ProjectUrl) ||
             !IsHttpsUrl(credit.ControlsUrl)))
            return null;
        if (reference.Devices.Any(device =>
            string.IsNullOrWhiteSpace(device.Id) || device.Bindings.Count == 0 ||
            device.Bindings.Any(binding =>
                string.IsNullOrWhiteSpace(binding.Slot) ||
                string.IsNullOrWhiteSpace(binding.Control) ||
                string.IsNullOrWhiteSpace(binding.Action)) ||
            device.Bindings.GroupBy(binding => binding.Slot, StringComparer.OrdinalIgnoreCase)
                .Any(group => group.Count() > 1)))
            return null;
        return reference;
    }

    private static bool IsHttpsUrl(string value) =>
        Uri.TryCreate(value, UriKind.Absolute, out var uri) &&
        uri.Scheme == Uri.UriSchemeHttps;
}
