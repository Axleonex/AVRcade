using VrClient.Core.FalloutNewVegas;

public sealed class FalloutNewVegasVortexSyncTests
{
    [Fact]
    public void Preview_is_read_only_and_apply_preserves_vr_entries_and_vortex_files()
    {
        using var fixture = new Fixture();
        fixture.Source("# Vortex active profile\n*Quest.esp\n*Weather.esm\n");
        fixture.Deployed("Quest.esp", "Weather.esm");
        fixture.Vr("FNVR.esp\n", "FNVR.esp\n");

        var service = new FalloutNewVegasVortexSyncService();
        var plan = service.Preview(fixture.Game, fixture.Profile, fixture.Local);

        Assert.Equal("FNVR.esp\n", File.ReadAllText(fixture.VrPlugins));
        Assert.Equal(new[] { "Quest.esp", "Weather.esm" }, plan.VortexPlugins);
        Assert.False(File.Exists(Path.Combine(fixture.Profile, "avrcade-vortex-sync.json")));

        service.Apply(plan);
        Assert.Equal(new[] { "Quest.esp", "Weather.esm", "FNVR.esp" }, Active(fixture.VrPlugins));
        Assert.Equal(new[] { "Quest.esp", "Weather.esm", "FNVR.esp" }, Active(fixture.VrLoadOrder));
        Assert.Equal("# Vortex active profile\n*Quest.esp\n*Weather.esm\n", File.ReadAllText(fixture.SourcePlugins));
        Assert.True(service.Validate(fixture.Game, fixture.Profile, fixture.Local).Ready);
        Assert.False(service.Apply(service.Preview(fixture.Game, fixture.Profile, fixture.Local)).Changed);
    }

    [Fact]
    public void Resync_drops_only_previously_imported_plugins_and_keeps_original_vr_plugins()
    {
        using var fixture = new Fixture();
        fixture.Source("Quest.esp\nWeather.esm\n");
        fixture.Deployed("Quest.esp", "Weather.esm");
        fixture.Vr("FNVR.esp\nWeather.esm\n", "FNVR.esp\nWeather.esm\n");
        var service = new FalloutNewVegasVortexSyncService();
        service.Apply(service.Preview(fixture.Game, fixture.Profile, fixture.Local));

        fixture.Source("NewQuest.esp\n");
        fixture.Deployed("NewQuest.esp");
        Assert.False(service.Validate(fixture.Game, fixture.Profile, fixture.Local).Ready);
        service.Apply(service.Preview(fixture.Game, fixture.Profile, fixture.Local));

        Assert.Equal(new[] { "NewQuest.esp", "FNVR.esp", "Weather.esm" }, Active(fixture.VrPlugins));
        Assert.DoesNotContain("Quest.esp", Active(fixture.VrPlugins));
        Assert.True(service.Validate(fixture.Game, fixture.Profile, fixture.Local).Ready);
    }

    [Fact]
    public void Manual_vr_profile_edit_blocks_resync_without_overwriting_it()
    {
        using var fixture = new Fixture();
        fixture.Source("Quest.esp\n");
        fixture.Deployed("Quest.esp");
        fixture.Vr("FNVR.esp\n", "FNVR.esp\n");
        var service = new FalloutNewVegasVortexSyncService();
        service.Apply(service.Preview(fixture.Game, fixture.Profile, fixture.Local));
        File.AppendAllText(fixture.VrPlugins, "Manual.esp\n");

        Assert.Throws<InvalidOperationException>(() => service.Preview(fixture.Game, fixture.Profile, fixture.Local));
        Assert.Contains("Manual.esp", File.ReadAllText(fixture.VrPlugins));
        Assert.False(service.Validate(fixture.Game, fixture.Profile, fixture.Local).Ready);
    }

    [Fact]
    public void Missing_deployed_plugin_or_unsafe_source_line_blocks_sync()
    {
        using var fixture = new Fixture();
        fixture.Vr("FNVR.esp\n", "FNVR.esp\n");
        var service = new FalloutNewVegasVortexSyncService();
        fixture.Source("Missing.esp\n");
        Assert.Throws<InvalidOperationException>(() => service.Preview(fixture.Game, fixture.Profile, fixture.Local));
        fixture.Source("../escape.esp\n");
        Assert.Throws<InvalidOperationException>(() => service.Preview(fixture.Game, fixture.Profile, fixture.Local));
    }

    [Fact]
    public void Apply_rejects_source_or_target_changes_after_preview()
    {
        using var fixture = new Fixture();
        fixture.Source("Quest.esp\n");
        fixture.Deployed("Quest.esp", "NewQuest.esp");
        fixture.Vr("FNVR.esp\n", "FNVR.esp\n");
        var service = new FalloutNewVegasVortexSyncService();
        var plan = service.Preview(fixture.Game, fixture.Profile, fixture.Local);

        fixture.Source("NewQuest.esp\n");
        Assert.Throws<InvalidOperationException>(() => service.Apply(plan));
        fixture.Source("Quest.esp\n");
        File.AppendAllText(fixture.VrLoadOrder, "Manual.esp\n");
        Assert.Throws<InvalidOperationException>(() => service.Apply(plan));
        Assert.Equal("FNVR.esp\n", File.ReadAllText(fixture.VrPlugins));
    }

    [Fact]
    public void Readiness_detects_deployment_removed_after_sync()
    {
        using var fixture = new Fixture();
        fixture.Source("Quest.esp\n");
        fixture.Deployed("Quest.esp");
        fixture.Vr("FNVR.esp\n", "FNVR.esp\n");
        var service = new FalloutNewVegasVortexSyncService();
        service.Apply(service.Preview(fixture.Game, fixture.Profile, fixture.Local));
        Assert.True(service.Validate(fixture.Game, fixture.Profile, fixture.Local).Ready);

        File.Delete(Path.Combine(fixture.Game, "Data", "Quest.esp"));
        Assert.False(service.Validate(fixture.Game, fixture.Profile, fixture.Local).Ready);
    }

    [Fact]
    public void Source_load_order_controls_vr_load_order_separately_from_plugin_list()
    {
        using var fixture = new Fixture();
        fixture.Source("Quest.esp\nWeather.esm\n");
        fixture.SourceOrder("Weather.esm\nQuest.esp\nInactive.esp\n");
        fixture.Deployed("Quest.esp", "Weather.esm");
        fixture.Vr("FNVR.esp\n", "FNVR.esp\n");
        var service = new FalloutNewVegasVortexSyncService();
        service.Apply(service.Preview(fixture.Game, fixture.Profile, fixture.Local));

        Assert.Equal(new[] { "Quest.esp", "Weather.esm", "FNVR.esp" }, Active(fixture.VrPlugins));
        Assert.Equal(new[] { "Weather.esm", "Quest.esp", "FNVR.esp" }, Active(fixture.VrLoadOrder));
        Assert.True(service.Validate(fixture.Game, fixture.Profile, fixture.Local).Ready);
    }

    private static string[] Active(string path) => File.ReadAllLines(path)
        .Select(line => line.Trim())
        .Where(line => line.Length > 0 && !line.StartsWith('#'))
        .ToArray();

    private sealed class Fixture : IDisposable
    {
        private readonly string _root = Path.Combine(Path.GetTempPath(), "fnv-vortex-sync-" + Guid.NewGuid().ToString("N"));
        public string Game => Path.Combine(_root, "game");
        public string Profile => Path.Combine(_root, "mo2", "profiles", "New Vegas VR");
        public string Local => Path.Combine(_root, "local");
        public string SourcePlugins => Path.Combine(Local, "FalloutNV", "plugins.txt");
        public string SourceLoadOrder => Path.Combine(Local, "FalloutNV", "loadorder.txt");
        public string VrPlugins => Path.Combine(Profile, "plugins.txt");
        public string VrLoadOrder => Path.Combine(Profile, "loadorder.txt");

        public Fixture()
        {
            Directory.CreateDirectory(Path.Combine(Game, "Data"));
            Directory.CreateDirectory(Profile);
            Directory.CreateDirectory(Path.GetDirectoryName(SourcePlugins)!);
        }

        public void Source(string content) => File.WriteAllText(SourcePlugins, content);
        public void SourceOrder(string content) => File.WriteAllText(SourceLoadOrder, content);
        public void Vr(string plugins, string loadOrder)
        {
            File.WriteAllText(VrPlugins, plugins);
            File.WriteAllText(VrLoadOrder, loadOrder);
        }
        public void Deployed(params string[] plugins)
        {
            foreach (var plugin in plugins)
                File.WriteAllText(Path.Combine(Game, "Data", plugin), string.Empty);
        }
        public void Dispose() => Directory.Delete(_root, recursive: true);
    }
}
