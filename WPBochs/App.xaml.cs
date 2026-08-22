using System;
using Windows.ApplicationModel;
using Windows.ApplicationModel.Activation;
using Windows.Graphics.Display;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media.Animation;
using Windows.UI.Xaml.Navigation;

namespace WPBochs
{
    public sealed partial class App : Application
    {
        private TransitionCollection transitions;

        public App()
        {
            InitializeComponent();
            Suspending += OnSuspending;
            DisplayInformation.AutoRotationPreferences = DisplayOrientations.Landscape | DisplayOrientations.LandscapeFlipped;
        }

        protected override void OnLaunched(LaunchActivatedEventArgs e)
        {
#if DEBUG
            if (System.Diagnostics.Debugger.IsAttached)
            {
                DebugSettings.EnableFrameRateCounter = true;
            }
#endif

            Frame rootFrame = Window.Current.Content as Frame;
            if (rootFrame == null)
            {
                rootFrame = new Frame();
                rootFrame.CacheSize = 1;
                rootFrame.Language = Windows.Globalization.ApplicationLanguages.Languages[0];
                if (e.PreviousExecutionState == ApplicationExecutionState.Terminated) { }
                Window.Current.Content = rootFrame;
            }
            if (rootFrame.Content == null)
            {
                if (rootFrame.ContentTransitions != null)
                {
                    transitions = new TransitionCollection();
                    foreach (Transition c in rootFrame.ContentTransitions) transitions.Add(c);
                }
                rootFrame.ContentTransitions = null;
                rootFrame.Navigated += RootFrame_FirstNavigated;
                if (!rootFrame.Navigate(typeof(MainPage), e.Arguments))
                {
                    throw new Exception("Failed to create initial page");
                }
            }
            Window.Current.Activate();
        }

        private void RootFrame_FirstNavigated(object sender, NavigationEventArgs e)
        {
            Frame rootFrame = sender as Frame;
            rootFrame.ContentTransitions = transitions ?? new TransitionCollection() { new NavigationThemeTransition() };
            rootFrame.Navigated -= RootFrame_FirstNavigated;
        }

        private void OnSuspending(object sender, SuspendingEventArgs e)
        {
            SuspendingDeferral deferral = e.SuspendingOperation.GetDeferral();
            deferral.Complete();
        }

        protected override void OnActivated(IActivatedEventArgs args)
        {
            base.OnActivated(args);
            if (args.Kind == ActivationKind.PickFileContinuation)
            {
                FileOpenPickerContinuationEventArgs continuationArgs = args as FileOpenPickerContinuationEventArgs;
                Frame rootFrame = Window.Current.Content as Frame;
                if (rootFrame != null)
                {
                    MainPage mainPage = rootFrame.Content as MainPage;
                    if (mainPage != null) mainPage.ContinueFileOpenPicker(continuationArgs);
                }
            }
            else if (args.Kind == ActivationKind.PickSaveFileContinuation)
            {
                FileSavePickerContinuationEventArgs continuationArgs = args as FileSavePickerContinuationEventArgs;
                Frame rootFrame = Window.Current.Content as Frame;
                if (rootFrame != null)
                {
                    ImageCreator imageCreatorPage = rootFrame.Content as ImageCreator;
                    if (imageCreatorPage != null) imageCreatorPage.ContinueFileSavePicker(continuationArgs);
                }
            }
        }
    }
}