#include "pch.h"

#include "App.xaml.h"

#include "MainWindow.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::KettleLogic::implementation
{
    App::App()
    {
        InitializeComponent();

        // An error nothing caught is reported, and the app goes on, since the work in progress is autosaved.
        UnhandledException([](auto const&, UnhandledExceptionEventArgs const& args)
        {
            const hstring message = args.Message();
            ::MessageBoxW(nullptr, message.c_str(), L"Kettle Logic", MB_OK | MB_ICONERROR);
            args.Handled(true);
        });
    }

    App::~App() = default;

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        window_ = std::make_unique<::kl::MainWindow>();
        window_->Activate();
    }
}
