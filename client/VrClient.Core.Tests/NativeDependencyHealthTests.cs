using System.Text;
using VrClient.Core.Launch;

namespace VrClient.Core.Tests;

// Minimal PE images with independent import/export tables; no fixture is executed.
internal static class NativePeFixture
{
    public static byte[] Create(bool x64 = true, string? library = null, string? import = null,
        string? export = null, ushort? importOrdinal = null)
    {
        var bytes = new byte[4096];
        using var writer = new BinaryWriter(new MemoryStream(bytes));
        void U16(int offset, ushort value) { writer.BaseStream.Position = offset; writer.Write(value); }
        void U32(int offset, uint value) { writer.BaseStream.Position = offset; writer.Write(value); }
        void Text(int offset, string value) { writer.BaseStream.Position = offset; writer.Write(Encoding.ASCII.GetBytes(value + "\0")); }
        U16(0, 0x5a4d); U32(60, 128); U32(128, 0x4550);
        U16(132, x64 ? (ushort)0x8664 : (ushort)0x14c); U16(134, 1);
        U16(148, x64 ? (ushort)240 : (ushort)224); U16(150, 0x2022);
        const int opt = 152;
        U16(opt, x64 ? (ushort)0x20b : (ushort)0x10b);
        U32(opt + 32, 4096); U32(opt + 36, 512); U32(opt + 56, 8192); U32(opt + 60, 512);
        U16(opt + 68, 3); U32(opt + (x64 ? 108 : 92), 16);
        var directories = opt + (x64 ? 112 : 96);
        var section = opt + (x64 ? 240 : 224);
        Text(section, ".rdata"); U32(section + 8, 3584); U32(section + 12, 4096);
        U32(section + 16, 3584); U32(section + 20, 512); U32(section + 36, 0x40000040);
        uint Rva(int offset) => (uint)(offset - 512 + 4096);
        if (library is not null)
        {
            U32(directories + 8, Rva(512)); U32(directories + 12, 40);
            U32(512, Rva(640)); U32(524, Rva(600)); U32(528, Rva(640)); Text(600, library);
            writer.BaseStream.Position = 640;
            var thunk = importOrdinal.HasValue ? (x64 ? 0x8000000000000000UL : 0x80000000UL) | importOrdinal.Value : Rva(680);
            if (x64) writer.Write(thunk); else writer.Write((uint)thunk);
            U16(680, 0); Text(682, import ?? "RequiredFunction");
        }
        if (export is not null)
        {
            U32(directories, Rva(1024)); U32(directories + 4, 200);
            U32(1040, 1); U32(1044, 1); U32(1048, 1);
            U32(1052, Rva(1080)); U32(1056, Rva(1090)); U32(1060, Rva(1100));
            U32(1080, Rva(1536)); U32(1090, Rva(1120)); U16(1100, 0); Text(1120, export);
        }
        return bytes;
    }
}

public sealed class NativeDependencyHealthTests : IDisposable
{
    private const string IcuSymbol = "?find@RegexMatcher@icu_65@@UEAACXZ";
    private readonly string _root = Path.Combine(Path.GetTempPath(), "avrcade-native-" + Guid.NewGuid().ToString("N"));
    public NativeDependencyHealthTests() => Directory.CreateDirectory(_root);
    public void Dispose() => Directory.Delete(_root, true);
    private string Write(string name, byte[] bytes)
    { var path = Path.Combine(_root, name); File.WriteAllBytes(path, bytes); return path; }
    private NativeDependencyReport Check(string executable) => new NativeDependencyHealth().Check(_root, [executable], Path.Combine(_root, "system"));

    [Fact]
    public void DetectsTheScreenshotsMissingIcuEntryPoint()
    {
        var exe = Write("Game.exe", NativePeFixture.Create(library: "icuin.dll", import: IcuSymbol));
        Write("icuin.dll", NativePeFixture.Create(export: "OtherFunction"));
        var report = Check(exe);
        Assert.False(report.Healthy);
        Assert.Contains(report.Issues, issue => issue.Kind == "missing entry point" && issue.Symbol == IcuSymbol);
    }

    [Fact]
    public void MatchingIcuExportPassesWithoutClaimingHeadsetReadiness()
    {
        var exe = Write("Game.exe", NativePeFixture.Create(library: "icuin.dll", import: IcuSymbol));
        Write("icuin.dll", NativePeFixture.Create(export: IcuSymbol));
        var report = Check(exe);
        Assert.True(report.Healthy, report.Message); Assert.Equal(2, report.FilesChecked);
        Assert.DoesNotContain("VR ready", report.Message);
    }

    [Theory]
    [InlineData("repo")]
    [InlineData("yapyap")]
    [InlineData("cyberpunk-2077")]
    [InlineData("escape-the-backrooms")]
    public void MissingDependenciesAreDetectedForAnyGame(string name)
    {
        var exe = Write(name + ".exe", NativePeFixture.Create(library: "missing.dll"));
        Assert.Contains(Check(exe).Issues, issue => issue.Kind == "missing DLL" && issue.Library == "missing.dll");
    }

    [Fact]
    public void WrongArchitectureIsReported()
    {
        var exe = Write("Game.exe", NativePeFixture.Create(library: "icuin.dll", import: IcuSymbol));
        Write("icuin.dll", NativePeFixture.Create(x64: false, export: IcuSymbol));
        Assert.Contains(Check(exe).Issues, issue => issue.Kind == "wrong architecture");
    }

    [Fact]
    public void InvalidExistingBinaryIsNotHealthy()
    {
        var exe = Write("Game.exe", NativePeFixture.Create(library: "broken.dll"));
        Write("broken.dll", Encoding.UTF8.GetBytes("MZ but not a valid portable executable"));
        Assert.Contains(Check(exe).Issues, issue => issue.Kind == "invalid binary");
    }

    [Fact]
    public void Native32BitGamesAndOrdinalImportsWork()
    {
        var exe = Write("Game.exe", NativePeFixture.Create(x64: false, library: "local.dll", importOrdinal: 1));
        Write("local.dll", NativePeFixture.Create(x64: false, export: "Function"));
        Assert.True(Check(exe).Healthy);
    }

    [Fact]
    public void WindowsApiSetsDoNotRequireLooseDllFiles()
    {
        var exe = Write("Game.exe", NativePeFixture.Create(library: "api-ms-win-core-file-l1-1-0.dll"));
        Assert.True(Check(exe).Healthy);
    }

    [Fact]
    public void ProxyDllSelfImportsUseWindowsProviderWithoutRecursion()
    {
        var exe = Write("Game.exe", NativePeFixture.Create(library: "version.dll", import: "Function"));
        Write("version.dll", NativePeFixture.Create(library: "version.dll", import: "SystemFunction", export: "Function"));
        Directory.CreateDirectory(Path.Combine(_root, "system"));
        Write("system/version.dll", NativePeFixture.Create(export: "SystemFunction"));
        Assert.True(Check(exe).Healthy);
    }

    [Fact]
    public void ImportPathsCannotEscapeTheGame()
    {
        var exe = Write("Game.exe", NativePeFixture.Create(library: "../outside.dll"));
        Assert.Contains(Check(exe).Issues, issue => issue.Kind == "invalid DLL name");
    }
}
