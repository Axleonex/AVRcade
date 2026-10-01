using VrClient.App.Services;

public sealed class FalloutNewVegasDesktopPreparationTests
{
    [Fact]
    public void Native_vr_mode_refuses_detectable_vortex_deployment_without_touching_it()
    {
        var root = Path.Combine(Path.GetTempPath(), "fnv-native-clean-" + Guid.NewGuid().ToString("N"));
        var data = Path.Combine(root, "Data");
        Directory.CreateDirectory(data);
        try
        {
            using var service = new FalloutNewVegasDesktopService(root, root);
            var settings = new FalloutNewVegasDesktopSettings { GameDirectory = root };
            service.EnsureNativeModeClean(settings);
            var manifest = Path.Combine(data, "vortex.deployment.json");
            File.WriteAllText(manifest, "{}");
            Assert.Throws<InvalidOperationException>(() => service.EnsureNativeModeClean(settings));
            Assert.Equal("{}", File.ReadAllText(manifest));
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void Native_vr_mode_refuses_non_vanilla_physical_plugins()
    {
        var root = Path.Combine(Path.GetTempPath(), "fnv-native-plugin-" + Guid.NewGuid().ToString("N"));
        var data = Path.Combine(root, "Data");
        Directory.CreateDirectory(data);
        try
        {
            using var service = new FalloutNewVegasDesktopService(root, root);
            var settings = new FalloutNewVegasDesktopSettings { GameDirectory = root };
            File.WriteAllText(Path.Combine(data, "FalloutNV.esm"), "");
            service.EnsureNativeModeClean(settings);
            File.WriteAllText(Path.Combine(data, "Quest.esp"), "");
            Assert.Throws<InvalidOperationException>(() => service.EnsureNativeModeClean(settings));
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void Vortex_sync_refuses_external_or_traversing_mo2_profiles()
    {
        var root = Path.Combine(Path.GetTempPath(), "fnv-vortex-target-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            using var service = new FalloutNewVegasDesktopService(root, root);
            var settings = new FalloutNewVegasDesktopSettings
            {
                GameDirectory = root,
                ModOrganizerInstanceDirectory = Path.GetTempPath(),
                VrProfileName = "New Vegas VR"
            };
            Assert.Throws<InvalidOperationException>(() => service.PreviewVortexSync(settings));
            settings.ModOrganizerInstanceDirectory = Path.Combine(root, "mo2-instance");
            settings.VrProfileName = "..\\normal-profile";
            Assert.Throws<InvalidOperationException>(() => service.PreviewVortexSync(settings));
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void Native_launcher_requires_all_artifacts_and_never_uses_shell_or_profiles()
    {
        var root = Path.Combine(Path.GetTempPath(), "fnv-native-launch-" + Guid.NewGuid().ToString("N"));
        var build = Path.Combine(root, "build", "fnv-x86");
        Directory.CreateDirectory(build);
        try
        {
            using var service = new FalloutNewVegasDesktopService(root, root);
            var settings = new FalloutNewVegasDesktopSettings { GameDirectory = root };
            Assert.Throws<FileNotFoundException>(() => service.CreateNativeTestStartInfo(settings));
            foreach (var name in new[] { "fnv-test-launcher.exe", "vrclient_fnv_stereo.dll", "openxr_loader.dll" })
                File.WriteAllBytes(Path.Combine(build, name), new byte[] { 0 });
            File.WriteAllBytes(Path.Combine(root, "FalloutNV.exe"), new byte[] { 0 });
            var start = service.CreateNativeTestStartInfo(settings);
            Assert.False(start.UseShellExecute);
            Assert.Equal(Path.Combine(build, "fnv-test-launcher.exe"), start.FileName);
            Assert.Equal(new[] { "--game", Path.Combine(root, "FalloutNV.exe") }, start.ArgumentList);
            Assert.Equal(new[] { "--stop" }, service.CreateNativeTestStartInfo(settings, stop: true).ArgumentList);
            Assert.False(Directory.Exists(Path.Combine(root, "mo2-instance")));
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void Native_launcher_receives_the_selected_steam_game_path_without_using_a_steam_uri()
    {
        var root = Path.Combine(Path.GetTempPath(), "fnv-native-identity-" + Guid.NewGuid().ToString("N"));
        var build = Path.Combine(root, "build", "fnv-x86");
        var game = Path.Combine(root, "Fallout New Vegas");
        Directory.CreateDirectory(build);
        Directory.CreateDirectory(game);
        try
        {
            foreach (var name in new[] { "fnv-test-launcher.exe", "vrclient_fnv_stereo.dll", "openxr_loader.dll" })
                File.WriteAllBytes(Path.Combine(build, name), new byte[] { 0 });
            File.WriteAllBytes(Path.Combine(game, "FalloutNV.exe"), new byte[] { 0 });
            using var service = new FalloutNewVegasDesktopService(root, root);
            var settings = new FalloutNewVegasDesktopSettings { GameDirectory = game, Storefront = VrClient.Core.FalloutNewVegas.FnvStorefront.Steam };

            var start = service.CreateNativeTestStartInfo(settings);

            Assert.False(start.UseShellExecute);
            Assert.Equal(new[] { "--game", Path.Combine(game, "FalloutNV.exe") }, start.ArgumentList);
            Assert.Equal(new[] { "--stop" }, service.CreateNativeTestStartInfo(settings, stop: true).ArgumentList);
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void Packaged_native_launcher_is_preferred_without_a_source_checkout()
    {
        var root = Path.Combine(Path.GetTempPath(), "fnv-packaged-launch-" + Guid.NewGuid().ToString("N"));
        var packaged = Path.Combine(root, "native", "fallout-new-vegas");
        Directory.CreateDirectory(packaged);
        try
        {
            foreach (var name in new[] { "fnv-test-launcher.exe", "vrclient_fnv_stereo.dll", "openxr_loader.dll" })
                File.WriteAllBytes(Path.Combine(packaged, name), new byte[] { 0 });
            File.WriteAllBytes(Path.Combine(root, "FalloutNV.exe"), new byte[] { 0 });

            using var service = new FalloutNewVegasDesktopService(root, root);
            var start = service.CreateNativeTestStartInfo(new FalloutNewVegasDesktopSettings { GameDirectory = root });
            var settings = service.LoadAndDetect();

            Assert.Equal(Path.Combine(packaged, "fnv-test-launcher.exe"), start.FileName);
            Assert.Equal(Path.Combine(packaged, "fnv-test-launcher.exe"), settings.NativeAdapterExecutablePath);
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void Empty_instance_and_source_prepare_without_manual_paths_and_persist_for_relaunch()
    {
        var root = Path.Combine(Path.GetTempPath(), "fnv-desktop-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            using var service = new FalloutNewVegasDesktopService(root, root);
            var settings = new FalloutNewVegasDesktopSettings { GameDirectory = root, VrProfileName = "" };
            var preview = service.PreviewPreparation(settings);
            Assert.Equal(Path.Combine(root, "mo2-instance"), settings.ModOrganizerInstanceDirectory);
            Assert.False(Directory.Exists(settings.VrProfileDirectory));
            Assert.True(service.ApplyPreparation(settings).Changed);
            Assert.True(File.Exists(Path.Combine(settings.VrProfileDirectory, "modlist.txt")));
            Assert.False(service.ApplyPreparation(settings).Changed);
            Assert.Equal(settings.VrProfileDirectory, service.LoadAndDetect().VrProfileDirectory);
            Assert.False(service.Check(settings).Ready); // A prepared profile is not an installed VR stack.
            settings.ModOrganizerExecutablePath = Path.Combine(root, "ModOrganizer.exe");
            File.WriteAllBytes(settings.ModOrganizerExecutablePath, new byte[] { 0 });
            Assert.Contains(service.Check(settings).Checks, check => check.Code == "mo2_instance_not_bound");
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void Managed_portable_mo2_is_detected_bound_and_prepared_without_touching_the_game()
    {
        var root = Path.Combine(Path.GetTempPath(), "fnv-managed-mo2-" + Guid.NewGuid().ToString("N"));
        var runtime = Path.Combine(root, "runtime", "mo2-2.5.2");
        Directory.CreateDirectory(runtime);
        File.WriteAllBytes(Path.Combine(runtime, "ModOrganizer.exe"), new byte[] { 0 });
        try
        {
            using var service = new FalloutNewVegasDesktopService(root, root);
            var settings = service.LoadAndDetect();
            settings.GameDirectory = root;
            Assert.Equal(runtime, settings.ModOrganizerInstanceDirectory);
            Assert.Equal(Path.Combine(runtime, "ModOrganizer.exe"), settings.ModOrganizerExecutablePath);
            service.ApplyPreparation(settings);
            Assert.True(File.Exists(Path.Combine(runtime, "portable.txt")));
            Assert.True(File.Exists(Path.Combine(runtime, "profiles", "New Vegas VR", "modlist.txt")));
            var ini = File.ReadAllText(Path.Combine(runtime, "ModOrganizer.ini"));
            Assert.Contains("gameName=Fallout New Vegas", ini);
            Assert.Contains("selected_profile=New Vegas VR", ini);
        }
        finally { Directory.Delete(root, true); }
    }

    [Fact]
    public void Stale_managed_instance_is_rebound_to_existing_portable_mo2()
    {
        var root = Path.Combine(Path.GetTempPath(), "fnv-stale-mo2-" + Guid.NewGuid().ToString("N"));
        var runtime = Path.Combine(root, "runtime", "mo2-2.5.2");
        Directory.CreateDirectory(runtime);
        File.WriteAllBytes(Path.Combine(runtime, "ModOrganizer.exe"), new byte[] { 0 });
        File.WriteAllText(Path.Combine(runtime, "portable.txt"), string.Empty);
        try
        {
            using var service = new FalloutNewVegasDesktopService(root, root);
            var saved = new FalloutNewVegasDesktopSettings
            {
                ModOrganizerInstanceDirectory = Path.Combine(root, "mo2-instance")
            };
            service.Save(saved);

            var detected = service.LoadAndDetect();

            Assert.Equal(runtime, detected.ModOrganizerInstanceDirectory);
            Assert.Equal(Path.Combine(runtime, "ModOrganizer.exe"), detected.ModOrganizerExecutablePath);
        }
        finally { Directory.Delete(root, true); }
    }
}
