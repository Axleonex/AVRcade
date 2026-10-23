using VrClient.Core.Launch;

namespace VrClient.App.ViewModels;

public partial class MainWindowViewModel
{
    public TroubleshootingGuideViewModel TroubleshootingGuide { get; private set; } = TroubleshootingGuideViewModel.Create([]);
    partial void OnTroubleshootingFindingsChanged(IReadOnlyList<TroubleshootingFinding> value) => RefreshTroubleshootingGuide();
    private void RefreshTroubleshootingGuide()
    {
        TroubleshootingGuide = TroubleshootingGuideViewModel.Create(TroubleshootingFindings, SelectedGame?.VersionRecovery);
        OnPropertyChanged(nameof(TroubleshootingGuide));
    }
    public async Task RunHelpActionAsync(TroubleshootingCardViewModel card)
    {
        if (!CanRunGameDiagnostics) return;
        switch (card.Action)
        {
            case HelpAction.CheckGame: await RunGameDiagnosticsCommand.ExecuteAsync(null); break;
            case HelpAction.Finding:
                if (card.Finding is { } finding) await TroubleshootingActionCommand.ExecuteAsync(finding);
                break;
            case HelpAction.OpenSetup: DetailTabIndex = 0; break;
        }
    }
}
