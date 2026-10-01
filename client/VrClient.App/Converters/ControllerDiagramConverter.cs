using System.Globalization;
using Avalonia;
using Avalonia.Data.Converters;
using Avalonia.Media.Imaging;
using Avalonia.Platform;

namespace VrClient.App.Converters;

public sealed class ControllerDiagramConverter : IValueConverter
{
    private readonly Dictionary<Uri, Bitmap> _cache = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not Uri uri)
            return null;
        if (_cache.TryGetValue(uri, out var image))
            return image;

        using var stream = AssetLoader.Open(uri);
        image = new Bitmap(stream);
        _cache.Add(uri, image);
        return image;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        AvaloniaProperty.UnsetValue;
}
