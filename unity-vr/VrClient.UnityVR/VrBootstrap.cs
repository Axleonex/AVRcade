using System;
using System.Reflection;
using BepInEx.Logging;

namespace VrClient.UnityVR;

internal static class VrBootstrap
{
    private static object? _manager;

    internal static bool TryStart(ManualLogSource log)
    {
        try
        {
            var settingsType = Type.GetType(
                "UnityEngine.XR.Management.XRGeneralSettings, Unity.XR.Management", throwOnError: false);
            if (settingsType is null)
            {
                log.LogError("UXR-BOOT-FAIL: no XR loader (Unity.XR.Management is absent)");
                return false;
            }

            var instance = settingsType.GetProperty("Instance", BindingFlags.Public | BindingFlags.Static)
                ?.GetValue(null, null);
            _manager = settingsType.GetProperty("Manager", BindingFlags.Public | BindingFlags.Instance)
                ?.GetValue(instance, null);
            if (_manager is null)
            {
                log.LogError("UXR-BOOT-FAIL: XR manager unavailable");
                return false;
            }

            var managerType = _manager.GetType();
            var activeLoader = managerType.GetProperty("activeLoader")?.GetValue(_manager, null);
            if (activeLoader is null)
            {
                managerType.GetMethod("InitializeLoaderSync", Type.EmptyTypes)?.Invoke(_manager, null);
                activeLoader = managerType.GetProperty("activeLoader")?.GetValue(_manager, null);
            }
            if (activeLoader is null)
            {
                log.LogError("UXR-BOOT-FAIL: no active OpenXR loader");
                return false;
            }

            managerType.GetMethod("StartSubsystems", Type.EmptyTypes)?.Invoke(_manager, null);
            log.LogInfo($"UXR-BOOT-OK: loader={activeLoader.GetType().FullName}");
            return true;
        }
        catch (TargetInvocationException ex)
        {
            var cause = ex.InnerException ?? ex;
            log.LogError($"UXR-BOOT-FAIL: {cause.GetType().Name}: {cause.Message}");
            return false;
        }
        catch (Exception ex)
        {
            log.LogError($"UXR-BOOT-FAIL: {ex.GetType().Name}: {ex.Message}");
            return false;
        }
    }

    internal static void Stop(ManualLogSource log)
    {
        if (_manager is null) return;
        try
        {
            _manager.GetType().GetMethod("StopSubsystems", Type.EmptyTypes)?.Invoke(_manager, null);
            _manager.GetType().GetMethod("DeinitializeLoader", Type.EmptyTypes)?.Invoke(_manager, null);
            log.LogInfo("UXR-STOP: XR subsystems stopped");
        }
        catch (Exception ex)
        {
            log.LogWarning($"UXR-STOP: {ex.GetType().Name}: {ex.Message}");
        }
        finally
        {
            _manager = null;
        }
    }
}
