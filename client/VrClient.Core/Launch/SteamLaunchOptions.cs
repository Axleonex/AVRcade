namespace VrClient.Core.Launch;
using System.Text;

/// LI-1b: automatic Steam launch-option install. Composes the wrapper line and
/// edits Steam's per-user localconfig.vdf (LaunchOptions for one app id),
/// preserving every other byte of the file. Steam rewrites this file from
/// memory on exit, so the CLI verb closes Steam before editing and takes a
/// timestamped backup first.
public static class SteamLaunchOptions
{
    // ---- launch-option line composition (pure string rules) ----

    private static string WrapPrefix(string wrapperExePath) => $"\"{wrapperExePath}\" wrap --";

    /// Wrap whatever the user already has:
    ///   ""                          -> "<wrapper>" wrap -- %command%
    ///   contains %command% (chain)  -> "<wrapper>" wrap -- <existing>
    ///   plain game args             -> "<wrapper>" wrap -- %command% <existing>
    ///   already wrapped by us       -> unchanged (idempotent)
    public static string Compose(string existing, string wrapperExePath)
    {
        var prefix = WrapPrefix(wrapperExePath);
        existing = existing.Trim();
        if (existing.Contains(prefix, StringComparison.OrdinalIgnoreCase))
            return existing;
        if (existing.Length == 0)
            return $"{prefix} %command%";
        return existing.Contains("%command%", StringComparison.OrdinalIgnoreCase)
            ? $"{prefix} {existing}"
            : $"{prefix} %command% {existing}";
    }

    /// Remove our wrapper, restoring what it wrapped ("" when it wrapped a bare %command%).
    public static string Strip(string existing, string wrapperExePath)
    {
        var prefix = WrapPrefix(wrapperExePath);
        var trimmed = existing.Trim();
        if (!trimmed.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            return trimmed;
        var inner = trimmed[prefix.Length..].Trim();
        return inner.Equals("%command%", StringComparison.OrdinalIgnoreCase) ? "" : inner;
    }

    // ---- localconfig.vdf editing ----

    /// Read the current LaunchOptions for appId; null when the app block or key is absent.
    public static string? GetLaunchOptions(string vdfText, string appId)
    {
        var apps = FindAppsBlock(Parse(vdfText));
        if (apps is null || !apps.Children.TryGetValue(appId, out var app))
            return null;
        return app.Values.TryGetValue("LaunchOptions", out var v) ? v.Value : null;
    }

    /// Set (or create) LaunchOptions for appId, splicing only the affected bytes.
    /// Throws when the file does not look like a Steam localconfig.vdf.
    public static string SetLaunchOptions(string vdfText, string appId, string launchOptions)
    {
        var apps = FindAppsBlock(Parse(vdfText))
            ?? throw new InvalidOperationException(
                "localconfig_apps_block_missing - this does not look like a Steam localconfig.vdf");
        var escaped = Escape(launchOptions);

        if (apps.Children.TryGetValue(appId, out var app))
        {
            if (app.Values.TryGetValue("LaunchOptions", out var v))
                return vdfText[..v.ValStart] + $"\"{escaped}\"" + vdfText[v.ValEnd..];
            // App block exists without the key: insert right after its '{'.
            var insertAt = app.OpenOffset + 1;
            return vdfText[..insertAt] + $"\n\t\t\t\t\t\t\"LaunchOptions\"\t\t\"{escaped}\"" + vdfText[insertAt..];
        }

        // No block for this app yet: create one inside "apps".
        var appsInsertAt = apps.OpenOffset + 1;
        var block = $"\n\t\t\t\t\t\"{appId}\"\n\t\t\t\t\t{{\n\t\t\t\t\t\t\"LaunchOptions\"\t\t\"{escaped}\"\n\t\t\t\t\t}}";
        return vdfText[..appsInsertAt] + block + vdfText[appsInsertAt..];
    }

    private static Block? FindAppsBlock(Block root) =>
        root.Child("UserLocalConfigStore")?.Child("Software")?.Child("Valve")
            ?.Child("Steam")?.Child("apps");

    // Steam's VDF writer escapes backslash and quote in values.
    private static string Escape(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");

    // ---- minimal VDF structural parser (offsets kept for byte-preserving splices) ----

    private sealed class Block
    {
        public readonly Dictionary<string, Block> Children = new(StringComparer.OrdinalIgnoreCase);
        public readonly Dictionary<string, (string Value, int ValStart, int ValEnd)> Values =
            new(StringComparer.OrdinalIgnoreCase);
        public int OpenOffset;
        public Block? Child(string name) => Children.TryGetValue(name, out var b) ? b : null;
    }

    private enum TokKind { Str, Open, Close }
    private readonly record struct Token(TokKind Kind, string Value, int Start, int End);

    private static Block Parse(string text)
    {
        var tokens = Tokenize(text);
        var i = 0;
        return ParseBlock(tokens, ref i);
    }

    private static Block ParseBlock(IReadOnlyList<Token> toks, ref int i)
    {
        var block = new Block();
        while (i < toks.Count)
        {
            var tok = toks[i];
            if (tok.Kind == TokKind.Close)
            {
                i++;
                return block;
            }
            if (tok.Kind == TokKind.Str && i + 1 < toks.Count)
            {
                var next = toks[i + 1];
                if (next.Kind == TokKind.Open)
                {
                    i += 2;
                    var child = ParseBlock(toks, ref i);
                    child.OpenOffset = next.Start;
                    block.Children[tok.Value] = child;
                    continue;
                }
                if (next.Kind == TokKind.Str)
                {
                    block.Values[tok.Value] = (next.Value, next.Start, next.End);
                    i += 2;
                    continue;
                }
            }
            i++; // stray token: skip defensively, never throw mid-file
        }
        return block;
    }

    private static List<Token> Tokenize(string s)
    {
        var tokens = new List<Token>();
        for (var i = 0; i < s.Length;)
        {
            var c = s[i];
            if (c == '"')
            {
                var start = i++;
                var sb = new StringBuilder();
                while (i < s.Length && s[i] != '"')
                {
                    if (s[i] == '\\' && i + 1 < s.Length)
                    {
                        sb.Append(s[i + 1] switch { 'n' => '\n', 't' => '\t', _ => s[i + 1] });
                        i += 2;
                    }
                    else
                        sb.Append(s[i++]);
                }
                i++; // closing quote
                tokens.Add(new Token(TokKind.Str, sb.ToString(), start, i));
            }
            else if (c == '{')
                tokens.Add(new Token(TokKind.Open, "{", i, ++i));
            else if (c == '}')
                tokens.Add(new Token(TokKind.Close, "}", i, ++i));
            else if (c == '/' && i + 1 < s.Length && s[i + 1] == '/')
                while (i < s.Length && s[i] != '\n') i++;
            else
                i++;
        }
        return tokens;
    }
}
