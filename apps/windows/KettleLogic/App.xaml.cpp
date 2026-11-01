#include "pch.h"

#include "App.xaml.h"

#include "MainWindow.h"
#include "Telemetry.h"

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
            // Only the error's number is reported, as a fixed code, never its message.
            char code[16];
            std::snprintf(code, sizeof code, "%08x", static_cast<unsigned>(args.Exception().value));
            ::kl::telemetry::RecordError("unhandled", code, "ui");
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
