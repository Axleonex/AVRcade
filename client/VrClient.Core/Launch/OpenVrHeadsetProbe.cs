using System.Runtime.InteropServices;

namespace VrClient.Core.Launch;

/// Checks the installed OpenVR runtime without claiming the game's scene slot.
public static class OpenVrHeadsetProbe
{
    private static readonly object Gate = new();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate uint Initialize(ref int error, int applicationType,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string startupInfo);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void Shutdown();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    private delegate bool HmdPresent();

    public static bool IsReady(string startupExecutable)
    {
        if (!OperatingSystem.IsWindows()) return false;
        lock (Gate)
        {
            nint library = 0;
            var initialized = false;
            Shutdown? shutdown = null;
            try
            {
                library = NativeLibrary.Load(Path.Combine(Path.GetDirectoryName(startupExecutable)!, "openvr_api.dll"));
                var present = Marshal.GetDelegateForFunctionPointer<HmdPresent>(NativeLibrary.GetExport(library, "VR_IsHmdPresent"));
                if (!present()) return false;
                var initialize = Marshal.GetDelegateForFunctionPointer<Initialize>(NativeLibrary.GetExport(library, "VR_InitInternal2"));
                shutdown = Marshal.GetDelegateForFunctionPointer<Shutdown>(NativeLibrary.GetExport(library, "VR_ShutdownInternal"));
                var error = 0;
                // VRApplication_Background (3): no scene focus and no dashboard overlay.
                initialize(ref error, 3, "");
                initialized = error == 0;
                return initialized && present();
            }
            catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or
                                       BadImageFormatException or IOException or UnauthorizedAccessException)
            {
                return false;
            }
            finally
            {
                if (initialized) shutdown?.Invoke();
                if (library != 0) NativeLibrary.Free(library);
            }
        }
    }
}
