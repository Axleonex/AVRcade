using System.Text;
using VrClient.Core.Config;

namespace VrClient.Core.Unreal;

/// <summary>A read-only, on-demand UEVR menu reference generated from the app's controller map.</summary>
public static class UevrControllerGuide
{
    public static string Build(ControllerReference reference)
    {
        var script = new StringBuilder("-- AVRcade controller guide. Generated from the per-game controller map.\n");
        script.AppendLine("uevr.sdk.callbacks.on_draw_ui(function()");
        script.AppendLine("  if imgui.tree_node(\"AVRcade controls\") then");
        AddText(script, reference.Title);
        AddText(script, reference.Guidance);
        foreach (var device in reference.Devices)
        {
            script.AppendLine($"    if imgui.tree_node({Quote(device.DisplayName)}) then");
            foreach (var binding in device.Bindings)
            {
                AddText(script, $"{binding.Control}: {binding.Action}");
                if (!string.IsNullOrWhiteSpace(binding.Note)) AddText(script, binding.Note);
            }
            script.AppendLine("      imgui.tree_pop()");
            script.AppendLine("    end");
        }
        script.AppendLine("    imgui.tree_pop()");
        script.AppendLine("  end");
        script.AppendLine("end)");
        return script.ToString();
    }

    public static string Install(ControllerReference reference, string profileDirectory)
    {
        var scripts = Path.Combine(profileDirectory, "scripts");
        Directory.CreateDirectory(scripts);
        var destination = Path.Combine(scripts, "vrclient_controls.lua");
        var content = Build(reference);
        if (File.Exists(destination))
        {
            if (File.ReadAllText(destination) == content) return destination;
            // Preserve even hand-edited versions; .bak files are not executed by UEVR.
            File.Copy(destination, destination + $".{Guid.NewGuid():N}.bak");
        }
        File.WriteAllText(destination, content, new UTF8Encoding(false));
        return destination;
    }

    private static void AddText(StringBuilder script, string value)
    {
        // Explicit lines keep the guide readable within UEVR's narrow Script UI panel.
        var line = new StringBuilder();
        foreach (var word in value.Split(' '))
        {
            if (line.Length > 0 && line.Length + word.Length > 78)
            {
                script.AppendLine($"    imgui.text({Quote(line.ToString())})");
                line.Clear();
            }
            if (line.Length > 0) line.Append(' ');
            line.Append(word);
        }
        script.AppendLine($"    imgui.text({Quote(line.ToString())})");
    }

    private static string Quote(string value)
    {
        var result = new StringBuilder("\"");
        foreach (var c in value)
            result.Append(c switch
            {
                '\\' => "\\\\", '"' => "\\\"", '\n' => "\\n", '\r' => "\\r",
                _ when char.IsControl(c) => "\\" + ((int)c).ToString("D3"),
                _ => c.ToString()
            });
        return result.Append('"').ToString();
    }
}
