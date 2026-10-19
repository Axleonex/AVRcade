using System.Globalization;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Markup.Xaml;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using VrClient.App.Converters;
using VrClient.Core.Config;
using VrClient.App.ViewModels;
using System.ComponentModel;

namespace VrClient.App.Views;

public partial class ControllerDiagram : UserControl
{
    private static readonly IBrush Ground = Brush.Parse("#0E1B24");
    private static readonly IBrush Surface = Brush.Parse("#18313F");
    private static readonly IBrush Ink = Brushes.White;
    private static readonly IBrush SecondaryInk = Brush.Parse("#A9E8F4");
    private static readonly IBrush Accent = Brush.Parse("#65CDE8");
    private static readonly ControllerDiagramConverter Images = new();
    private readonly Canvas _canvas;
    private readonly TextBlock _context;
    private readonly StackPanel _editorPanel;
    private ControllerEditorViewModel? _editor;
    private bool _refreshingEditor;

    public ControllerDiagram()
    {
        AvaloniaXamlLoader.Load(this);
        this.FindControl<Border>("DiagramSurface")!.Background = Ground;
        _canvas = this.FindControl<Canvas>("DiagramCanvas")!;
        _context = this.FindControl<TextBlock>("ContextText")!;
        _context.Foreground = SecondaryInk;
        _editorPanel = this.FindControl<StackPanel>("EditorPanel")!;
        DataContextChanged += (_, _) =>
        {
            if (_editor != null) _editor.PropertyChanged -= EditorChanged;
            _editor = DataContext as ControllerEditorViewModel;
            if (_editor != null) _editor.PropertyChanged += EditorChanged;
            BuildDiagram(); RefreshEditor();
        };
    }

    private void EditorChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(ControllerEditorViewModel.Diagram)) BuildDiagram();
        if (!_refreshingEditor) RefreshEditor();
    }

    private void RefreshEditor()
    {
        _refreshingEditor = true;
        try
        {
            _editorPanel.Children.Clear(); _editorPanel.IsVisible = _editor != null;
            if (_editor == null) return;
            _editorPanel.Children.Add(new TextBlock { Text = "Select a callout to swap compatible actions on the same hand. Selecting a stick edits its press; stick tilt stays unchanged. Game prompts may still show the default button.",
                Foreground = SecondaryInk, TextWrapping = TextWrapping.Wrap });
            _editorPanel.Children.Add(new TextBlock { Text = "Assign action to " + _editor.SelectedControl,
                Foreground = Ink, FontWeight = FontWeight.SemiBold });
            var choices = _editor.Choices;
            var picker = new ComboBox { ItemsSource = choices.Select(c => c.Display).ToArray(),
                SelectedIndex = choices.ToList().FindIndex(c => c.Slot == _editor.SelectedAction?.Slot), MinWidth = 300,
                HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Stretch };
            picker.SelectionChanged += (_, _) =>
            {
                if (!_refreshingEditor && picker.SelectedIndex >= 0 && picker.SelectedIndex < choices.Count)
                    _editor.SelectedAction = choices[picker.SelectedIndex];
            };
            _editorPanel.Children.Add(picker);
            _editorPanel.Children.Add(new Button { Content = _editor.ResetButtonText, Command = _editor.ResetDefaultsCommand, IsEnabled = _editor.HasSelectedDefaults,
                Foreground = Ink, Background = Surface });
            _editorPanel.Children.Add(new Button { Content = "Save as my defaults", Command = _editor.SaveAsDefaultsCommand,
                Foreground = Ink, Background = Surface });
            _editorPanel.Children.Add(new Button { Content = "Restore original bindings", Command = _editor.RestoreOriginalCommand,
                Foreground = Ink, Background = Surface });
            foreach (var binding in _editor.Diagram.Bindings.Where(binding =>
                !ControllerCustomization.Slots.Contains(binding.Slot) && binding.Slot is not ("left_stick" or "right_stick")))
                _editorPanel.Children.Add(new TextBlock {
                    Text = binding.Control + ": " + binding.Action + (binding.Note is null ? "" : ". " + binding.Note),
                    Foreground = SecondaryInk, TextWrapping = TextWrapping.Wrap });
            _editorPanel.Children.Add(new TextBlock { Text = _editor.Status, Foreground = SecondaryInk,
                TextWrapping = TextWrapping.Wrap });
        }
        finally { _refreshingEditor = false; }
    }

    private void BuildDiagram()
    {
        _canvas.Children.Clear();
        var device = _editor?.Diagram ?? DataContext as ControllerDeviceReference;
        if (device == null) return;
        var index = device.Id == "valve-index";
        AutomationProperties.SetName(this, $"{device.DisplayName} button diagram");
        var image = new Image
        {
            Source = Images.Convert(device.DiagramAssetUri, typeof(Bitmap), null,
                CultureInfo.InvariantCulture) as Bitmap,
            Width = 384, Height = 264, Stretch = Stretch.Uniform
        };
        Place(image, 268, 176);
        if (index)
        {
            // The schematic's original letter stamps are reversed vertically.
            // Correct the displayed buttons to the installed Index driver map:
            // B upper, A lower, with matching callout targets below.
            StampIndexButton("B", new Point(106, 40));
            StampIndexButton("A", new Point(113, 57));
            StampIndexButton("B", new Point(214, 40));
            StampIndexButton("A", new Point(207, 57));
        }
        Place(HandHeading("LEFT HAND"), 324, 128);
        Place(HandHeading("RIGHT HAND"), 516, 128);

        // Coordinates are in the existing SVGs' 320x220 viewBox, shared by their PNG exports.
        var left = index
            ? new[] { ("left_trigger", new Point(74, 24)), ("left_primary", new Point(113, 57)),
                ("left_secondary", new Point(106, 40)), ("left_stick", new Point(94, 53)),
                ("left_grip", new Point(51, 139)) }
            : new[] { ("left_trigger", new Point(75, 50)), ("left_primary", new Point(104, 80)),
                ("left_secondary", new Point(91, 62)), ("left_stick", new Point(79, 94)),
                ("left_grip", new Point(54, 134)) };
        var right = index
            ? new[] { ("right_trigger", new Point(246, 24)), ("right_primary", new Point(207, 57)),
                ("right_secondary", new Point(214, 40)), ("right_stick", new Point(226, 53)),
                ("right_grip", new Point(269, 139)) }
            : new[] { ("right_trigger", new Point(245, 50)), ("right_primary", new Point(216, 80)),
                ("right_secondary", new Point(229, 62)), ("right_stick", new Point(241, 94)),
                ("right_grip", new Point(266, 134)) };
        AddCallouts(device, left, false);
        AddCallouts(device, right, true);
        _context.Text = device.Context ?? "Match the left and right controllers. Each line points to the named physical button; see the notes below for combination gestures.";
    }

    private void StampIndexButton(string letter, Point button)
    {
        var stamp = new Border
        {
            Width = 21, Height = 21, CornerRadius = new CornerRadius(11),
            Background = Brush.Parse("#6094AA"),
            Child = new TextBlock
            {
                Text = letter, FontSize = 14, FontWeight = FontWeight.Bold,
                Foreground = Ink, TextAlignment = TextAlignment.Center,
                VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center
            }
        };
        Place(stamp, 268 + button.X * 1.2 - 10.5, 176 + button.Y * 1.2 - 10.5);
    }

    private void AddCallouts(ControllerDeviceReference device, (string Slot, Point Button)[] buttons,
        bool rightHand)
    {
        var row = 0;
        foreach (var (slot, button) in buttons.OrderBy(item => item.Button.Y))
        {
            var binding = device.Bindings.FirstOrDefault(item => item.Slot == slot);
            if (binding is null) continue;
            var editSlot = slot.EndsWith("_stick", StringComparison.Ordinal) ? slot + "_click" : slot;
            var actionText = binding.Action;
            if (_editor != null && slot.EndsWith("_stick", StringComparison.Ordinal))
                actionText = "Tilt: " + binding.Action + "\nPress: " + device.Bindings.First(b => b.Slot == editSlot).Action;
            var top = 24 + row++ * 112;
            var target = new Point(268 + button.X * 1.2, 176 + button.Y * 1.2);
            _canvas.Children.Add(new Line
            {
                StartPoint = new Point(rightHand ? 672 : 248, top + 56),
                EndPoint = new Point(target.X + (rightHand ? 11 : -11), target.Y),
                Stroke = Accent, StrokeThickness = 2
            });
            // Outline the button instead of covering its letter with a dot.
            Place(new Ellipse { Width = 22, Height = 22, Stroke = Accent,
                StrokeThickness = 1.5, Fill = Brushes.Transparent }, target.X - 11, target.Y - 11);
            var label = new Button
            {
                Width = 248, Height = 104, Padding = new Thickness(12, 8),
                CornerRadius = new CornerRadius(6), Background = Surface,
                HorizontalContentAlignment = Avalonia.Layout.HorizontalAlignment.Stretch,
                Content = new StackPanel
                {
                    Spacing = 4,
                    Children =
                    {
                        new TextBlock { Text = _editor == null ? binding.Control :
                            (slot.EndsWith("_stick", StringComparison.Ordinal) ? (rightHand ? "Right" : "Left") + " stick tilt / press" : ControllerCustomization.Label(device.Id, slot)), Foreground = SecondaryInk,
                            FontSize = 16, LineHeight = 20, TextWrapping = TextWrapping.Wrap },
                        new TextBlock { Text = actionText, Foreground = Ink,
                            FontSize = 17, LineHeight = 21, FontWeight = FontWeight.SemiBold,
                            TextWrapping = TextWrapping.Wrap }
                    }
                }
            };
            label.Click += (_, _) => { _editor?.Select(editSlot); RefreshEditor(); };
            ToolTip.SetTip(label, binding.Note ?? binding.Control);
            AutomationProperties.SetName(label, $"{binding.Control}: {binding.Action}. {binding.Note}");
            Place(label, rightHand ? 672 : 0, top);
        }
    }

    private static TextBlock HandHeading(string text) => new()
    {
        Text = text, FontSize = 16, FontWeight = FontWeight.SemiBold, Foreground = SecondaryInk
    };

    private void Place(Control child, double x, double y)
    {
        Canvas.SetLeft(child, x);
        Canvas.SetTop(child, y);
        _canvas.Children.Add(child);
    }
}
