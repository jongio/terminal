// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include "TerminalPaneContent.g.h"
#include "BellEventArgs.g.h"
#include "NotificationEventArgs.g.h"
#include "BasicPaneEvents.h"
#include "CopilotActivity.h"
#include "CopilotProcessTracker.h"

namespace winrt::TerminalApp::implementation
{
    struct TerminalSettingsCache;

    struct BellEventArgs : public BellEventArgsT<BellEventArgs>
    {
    public:
        BellEventArgs(bool flashTaskbar, bool sendNotification) :
            FlashTaskbar(flashTaskbar), SendNotification(sendNotification) {}

        til::property<bool> FlashTaskbar;
        til::property<bool> SendNotification;
    };

    struct NotificationEventArgs : public NotificationEventArgsT<NotificationEventArgs>
    {
    public:
        NotificationEventArgs(const winrt::hstring& title = {}, const winrt::hstring& body = {}) :
            Title(title), Body(body) {}

        til::property<winrt::hstring> Title;
        til::property<winrt::hstring> Body;
    };

    struct TerminalPaneContent : TerminalPaneContentT<TerminalPaneContent>, BasicPaneEvents
    {
        TerminalPaneContent(const winrt::Microsoft::Terminal::Settings::Model::Profile& profile,
                            const std::shared_ptr<TerminalSettingsCache>& cache,
                            const winrt::Microsoft::Terminal::Control::TermControl& control);

        winrt::Windows::UI::Xaml::FrameworkElement GetRoot();
        winrt::Microsoft::Terminal::Control::TermControl GetTermControl();
        winrt::Windows::Foundation::Size MinimumSize();
        void Focus(winrt::Windows::UI::Xaml::FocusState reason = winrt::Windows::UI::Xaml::FocusState::Programmatic);
        void Close();

        winrt::Microsoft::Terminal::Settings::Model::INewContentArgs GetNewTerminalArgs(BuildStartupKind kind) const;

        void UpdateSettings(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings,
                            const winrt::Microsoft::Terminal::Settings::Model::WindowSettings& windowSettings);

        void MarkAsDefterm();
        TerminalApp::CopilotActivityState CopilotActivityState() const noexcept;
        void SetCopilotPaneFocused(bool focused);

        winrt::Microsoft::Terminal::Settings::Model::Profile GetProfile() const
        {
            return _profile;
        }

        winrt::hstring Title() { return _control.Title(); }
        uint64_t TaskbarState() { return _control.TaskbarState(); }
        uint64_t TaskbarProgress() { return _control.TaskbarProgress(); }
        bool ReadOnly() { return _control.ReadOnly(); }
        winrt::hstring Icon() const;
        Windows::Foundation::IReference<winrt::Windows::UI::Color> TabColor() const noexcept;
        winrt::Windows::UI::Xaml::Media::Brush BackgroundBrush();

        float SnapDownToGrid(const TerminalApp::PaneSnapDirection direction, const float sizeToSnap);
        Windows::Foundation::Size GridUnitSize();

        til::typed_event<TerminalApp::TerminalPaneContent, winrt::Windows::Foundation::IInspectable> RestartTerminalRequested;
        til::typed_event<TerminalApp::TerminalPaneContent, winrt::Windows::Foundation::IInspectable> CopilotActivityChanged;

        // See BasicPaneEvents for most generic event definitions

    private:
        winrt::Microsoft::Terminal::Control::TermControl _control{ nullptr };
        winrt::Microsoft::Terminal::TerminalConnection::ConnectionState _connectionState{ winrt::Microsoft::Terminal::TerminalConnection::ConnectionState::NotConnected };
        winrt::Microsoft::Terminal::Settings::Model::Profile _profile{ nullptr };
        std::shared_ptr<TerminalSettingsCache> _cache{};
        bool _isDefTermSession{ false };
        bool _detectCopilotActivity{ false };
        CopilotProcessIdentity _copilotRootProcess;
        uint64_t _copilotRegistrationGeneration{};
        std::shared_ptr<CopilotPaneEventState> _copilotEventState{ std::make_shared<CopilotPaneEventState>() };
        CopilotPaneActivity _copilotActivity;
        CopilotProcessTracker::Registration _copilotProcessRegistration;
        SafeDispatcherTimer _copilotWorkingLeaseTimer;
        TerminalApp::CopilotActivityState _publishedCopilotActivity{ TerminalApp::CopilotActivityState::None };

        struct ControlEventTokens
        {
            winrt::Microsoft::Terminal::Control::TermControl::ConnectionStateChanged_revoker _ConnectionStateChanged;
            winrt::Microsoft::Terminal::Control::TermControl::WarningBell_revoker _WarningBell;
            winrt::Microsoft::Terminal::Control::TermControl::CloseTerminalRequested_revoker _CloseTerminalRequested;
            winrt::Microsoft::Terminal::Control::TermControl::RestartTerminalRequested_revoker _RestartTerminalRequested;

            winrt::Microsoft::Terminal::Control::TermControl::TitleChanged_revoker _TitleChanged;
            winrt::Microsoft::Terminal::Control::TermControl::TabColorChanged_revoker _TabColorChanged;
            winrt::Microsoft::Terminal::Control::TermControl::SetTaskbarProgress_revoker _SetTaskbarProgress;
            winrt::Microsoft::Terminal::Control::TermControl::ReadOnlyChanged_revoker _ReadOnlyChanged;
            winrt::Microsoft::Terminal::Control::TermControl::FocusFollowMouseRequested_revoker _FocusFollowMouseRequested;
            winrt::Microsoft::Terminal::Control::TermControl::ShowNotification_revoker _ShowNotification;

        } _controlEvents;
        void _setupControlEvents();
        void _removeControlEvents();

        safe_void_coroutine _controlConnectionStateChangedHandler(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& /*args*/);
        void _controlWarningBellHandler(const winrt::Windows::Foundation::IInspectable& sender,
                                        const winrt::Windows::Foundation::IInspectable& e);
        void _controlReadOnlyChangedHandler(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& e);

        void _controlTitleChanged(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& args);
        void _controlTabColorChanged(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& args);
        void _controlSetTaskbarProgress(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& args);
        void _controlReadOnlyChanged(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& args);
        void _controlFocusFollowMouseRequested(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& args);
        void _controlShowNotification(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Microsoft::Terminal::Control::ShowNotificationEventArgs& args);
        void _copilotWorkingLeaseExpired(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& args);

        void _closeTerminalRequestedHandler(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& /*args*/);
        void _restartTerminalRequestedHandler(const winrt::Windows::Foundation::IInspectable& sender, const winrt::Windows::Foundation::IInspectable& /*args*/);

        void _applyCopilotProgress(const CopilotProgressTransition& transition);
        void _queueCopilotAttention(const CopilotAttentionTransition& transition);
        void _queueCopilotProgress(const CopilotProgressTransition& transition);
        void _refreshCopilotWorkingLease();
        void _publishCopilotActivity();
        void _resetCopilotActivity();
        void _resetCopilotActivity(const CopilotEventOrdering& ordering);
        void _updateCopilotProcessRegistration();
    };
}
