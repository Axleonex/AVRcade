using System.Buffers.Binary;
using System.Reflection.PortableExecutable;
using System.Text;
using VrClient.Core.Modpack;

namespace VrClient.Core.Launch;

public sealed record NativeDependencyIssue(string Kind, string Consumer, string Library, string? Symbol = null);
public sealed record NativeDependencyReport(IReadOnlyList<NativeDependencyIssue> Issues, int FilesChecked)
{
    public bool Healthy => Issues.Count == 0;
    public string Message => Healthy ? $"Native dependency check passed ({FilesChecked} files)." :
        "Native startup problem: " + string.Join("; ", Issues.Take(3).Select(issue =>
            $"{issue.Consumer}: {issue.Kind} {issue.Library}" + (issue.Symbol is null ? "" : $" ({issue.Symbol})"))) +
        ". For original game files, open About at the bottom of this page and choose Verify game files in Steam. Update incompatible mods through their creator or mod manager.";
}

public sealed class NativeDependencyHealth
{
    public NativeDependencyReport Check(string root, IReadOnlyList<string> executables, string? systemDirectory = null)
    {
        var issues = new List<NativeDependencyIssue>();
        var images = new Dictionary<string, Image>(StringComparer.OrdinalIgnoreCase);
        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var queue = new Queue<(string File, string Directory)>();
        var system = systemDirectory ?? Environment.SystemDirectory;
        foreach (var executable in executables)
        {
            var file = ModUpdateTransaction.Resolve(root, Path.GetRelativePath(root, executable).Replace('\\', '/'));
            queue.Enqueue((file, Path.GetDirectoryName(file)!));
        }
        void Issue(string kind, string consumer, string library, string? symbol = null)
        {
            var next = new NativeDependencyIssue(kind, Path.GetRelativePath(root, consumer), library, symbol);
            if (issues.Count < 64 && !issues.Contains(next)) issues.Add(next);
        }
        Image? Read(string file, string consumer, string library)
        {
            if (images.TryGetValue(file, out var cached)) return cached;
            try
            {
                var image = new Image(file);
                images.Add(file, image);
                return image;
            }
            catch (Exception ex) when (ex is IOException or BadImageFormatException or
                InvalidDataException or UnauthorizedAccessException or OverflowException)
            { Issue("invalid binary", consumer, library); return null; }
        }
        bool SystemFile(string library) => !string.IsNullOrEmpty(system) && File.Exists(Path.Combine(system, library)) ||
            systemDirectory is null && OperatingSystem.IsWindows() &&
            File.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "SysWOW64", library));
        try
        {
            while (queue.TryDequeue(out var next))
            {
                if (!visited.Add(next.File + "|" + next.Directory)) continue;
                if (visited.Count > 128) { Issue("dependency scan limit reached", next.File, "Recheck setup"); break; }
                if (!File.Exists(next.File)) { Issue("missing DLL", next.File, Path.GetFileName(next.File)); continue; }
                var consumer = Read(next.File, next.File, Path.GetFileName(next.File));
                if (consumer is null || consumer.Managed) continue;
                IReadOnlyList<Import> imports;
                try { imports = consumer.Imports(); }
                catch (Exception ex) when (ex is IOException or InvalidDataException or OverflowException)
                { Issue("invalid import table", next.File, Path.GetFileName(next.File)); continue; }
                foreach (var group in imports.GroupBy(item => item.Library, StringComparer.OrdinalIgnoreCase))
                {
                    var library = group.Key;
                    if (string.IsNullOrWhiteSpace(library) || library.IndexOfAny(['/', '\\', ':']) >= 0)
                    { Issue("invalid DLL name", next.File, library); continue; }
                    if (library.StartsWith("api-ms-", StringComparison.OrdinalIgnoreCase) ||
                        library.StartsWith("ext-ms-", StringComparison.OrdinalIgnoreCase)) continue;
                    // A proxy named version.dll or winmm.dll can import the real Windows DLL.
                    if (library.Equals(Path.GetFileName(next.File), StringComparison.OrdinalIgnoreCase) && SystemFile(library)) continue;
                    var candidate = ModUpdateTransaction.Resolve(root,
                        Path.GetRelativePath(root, Path.Combine(next.Directory, library)).Replace('\\', '/'));
                    if (!File.Exists(candidate))
                    {
                        if (!SystemFile(library)) Issue("missing DLL", next.File, library);
                        continue;
                    }
                    var provider = Read(candidate, next.File, library);
                    if (provider is null) continue;
                    if (!provider.Managed && provider.Machine != consumer.Machine)
                    { Issue("wrong architecture", next.File, library); continue; }
                    try
                    {
                        foreach (var import in group)
                            if (!provider.HasExport(import.Symbol)) Issue("missing entry point", next.File, library, import.Symbol);
                    }
                    catch (Exception ex) when (ex is IOException or InvalidDataException or OverflowException)
                    { Issue("invalid export table", next.File, library); continue; }
                    queue.Enqueue((candidate, next.Directory));
                }
            }
            return new(issues, images.Count);
        }
        finally { foreach (var image in images.Values) image.Dispose(); }
    }

    private sealed record Import(string Library, string Symbol);

    // Reads PE metadata without loading DLLs or executing any game/mod code.
    private sealed class Image : IDisposable
    {
        private readonly FileStream _stream;
        private readonly PEHeaders _headers;
        private HashSet<string>? _exports;
        public Machine Machine => _headers.CoffHeader.Machine;
        public bool Managed => _headers.CorHeader is not null;
        private bool Is64 => _headers.PEHeader!.Magic == PEMagic.PE32Plus;
        public Image(string path)
        {
            _stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            try
            {
                using var reader = new PEReader(_stream, PEStreamOptions.LeaveOpen);
                _headers = reader.PEHeaders;
                if (_headers.PEHeader is null) throw new InvalidDataException("Not a PE executable.");
            }
            catch { _stream.Dispose(); throw; }
        }
        public void Dispose() => _stream.Dispose();
        private long Offset(uint rva)
        {
            if (rva < _headers.PEHeader!.SizeOfHeaders) return rva;
            foreach (var section in _headers.SectionHeaders)
                if (rva >= section.VirtualAddress && (long)rva - section.VirtualAddress < section.SizeOfRawData)
                    return checked((long)section.PointerToRawData + rva - section.VirtualAddress);
            throw new InvalidDataException("PE address is outside file sections.");
        }
        private void Read(long offset, Span<byte> bytes)
        {
            if (offset < 0 || offset > _stream.Length - bytes.Length) throw new InvalidDataException("Truncated PE table.");
            _stream.Position = offset;
            _stream.ReadExactly(bytes);
        }
        private ushort U16(long offset) { Span<byte> bytes = stackalloc byte[2]; Read(offset, bytes); return BinaryPrimitives.ReadUInt16LittleEndian(bytes); }
        private uint U32(long offset) { Span<byte> bytes = stackalloc byte[4]; Read(offset, bytes); return BinaryPrimitives.ReadUInt32LittleEndian(bytes); }
        private ulong U64(long offset) { Span<byte> bytes = stackalloc byte[8]; Read(offset, bytes); return BinaryPrimitives.ReadUInt64LittleEndian(bytes); }
        private string Text(uint rva)
        {
            var offset = Offset(rva);
            var bytes = new List<byte>();
            for (var i = 0; i < 4096; i++)
            {
                _stream.Position = offset + i;
                var value = _stream.ReadByte();
                if (value < 0) break;
                if (value == 0) return Encoding.ASCII.GetString(bytes.ToArray());
                bytes.Add((byte)value);
            }
            throw new InvalidDataException("Unterminated PE name.");
        }
        public IReadOnlyList<Import> Imports()
        {
            var imports = new List<Import>();
            var directory = _headers.PEHeader!.ImportTableDirectory;
            if (directory.RelativeVirtualAddress == 0) return imports;
            var offset = Offset((uint)directory.RelativeVirtualAddress);
            var count = Math.Min(4096, directory.Size / 20);
            for (var i = 0; i < count; i++)
            {
                var descriptor = offset + i * 20;
                var originalThunk = U32(descriptor);
                var name = U32(descriptor + 12);
                var firstThunk = U32(descriptor + 16);
                if (originalThunk == 0 && name == 0 && firstThunk == 0) return imports;
                var library = Text(name);
                var thunk = Offset(originalThunk == 0 ? firstThunk : originalThunk);
                for (var j = 0; j < 65536; j++)
                {
                    var value = Is64 ? U64(thunk + j * 8L) : U32(thunk + j * 4L);
                    if (value == 0) break;
                    var ordinal = (value & (Is64 ? 0x8000000000000000UL : 0x80000000UL)) != 0;
                    imports.Add(new(library, ordinal ? "#" + (value & 0xffff) : Text(checked((uint)value + 2))));
                    if (j == 65535) throw new InvalidDataException("Import count exceeds the scan limit.");
                }
            }
            throw new InvalidDataException("Import descriptors are not terminated.");
        }
        public bool HasExport(string symbol)
        {
            if (_exports is not null) return _exports.Contains(symbol);
            _exports = new(StringComparer.Ordinal);
            var directory = _headers.PEHeader!.ExportTableDirectory;
            if (directory.RelativeVirtualAddress == 0) return false;
            var offset = Offset((uint)directory.RelativeVirtualAddress);
            var ordinalBase = U32(offset + 16);
            var functions = U32(offset + 20);
            var names = U32(offset + 24);
            if (functions > 65536 || names > 65536) throw new InvalidDataException("Export count exceeds the scan limit.");
            var addresses = Offset(U32(offset + 28));
            for (uint i = 0; i < functions; i++)
                if (U32(addresses + i * 4L) != 0) _exports.Add("#" + (ordinalBase + i));
            if (names != 0)
            {
                var nameTable = Offset(U32(offset + 32));
                var ordinals = Offset(U32(offset + 36));
                for (uint i = 0; i < names; i++)
                {
                    var ordinal = U16(ordinals + i * 2L);
                    if (ordinal >= functions) throw new InvalidDataException("Invalid export ordinal.");
                    if (U32(addresses + ordinal * 4L) != 0) _exports.Add(Text(U32(nameTable + i * 4L)));
                }
            }
            return _exports.Contains(symbol);
        }
    }
}
