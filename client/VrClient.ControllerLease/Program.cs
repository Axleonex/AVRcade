using System.Diagnostics;
using VrClient.Core;
using VrClient.Core.Config;

var bindings = new GtaVControllerBindings();
var status = bindings.LeaseFile + ".status.txt";
try
{
    if (args.Length != 1 || !Path.GetFullPath(args[0]).Equals(Path.GetFullPath(bindings.LeaseFile), StringComparison.OrdinalIgnoreCase)) return 2;
    if (bindings.ReadLease() is not { } lease) return 0;
    // A cancelled/failed Steam launch must not leave the vendor's shared file
    // assigned to AVRcade. This helper does not launch or close the game.
    Process? game = null;
    while (DateTimeOffset.UtcNow - lease.Created < TimeSpan.FromSeconds(120))
    {
        foreach (var process in Process.GetProcessesByName("GTA5"))
        {
            try
            {
                if (string.Equals(process.MainModule?.FileName, lease.ExpectedExe, StringComparison.OrdinalIgnoreCase)) { game = process; break; }
            }
            catch (System.ComponentModel.Win32Exception)
            {
                // Staging refused any pre-existing GTA5 process. A newly started
                // elevated game may hide its module path; keep the lease until
                // that process exits rather than restoring while it is running.
                game = process; break;
            }
            process.Dispose();
        }
        if (game != null) break;
        await Task.Delay(500);
    }
    if (game != null) { using (game) await game.WaitForExitAsync(); }
    // Another recovery/launch may have replaced the journal; do not undo it.
    if (bindings.ReadLease()?.Created == lease.Created) bindings.Recover(preserveSessionEdits: game != null);
    AtomicFile.WriteAllText(status, "Original native controller bindings restored.");
    return 0;
}
catch (Exception error)
{
    AtomicFile.WriteAllText(status, "Binding restore needs attention: " + error.Message);
    return 1;
}
