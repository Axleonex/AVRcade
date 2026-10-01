using System.Text.Json;
using VrClient.Core.Unreal;

namespace VrClient.Core.Tests;

public sealed class NativeUnrealPreflightTests
{
    [Fact]
    public void Evaluate_blocks_matching_build_until_executable_hash_is_pinned()
    {
        var fixture = BuildSingleLibraryFixture(expectedSha256: null);
        try
        {
            var result = new NativeUnrealPreflight().Evaluate(
                fixture.ProfilePath, fixture.SteamRoot);

            Assert.Equal(
                NativeUnrealPreflightStatus.FingerprintUnpinned,
                result.Status);
            Assert.Equal(5, result.ExitCode);
            Assert.Equal(
                VrClient.Core.Hashing.Sha256OfFile(fixture.ExecutablePath),
                result.ActualSha256);
        }
        finally
        {
            Directory.Delete(fixture.Root, recursive: true);
        }
    }

    [Fact]
    public void Evaluate_allows_observation_when_build_and_hash_are_pinned()
    {
        var fixture = BuildSingleLibraryFixture(
            expectedSha256: VrClient.Core.Hashing.Sha256OfBytes([1, 2, 3]));
        try
        {
            var result = new NativeUnrealPreflight().Evaluate(
                fixture.ProfilePath, fixture.SteamRoot);

            Assert.Equal(
                NativeUnrealPreflightStatus.ReadyForInProcessObservation,
                result.Status);
            Assert.Equal(0, result.ExitCode);
        }
        finally
        {
            Directory.Delete(fixture.Root, recursive: true);
        }
    }

    [Fact]
    public void Evaluate_finds_secondary_Steam_library_and_blocks_changed_build()
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
        var primary = Path.Combine(root, "primary");
        var secondary = Path.Combine(root, "secondary");
        Directory.CreateDirectory(Path.Combine(primary, "steamapps"));
        Directory.CreateDirectory(Path.Combine(secondary, "steamapps"));

        try
        {
            var escapedSecondary = secondary.Replace("\\", "\\\\");
            File.WriteAllText(
                Path.Combine(primary, "steamapps", "libraryfolders.vdf"),
                $"\"libraryfolders\"\n{{\n\t\"1\"\n\t{{\n\t\t\"path\"\t\t\"{escapedSecondary}\"\n\t}}\n}}\n");

            var manifest = Path.Combine(
                secondary, "steamapps", "appmanifest_4704690.acf");
            File.WriteAllText(
                manifest,
                "\"AppState\"\n{\n\t\"appid\"\t\t\"4704690\"\n" +
                "\t\"installdir\"\t\t\"MECCHA CHAMELEON\"\n" +
                "\t\"buildid\"\t\t\"24396565\"\n}\n");

            var shippingRelative = Path.Combine(
                "Chameleon", "Binaries", "Win64",
                "PenguinHotel-Win64-Shipping.exe");
            var executable = Path.Combine(
                secondary, "steamapps", "common", "MECCHA CHAMELEON",
                shippingRelative);
            Directory.CreateDirectory(Path.GetDirectoryName(executable)!);
            File.WriteAllBytes(executable, [1, 2, 3]);

            var profile = Path.Combine(root, "meccha-chameleon.json");
            File.WriteAllText(profile, JsonSerializer.Serialize(new
            {
                schema = "unreal-native/1",
                game = new
                {
                    slug = "meccha-chameleon",
                    steam_app_id = "4704690",
                    steam_buildid_observed = "24149874",
                    shipping_binary = shippingRelative,
                    executable_sha256_observed = (string?)null
                }
            }));

            var result = new NativeUnrealPreflight().Evaluate(profile, primary);

            Assert.Equal(
                NativeUnrealPreflightStatus.BuildMismatch, result.Status);
            Assert.Equal(4, result.ExitCode);
            Assert.Equal("24396565", result.ActualBuildId);
            Assert.Equal("24149874", result.ExpectedBuildId);
            Assert.Equal(
                Path.GetFullPath(executable),
                Path.GetFullPath(result.ExecutablePath!));
            Assert.Equal(
                Path.GetFullPath(manifest),
                Path.GetFullPath(result.ManifestPath!));
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private static NativeFixture BuildSingleLibraryFixture(
        string? expectedSha256)
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
        var steamApps = Path.Combine(root, "steamapps");
        Directory.CreateDirectory(steamApps);
        File.WriteAllText(
            Path.Combine(steamApps, "appmanifest_4704690.acf"),
            "\"AppState\"\n{\n\t\"appid\"\t\t\"4704690\"\n" +
            "\t\"installdir\"\t\t\"MECCHA CHAMELEON\"\n" +
            "\t\"buildid\"\t\t\"24396565\"\n}\n");

        var executable = Path.Combine(
            steamApps, "common", "MECCHA CHAMELEON", "Game.exe");
        Directory.CreateDirectory(Path.GetDirectoryName(executable)!);
        File.WriteAllBytes(executable, [1, 2, 3]);

        var profile = Path.Combine(root, "meccha-chameleon.json");
        File.WriteAllText(profile, JsonSerializer.Serialize(new
        {
            schema = "unreal-native/1",
            game = new
            {
                slug = "meccha-chameleon",
                steam_app_id = "4704690",
                steam_buildid_observed = "24396565",
                shipping_binary = "Game.exe",
                executable_sha256_observed = expectedSha256
            }
        }));

        return new NativeFixture(root, root, profile, executable);
    }

    private sealed record NativeFixture(
        string Root,
        string SteamRoot,
        string ProfilePath,
        string ExecutablePath);
}
