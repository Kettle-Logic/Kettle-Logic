#pragma once

#include "App.xaml.g.h"

#include <memory>

namespace kl {
class MainWindow;
}

namespace winrt::KettleLogic::implementation
{
    struct App : AppT<App>
    {
        App();
        ~App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        std::unique_ptr<::kl::MainWindow> window_;
    };
}
