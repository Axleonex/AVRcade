using System.ComponentModel;
using System.Runtime.InteropServices;
using Avalonia.Controls;
using Avalonia.Platform;

namespace VrClient.App.Views;

/// <summary>
/// Windows-only host for an existing mod-manager top-level window. The manager owns its UI and
/// process; AVRcade never reads or changes its mod database. Cross-process HWND
/// parenting is experimental and is undone before this control is destroyed.
/// </summary>
public sealed class VortexWindowHost : NativeControlHost
{
    private const int GwlStyle = -16;
    private const int SwpNoZOrder = 0x0004;
    private const int SwpFrameChanged = 0x0020;
    private const int SwpShowWindow = 0x0040;
    private const uint WsChild = 0x40000000;
    private const uint WsPopup = 0x80000000;
    private const uint WsCaption = 0x00C00000;
    private const uint WsThickFrame = 0x00040000;
    private const uint WsSysMenu = 0x00080000;
    private const uint WsMinimizeBox = 0x00020000;
    private const uint WsMaximizeBox = 0x00010000;

    private IntPtr _container;
    private IntPtr _vortex;
    private IntPtr _originalParent;
    private int _originalStyle;
    private RECT _originalRect;

    public bool IsDocked => _vortex != IntPtr.Zero && IsWindow(_vortex);

    public VortexWindowHost() => SizeChanged += (_, _) => ResizeVortex();

    protected override IPlatformHandle CreateNativeControlCore(IPlatformHandle parent)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException();
        _container = CreateWindowExW(0, "STATIC", "", WsChild | 0x10000000,
            0, 0, 1, 1, parent.Handle, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
        if (_container == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        return new PlatformHandle(_container, "HWND");
    }

    protected override void DestroyNativeControlCore(IPlatformHandle control)
    {
        Detach();
        if (_container != IntPtr.Zero) DestroyWindow(_container);
        _container = IntPtr.Zero;
    }

    public bool TryDock(IntPtr vortexWindow, out string error)
    {
        error = "";
        if (_container == IntPtr.Zero || !IsWindow(vortexWindow))
        {
            error = "The mod manager window is not ready.";
            return false;
        }
        if (_vortex != IntPtr.Zero) Detach();
        var hostDpi = GetWindowDpiAwarenessContext(_container);
        var vortexDpi = GetWindowDpiAwarenessContext(vortexWindow);
        if (hostDpi != IntPtr.Zero && vortexDpi != IntPtr.Zero &&
            !AreDpiAwarenessContextsEqual(hostDpi, vortexDpi))
        {
            error = "The mod manager and AVRcade use different Windows DPI modes; embedding could disrupt the manager. Its window remains separate.";
            return false;
        }

        _originalParent = GetParent(vortexWindow);
        _originalStyle = GetWindowLongW(vortexWindow, GwlStyle);
        GetWindowRect(vortexWindow, out _originalRect);
        var childStyle = ((uint)_originalStyle | WsChild) &
            ~(WsPopup | WsCaption | WsThickFrame | WsSysMenu | WsMinimizeBox | WsMaximizeBox);
        SetWindowLongW(vortexWindow, GwlStyle, unchecked((int)childStyle));
        Marshal.SetLastPInvokeError(0);
        var formerParent = SetParent(vortexWindow, _container);
        if (formerParent == IntPtr.Zero && Marshal.GetLastPInvokeError() != 0)
        {
            var win32Error = Marshal.GetLastPInvokeError();
            SetWindowLongW(vortexWindow, GwlStyle, _originalStyle);
            error = new Win32Exception(win32Error).Message;
            return false;
        }
        _vortex = vortexWindow;
        ResizeVortex();
        // Try the handoff on initial dock as well as via the visible recovery button.
        TryFocusManager(out _);
        return true;
    }

    /// <summary>
    /// A reparented manager runs its own UI thread. Avalonia focus on this host does not
    /// transfer keyboard focus to that thread, so provide an explicit recovery action.
    /// </summary>
    public bool TryFocusManager(out string error)
    {
        error = "";
        if (!IsDocked)
        {
            error = "No mod manager window is embedded.";
            return false;
        }

        var managerThread = GetWindowThreadProcessId(_vortex, out _);
        var currentThread = GetCurrentThreadId();
        if (managerThread == 0)
        {
            error = "Windows could not identify the manager's UI thread.";
            return false;
        }

        var attached = managerThread != currentThread;
        if (attached && !AttachThreadInput(currentThread, managerThread, true))
        {
            error = new Win32Exception(Marshal.GetLastWin32Error()).Message;
            return false;
        }

        try
        {
            SetFocus(_vortex);
            var focused = GetFocus();
            if (focused == _vortex || (focused != IntPtr.Zero && IsChild(_vortex, focused)))
                return true;
            error = "Windows did not give keyboard focus to the embedded manager. Close this panel to use its normal window.";
            return false;
        }
        finally
        {
            if (attached) AttachThreadInput(currentThread, managerThread, false);
        }
    }

    public void Detach()
    {
        if (_vortex == IntPtr.Zero) return;
        var window = _vortex;
        _vortex = IntPtr.Zero;
        if (!IsWindow(window)) return;
        // Restore top-level styling before removing the child relationship.
        var style = ((uint)_originalStyle & ~WsChild) | (_originalParent == IntPtr.Zero ? WsPopup : 0);
        SetWindowLongW(window, GwlStyle, unchecked((int)style));
        SetParent(window, _originalParent);
        SetWindowLongW(window, GwlStyle, _originalStyle);
        SetWindowPos(window, IntPtr.Zero, _originalRect.Left, _originalRect.Top,
            Math.Max(400, _originalRect.Right - _originalRect.Left),
            Math.Max(300, _originalRect.Bottom - _originalRect.Top),
            SwpNoZOrder | SwpFrameChanged | SwpShowWindow);
    }

    private void ResizeVortex()
    {
        if (_vortex == IntPtr.Zero || _container == IntPtr.Zero || !IsWindow(_vortex)) return;
        if (!GetClientRect(_container, out var area)) return;
        SetWindowPos(_vortex, IntPtr.Zero, 0, 0,
            Math.Max(1, area.Right), Math.Max(1, area.Bottom),
            SwpNoZOrder | SwpFrameChanged | SwpShowWindow);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateWindowExW(uint exStyle, string className, string windowName,
        uint style, int x, int y, int width, int height, IntPtr parent, IntPtr menu,
        IntPtr instance, IntPtr parameter);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool DestroyWindow(IntPtr window);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool IsChild(IntPtr parent, IntPtr child);
    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr SetFocus(IntPtr window);
    [DllImport("user32.dll")] private static extern IntPtr GetFocus();
    [DllImport("user32.dll", SetLastError = true)] private static extern bool AttachThreadInput(uint attach, uint attachTo, bool attachState);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll", SetLastError = true)] private static extern int GetWindowLongW(IntPtr window, int index);
    [DllImport("user32.dll", SetLastError = true)] private static extern int SetWindowLongW(IntPtr window, int index, int value);
    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr SetParent(IntPtr child, IntPtr parent);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool GetWindowRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool GetClientRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool SetWindowPos(IntPtr window, IntPtr after,
        int x, int y, int width, int height, int flags);
    [DllImport("user32.dll")] private static extern IntPtr GetWindowDpiAwarenessContext(IntPtr window);
    [DllImport("user32.dll")] private static extern bool AreDpiAwarenessContextsEqual(IntPtr first, IntPtr second);
}
