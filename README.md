# WhereDoesTimeGo - Windows Time Tracker

This program tracks your active window usage and displays a pie chart of program usage.

    ⏰ A profiler for your life,
    💧 A leak detector for your day,
    🔍 Finding lost hours since 2026.

![Screenshot](screenshot.png)

## Features

- **Time Tracking**: Records active window information (title, process, duration) to a list. Can manually edit entries, like edit "Away" entries to be more specific (e.g. replace "Away" with "Lunch").
- **Pie Chart**: Displays times by percentage.
- **File Saving**: Can save time entries to CSV file and reload/merge them. Can automatically save on exit to avoid losing data because of IT forced updates.
- **Timer**: If you hide the time entries and pie chart, it shows a large timer that shows how long the current session has been.
- **Power Management**: Automatically pauses tracking when machine is locked, sleeps, or hibernates.

## Keyboard Shortcuts
- **Ctrl+N**: Start new session
- **Ctrl+O**: Open CSV file
- **Ctrl+S**: Save to CSV file
- **Ctrl+Alt+S**: Save As
- **Enter**: Start/stop time tracking
- **Escape**: Stops time tracking
- **Delete**: Remove selected time entries
- **Double-click**/**F2**: Edit entry details

## Technical Implementation

Focus switches are detected with a combination of polling `WM_TIMER` and event hooks via `SetWinEventHook` with `EVENT_SYSTEM_FOREGROUND` (less intrusive than `SetWindowsHookEx`). Each time, `GetSystemTime` is called to log the time.

The application checks the current window using `GetForegroundWindow` and `GetWindowText`, ignoring little popup dialogs (detected via `GetWindow` with `GW_OWNER`) and retrieving the parent instead.

The process name is retrieved via `OpenProcess` with `PROCESS_QUERY_LIMITED_INFORMATION` and `QueryFullProcessImageName` rather than `GetModuleFileNameEx` (which purportedly avoids issues 32-bit vs 64-bit processes and has fewer issues with security restrictions). If that can't retrieved, use the window class name as a fallback.

Any session locks or power management events are detected via `WM_POWERBROADCAST` and `WM_WTSSESSION_CHANGE` messages (enabled by `WTSRegisterSessionNotification` call) so the app can sleep.

## Building

Requires:
- Visual Studio 2019 or later
- Windows SDK 10.0 or later
- User32/GDI/GDI+ (included in Windows SDK)

## CSV Format

- Window Title (quoted)
- Process Name
- Start Time (YYYY-MM-DD HH:MM:SS)
- End Time (YYYY-MM-DD HH:MM:SS)
