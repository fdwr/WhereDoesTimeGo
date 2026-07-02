// MainWindow

#include "Precomp.h"
#include "MainWindow.h"

#define MAX_LOADSTRING 100
#define TIMER_TRACK_ID 1
#define DEFAULT_POLLING_INTERVAL 0 // Never - use SetWinEventHook instead.
#define WM_TRAYICON (WM_USER + 100)
#define TRAY_ICON_ID 1

namespace ToolbarIcon
{
    enum ToolbarIcon
    {
        NewFile,
        OpenFile,
        MergeFile,
        SaveFile,
        SaveFileAs,
        EditTimeEntry,
        InsertTimeEntry,
        DeleteTimeEntry,
        ClearTimeEntries,
        StartTimeTracking,
        LapTimeTracking,
        StopTimeTracking,
        ShowTimeEntries,
        ShowTimer,
        ShowPieChart,
        ShowCalendar,
    };
}

struct TimeEntry
{
    std::wstring windowTitle;
    std::wstring processName;
    SYSTEMTIME startTime;
    SYSTEMTIME endTime;
    UINT64 durationMilliseconds;

    void ResetStartAndEndTimeToNow();
    void SetStartTimeToEndTime();
    void SetEndTimeToNow(); // Wrap up this entry by setting the end time to now and calculating the duration.
    void RecomputeDuration();
};

struct AggregatedTimeEntry
{
    std::wstring processName;
    UINT64 totalMilliseconds;
    float percentage;
};

// Global Variables:
HINSTANCE g_instanceHandle;
WCHAR g_defaultWindowTitle[MAX_LOADSTRING];
WCHAR g_windowClassName[MAX_LOADSTRING];

HWND g_hWndMainWindow = nullptr;
HWND g_hWndToolbar = nullptr;
HWND g_hwndTimeEntriesList = nullptr;
HWND g_hwndAggregatedTimeEntriesList = nullptr;
HWND g_hwndPieChart = nullptr;
HWND g_hwndCalendar = nullptr;
HWND g_hwndTimerDisplay = nullptr;
HWND g_hwndLabelTimeEntries = nullptr;
HWND g_hwndLabelTasks = nullptr;
HWND g_hwndLabelEmptyState = nullptr;
HWND g_hwndLastFocus = nullptr;
HWINEVENTHOOK g_hWinEventHook = nullptr;

bool g_isTrackingTime = false;
bool g_isUserAway = false;
bool g_needsUIRefresh = false;
bool g_showTimeEntries = true;
bool g_showPieChart = true;
bool g_showCalendar = true;
bool g_showTimer = true;
bool g_saveOnExit = false;
bool g_showAwayTime = true;
bool g_showSelf = true;
bool g_trackChildDialogs = true;
int g_pollingInterval = DEFAULT_POLLING_INTERVAL;

std::vector<TimeEntry> g_timeEntries;
std::vector<AggregatedTimeEntry> g_aggregatedEntries;
std::wstring g_timeEntriesFilePath;
bool g_timeEntriesAreModified = false;
SYSTEMTIME g_lastRecordedTime = {}; // Note UTC, not local.
SYSTEMTIME g_lastSelectedTime = {}; // Note UTC, not local.
TimeEntry g_editingEntry = {};
int g_hoveredTimeEntryIndex = -1; // Whatever the mouse was last over (e.g. in the calendar).
int g_hoveredAggregateEntryIndex = -1; // Whatever the mouse was last over (e.g. in the pie chart).

ULONG_PTR g_gdiplusToken = 0;
HFONT g_hLabelFont = nullptr;
HFONT g_hTimerFont = nullptr;
HFONT g_hMegaTimerFont = nullptr;
bool g_isTrayIconActive = false;

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitalizeInstance(HINSTANCE hInstance, int nCmdShow);
LRESULT CALLBACK WindowProcedure(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AboutDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK EditTimeEntryDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK EmptyListboxSubclassProcedure(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
void CALLBACK WinEventHookProcedure(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);

void CreateControls(HWND hWnd);
void ResizeControls(HWND hWnd);
void MinimizeToSystemTray(HWND hWnd);
void RestoreFromSystemTray(HWND hWnd);
void RemoveSystemTrayIcon(HWND hWnd);

void StartTracking(HWND hWnd);
void StopTracking(HWND hWnd, bool noUiUpdates = false);
void PauseTrackingBecauseAway(HWND hWnd);
void ResumeTrackingBecauseBack(HWND hWnd);
void RecalculateAggregatedData();
void UpdateTimeEntriesList();
void UpdateAggregatedTimeEntriesList();
void UpdateTimerDisplay();
void RefreshUI();
void SelectLastItemInRawList();

void RecordActiveWindowDetails(bool tryToMergeWithPreviousEntry);
void RecordInactiveState();
std::wstring GetProcessName(HWND hWnd);
void UpdateWindowTitle();
void SetTimeEntriesFilePath(std::wstring_view filePath);
void SetFileModifiedState(bool isModified);

void DrawPieChart(HDC hdc, RECT& rect);
void DrawCalendar(HDC hdc, RECT& rect);
int MapPieChartCoordinateToTaskIndex(POINT pt, RECT& rect);
int MapCalendarCoordinateToTimeEntryIndex(POINT pt, RECT& rect);
bool UpdateListboxHoverIndex(HWND hwndListbox, int& currentHoverIndex, int newHoverIndex);
void DrawListboxItem(std::wstring_view text, HDC hdc, const RECT& rcItem, UINT itemState);

bool WriteTextFile(const WCHAR* filename, std::wstring_view content);
bool ReadTextFile(const WCHAR* filename, std::wstring& outContent);
bool ParseCSVEntries(const std::wstring& fileContent, std::vector<TimeEntry>& outEntries);
void SortTimeEntriesByTime();
std::wstring GetUserDocumentsDirectory();
std::wstring GetSettingsFilePath(bool createPath = false, bool addFileName = true);
void NewFile(HWND hWnd);
void LoadFromCSV(HWND hWnd, bool mergeWithExisting = false);
void LoadFromCSV(HWND hWnd, const wchar_t* filePath, bool mergeWithExisting = false);
void SaveToCSV(HWND hWnd);
void SaveAsToCSV(HWND hWnd);
void SaveToCSV(HWND hwnd, const wchar_t* filePath);
void LoadSettings();
void SaveSettings();

void EditSelectedEntry(HWND hWnd);
void InsertEntry(HWND hWnd);
void AddLapEntry(HWND hWnd);
void DeleteSelectedEntries();
void ClearAllEntries(HWND hWnd, bool isNewFile);

bool IsSystemTimeAfterOrEqual(const SYSTEMTIME& time1, const SYSTEMTIME& time2);
UINT64 CalculateDurationInMilliseconds(const SYSTEMTIME& start, const SYSTEMTIME& end);
std::wstring FormatDateTime(const SYSTEMTIME& time, bool adjustToLocalTime);
std::wstring FormatDuration(UINT64 milliseconds);

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow
    )
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    LoadStringW(hInstance, IDS_APP_TITLE, g_defaultWindowTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_WHEREDOESTIMEGO, g_windowClassName, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    LoadSettings();

    if (!InitalizeInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_WHEREDOESTIMEGO));

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        HWND hWndParent = GetParent(msg.hwnd);
        if (!TranslateAccelerator(g_hWndMainWindow, hAccelTable, &msg))
        {
            if (!IsDialogMessage(hWndParent, &msg)) // Dispatch tab keypresses.
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    SaveSettings();
    Gdiplus::GdiplusShutdown(g_gdiplusToken);

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = &WindowProcedure;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WHEREDOESTIMEGO));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_WHEREDOESTIMEGO);
    wcex.lpszClassName = g_windowClassName;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_WHEREDOESTIMEGO));

    return RegisterClassExW(&wcex);
}

BOOL InitalizeInstance(HINSTANCE hInstance, int showCommand)
{
    g_instanceHandle = hInstance;

    GetSystemTime(&g_lastSelectedTime); // Set to now.

    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    HWND hWnd = CreateWindowW(g_windowClassName, g_defaultWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 1200, 760, nullptr, nullptr, hInstance, nullptr);
    g_hWndMainWindow = hWnd;

    if (!hWnd)
    {
        return FALSE;
    }

    // Enable WM_WTSSESSION_CHANGE messages.
    WTSRegisterSessionNotification(hWnd, NOTIFY_FOR_THIS_SESSION);

    CreateControls(hWnd);

    ShowWindow(hWnd, showCommand);
    UpdateWindow(hWnd);

    return TRUE;
}

template<typename T, typename Callable>
void DeleteResourceAndNullify(T& object, Callable F)
{
    if (object != nullptr)
    {
        F(object);
        object = nullptr;
    }
}

template<typename T>
void DeleteObjectAndNullify(T& object)
    requires(
        std::is_same_v<T, HPEN> ||
        std::is_same_v<T, HBRUSH> ||
        std::is_same_v<T, HFONT> ||
        std::is_same_v<T, HBITMAP> ||
        std::is_same_v<T, HRGN> ||
        std::is_same_v<T, HPALETTE>
    )
{
    DeleteResourceAndNullify(object, &DeleteObject);
}

void CreateControls(HWND hWnd)
{
    NONCLIENTMETRICS nonClientMetrics = {};
    nonClientMetrics.cbSize = sizeof(NONCLIENTMETRICS);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &nonClientMetrics, 0);
    g_hLabelFont = CreateFontIndirect(&nonClientMetrics.lfMessageFont);

    // Create large timer font, and an even larger one for timer-only mode.
    g_hTimerFont = CreateFont(64, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_hMegaTimerFont = CreateFont(180, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_hWndToolbar = CreateWindowEx(WS_EX_STATICEDGE, TOOLBARCLASSNAME, nullptr, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP | WS_TABSTOP, 0, 0, 0, 0, hWnd, (HMENU)IDC_TOOLBAR, g_instanceHandle, nullptr);

    SendMessage(g_hWndToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    // Load the toolbar bitmap and create an image list with alpha support.
    HBITMAP hBitmap = LoadBitmap(g_instanceHandle, MAKEINTRESOURCE(IDB_TOOLBAR));
    if (hBitmap)
    {
        HIMAGELIST hImageList = ImageList_Create(32, 32, ILC_COLOR32, 7, 0);
        if (hImageList)
        {
            ImageList_Add(hImageList, hBitmap, nullptr);
            SendMessage(g_hWndToolbar, TB_SETIMAGELIST, 0, (LPARAM)hImageList);
        }
        DeleteObject(hBitmap);
    }

    TBBUTTON toolbarButtons[] =
    {
        { ToolbarIcon::NewFile, IDM_NEW, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"New" },
        { ToolbarIcon::OpenFile, IDM_OPEN, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Open" },
        { ToolbarIcon::MergeFile, IDM_MERGE, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Merge" },
        { ToolbarIcon::SaveFile, IDM_SAVE, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Save" },
        { ToolbarIcon::SaveFileAs, IDM_SAVEAS, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Save As" },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { ToolbarIcon::StartTimeTracking, IDM_START_TIME_TRACKING, TBSTATE_ENABLED, BTNS_GROUP, {0}, 0, (INT_PTR)L"Start" },
        { ToolbarIcon::StopTimeTracking, IDM_STOP_TIME_TRACKING, TBSTATE_ENABLED | TBSTATE_CHECKED, BTNS_GROUP, {0}, 0, (INT_PTR)L"Stop" },
        { ToolbarIcon::LapTimeTracking, IDM_ADD_LAP_ENTRY, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Lap" },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { ToolbarIcon::EditTimeEntry, IDM_EDIT, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Edit" },
        { ToolbarIcon::InsertTimeEntry, IDM_INSERT, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Insert" },
        { ToolbarIcon::DeleteTimeEntry, IDM_DELETE, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Delete" },
        { ToolbarIcon::ClearTimeEntries, IDM_CLEAR_ENTRIES, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, (INT_PTR)L"Clear" },
        { 0, 0, 0, BTNS_SEP, {0}, 0, 0 },
        { ToolbarIcon::ShowTimeEntries, IDM_SHOW_TIME_ENTRIES, BYTE(TBSTATE_ENABLED | (g_showTimeEntries ? TBSTATE_CHECKED : 0)), BTNS_CHECK, {0}, 0, (INT_PTR)L"Entries" },
        { ToolbarIcon::ShowTimer, IDM_SHOW_TIMER, BYTE(TBSTATE_ENABLED | (g_showTimer ? TBSTATE_CHECKED : 0)), BTNS_CHECK, {0}, 0, (INT_PTR)L"Timer" },
        { ToolbarIcon::ShowPieChart, IDM_SHOW_PIE_CHART, BYTE(TBSTATE_ENABLED | (g_showPieChart ? TBSTATE_CHECKED : 0)), BTNS_CHECK, {0}, 0, (INT_PTR)L"Pie Chart" },
        { ToolbarIcon::ShowCalendar, IDM_SHOW_CALENDAR, BYTE(TBSTATE_ENABLED | (g_showCalendar ? TBSTATE_CHECKED : 0)), BTNS_CHECK, {0}, 0, (INT_PTR)L"Calendar" },
    };

    SendMessage(g_hWndToolbar, TB_ADDBUTTONS, sizeof(toolbarButtons) / sizeof(TBBUTTON), (LPARAM)&toolbarButtons);

    // Create labels
    g_hwndLabelTimeEntries = CreateWindowEx(0, L"STATIC", L"Time Entries:", WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 50, 300, 20, hWnd, (HMENU)IDC_LABEL_TIME_ENTRIES, g_instanceHandle, nullptr);
    g_hwndLabelTasks = CreateWindowEx(0, L"STATIC", L"Tasks:", WS_CHILD | WS_VISIBLE | SS_LEFT, 320, 50, 300, 20, hWnd, (HMENU)IDC_LABEL_TASKS, g_instanceHandle, nullptr);
    g_hwndTimeEntriesList = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTBOX, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_EXTENDEDSEL | LBS_MULTIPLESEL | LBS_NOINTEGRALHEIGHT | LBS_NODATA | WS_TABSTOP, 10, 70, 300, 480, hWnd, (HMENU)IDC_RAW_TIME_ENTRY_LIST, g_instanceHandle, nullptr);
    g_hwndAggregatedTimeEntriesList = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTBOX, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT | LBS_NODATA | WS_TABSTOP, 320, 70, 300, 480, hWnd, (HMENU)IDC_AGGREGATED_TIME_ENTRY_LIST, g_instanceHandle, nullptr);
    g_hwndPieChart = CreateWindowEx(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 630, 50, 500, 500, hWnd, (HMENU)IDC_PIECHART, g_instanceHandle, nullptr);
    g_hwndCalendar = CreateWindowEx(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 630, 50, 500, 500, hWnd, (HMENU)IDC_CALENDAR, g_instanceHandle, nullptr);
    g_hwndTimerDisplay = CreateWindowEx(0, L"STATIC", L"00h 00m 00s", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE , 630, 550, 500, 60, hWnd, (HMENU)IDC_TIMER_DISPLAY, g_instanceHandle, nullptr);
    g_hwndLabelEmptyState = CreateWindowEx(0, L"STATIC", L"Click View / Show Entries, Timer, Pie Chart, or Calendar to see them.\r\n\r\n⏰ A profiler for your life,\r\n💧 A leak detector for your day,\r\n🔍 Finding lost hours since 2026.", WS_CHILD | SS_CENTER, 0, 0, 0, 0, hWnd, (HMENU)IDC_LABEL_EMPTY_STATE, g_instanceHandle, nullptr);

    SetWindowFont(g_hwndLabelTimeEntries, g_hLabelFont, FALSE);
    SetWindowFont(g_hwndLabelTasks, g_hLabelFont, FALSE);
    SetWindowFont(g_hwndLabelEmptyState, g_hLabelFont, FALSE);
    SetWindowFont(g_hwndTimerDisplay, g_hTimerFont, FALSE);

    // Subclass the listboxes to paint empty-state watermark messages.
    SetWindowSubclass(g_hwndTimeEntriesList, &EmptyListboxSubclassProcedure, 0, (DWORD_PTR)L"Time entries empty. Click Start to record.");
    SetWindowSubclass(g_hwndAggregatedTimeEntriesList, &EmptyListboxSubclassProcedure, 0, (DWORD_PTR)L"Tasks empty. Click Start to record.");
}

void ResizeControls(HWND hWnd)
{
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);

    SendMessage(g_hWndToolbar, TB_AUTOSIZE, 0, 0);
    RECT toolbarRect;
    GetWindowRect(g_hWndToolbar, &toolbarRect);
    int toolbarHeight = toolbarRect.bottom - toolbarRect.top;

    int spacing = 8;
    int labelListSpacing = 2; // Minor spacing between label and listbox.
    int labelHeight = 20; // Average height for a label, like the timer entries and tasks labels.
    int timerHeight = 60; // Height of normal timer (not mega timer used in timer-only mode).
    int topMargin = toolbarHeight + spacing;
    int bottomMargin = spacing;
    int leftMargin = spacing;
    int rightMargin = spacing;

    int availableWidth = clientRect.right - leftMargin - rightMargin;
    int availableHeight = clientRect.bottom - topMargin - bottomMargin;

    if (g_showTimer)
    {
        if (g_showTimeEntries || g_showPieChart || g_showCalendar)
        {
            // Use normal font if in normal mode.
            // Position timer at the bottom spanning full width (unless it's the only visible control).
            SetWindowFont(g_hwndTimerDisplay, g_hTimerFont, TRUE);
            int timerTop = topMargin + availableHeight - timerHeight;
            SetWindowPos(g_hwndTimerDisplay, nullptr, leftMargin, timerTop, availableWidth, timerHeight, SWP_NOZORDER | SWP_NOCOPYBITS);

            // Reserve space for timer at bottom if showing timer and other controls.
            availableHeight -= (timerHeight + spacing);
        }
        else // only timer visible
        {
            // Timer-only mode hides everything else and shows timer at full size.
            // Use mega font for timer-only mode, and position timer to fill entire available area.
            SetWindowFont(g_hwndTimerDisplay, g_hMegaTimerFont, TRUE);
            SetWindowPos(g_hwndTimerDisplay, nullptr, leftMargin, topMargin, availableWidth, availableHeight, SWP_NOZORDER | SWP_NOCOPYBITS);
        }
    }

    int totalHorizontalElements = (g_showTimeEntries ? 1 : 0) + (g_showPieChart ? 1 : 0) + (g_showCalendar ? 1 : 0);
    int currentX = leftMargin;
    int listTopMargin = topMargin + labelHeight + labelListSpacing;
    int elementWidth = (availableWidth - (spacing * (totalHorizontalElements - 1))) / std::max(totalHorizontalElements, 1);

    // Vertically stack time entries (raw and aggregated lists).
    if (g_showTimeEntries)
    {
        int listWidth = elementWidth;
        int halfListHeight = ((availableHeight - spacing) / 2) - labelHeight - labelListSpacing;

        // Time Entries list is on top.
        SetWindowPos(g_hwndLabelTimeEntries, nullptr, currentX, topMargin, listWidth, labelHeight, SWP_NOZORDER);
        SetWindowPos(g_hwndTimeEntriesList, nullptr, currentX, listTopMargin, listWidth, halfListHeight, SWP_NOZORDER | SWP_NOCOPYBITS);

        // Tasks list is below.
        int bottom = topMargin + availableHeight;
        int tasksLabelTop = listTopMargin + halfListHeight + spacing;
        int tasksListTop = tasksLabelTop + labelHeight + labelListSpacing;
        halfListHeight = bottom - tasksListTop;
        SetWindowPos(g_hwndLabelTasks, nullptr, currentX, tasksLabelTop, listWidth, labelHeight, SWP_NOZORDER);
        SetWindowPos(g_hwndAggregatedTimeEntriesList, nullptr, currentX, tasksListTop, listWidth, halfListHeight, SWP_NOZORDER | SWP_NOCOPYBITS);

        currentX += listWidth + spacing;
    }

    // Show/hide and position pie chart.
    if (g_showPieChart)
    {
        int pieChartWidth = elementWidth;
        int pieChartHeight = availableHeight;

        SetWindowPos(g_hwndPieChart, nullptr, currentX, topMargin, pieChartWidth, pieChartHeight, SWP_NOZORDER | SWP_NOCOPYBITS);

        currentX += pieChartWidth + spacing;
    }

    // Show/hide and position calendar.
    if (g_showCalendar)
    {
        int calendarWidth = elementWidth;
        int calendarHeight = availableHeight;

        SetWindowPos(g_hwndCalendar, nullptr, currentX, topMargin, calendarWidth, calendarHeight, SWP_NOZORDER | SWP_NOCOPYBITS);
    }

    // Show/hide empty state message.
    bool allHidden = !g_showTimeEntries && !g_showTimer && !g_showPieChart && !g_showCalendar;
    if (allHidden)
    {
        // Center the empty state label. Unfortunately SS_CENTERIMAGE only works with single lines. So, estimate here.
        int labelWidth = 200;
        int labelHeight = 92;
        int labelX = (clientRect.right - labelWidth) / 2;
        int labelY = (clientRect.bottom - topMargin - labelHeight) / 2 + topMargin;
        SetWindowPos(g_hwndLabelEmptyState, nullptr, labelX, labelY, labelWidth, labelHeight, SWP_NOZORDER);
    }

    ShowWindow(g_hwndLabelTimeEntries, g_showTimeEntries ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hwndLabelTasks, g_showTimeEntries ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hwndTimeEntriesList, g_showTimeEntries ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hwndAggregatedTimeEntriesList, g_showTimeEntries ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hwndPieChart, g_showPieChart ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hwndCalendar, g_showCalendar ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hwndTimerDisplay, g_showTimer ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hwndLabelEmptyState, allHidden ? SW_SHOW : SW_HIDE);
}

void UpdateTrackingTimer()
{
    if (g_isTrackingTime && !g_isUserAway)
    {
        // If the window is active (and not minimized/hidden), update every second for visual interactivity.
        if (IsWindowVisible(g_hWndMainWindow))
        {
            SetTimer(g_hWndMainWindow, TIMER_TRACK_ID, 1000, nullptr);
        }
        // Otherwise if minimized/hidden, update less frequently to save resources, since the user can't see the updates anyway.
        else if (g_pollingInterval > 0)
        {
            SetTimer(g_hWndMainWindow, TIMER_TRACK_ID, g_pollingInterval, nullptr);
        }
        else
        {
            KillTimer(g_hWndMainWindow, TIMER_TRACK_ID);
        }
    }
    else
    {
        KillTimer(g_hWndMainWindow, TIMER_TRACK_ID);
    }
}

void StartTracking(HWND hWnd)
{
    if (!g_isTrackingTime)
    {
        g_isTrackingTime = true;
        GetSystemTime(&g_lastRecordedTime); // Reset selected time to now.
        SetFileModifiedState(true);
        UpdateTrackingTimer();

        // Capture foreground window changes, which can be a finer granularity than the polling timer.
        g_hWinEventHook = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_FOREGROUND,
            nullptr,
            &WinEventHookProcedure,
            0, // idProcess
            0, // idThread
            WINEVENT_OUTOFCONTEXT
        );

        RecordActiveWindowDetails(/*tryToMergeWithPreviousEntry*/ false);
        RefreshUI();

        // Update the toolbar Start and Stop buttons.
        SendMessage(g_hWndToolbar, TB_CHECKBUTTON, IDM_START_TIME_TRACKING, MAKELONG(TRUE, 0));
    }
    else
    {
        // Pressing Start a second time stops tracking, as a convenient shortcut,
        // similar to how pressing the Start/Stop button on a physical stopwatch would work.
        StopTracking(hWnd);
    }
}

void StopTracking(HWND hWnd, bool noUiUpdates)
{
    if (g_isTrackingTime)
    {
        g_isTrackingTime = false;
        UpdateTrackingTimer();
        DeleteResourceAndNullify(g_hWinEventHook, &UnhookWinEvent);

        // Finalize the tail end of the entries with the final time.
        if (!g_timeEntries.empty())
        {
            SetFileModifiedState(true);
            g_timeEntries.back().SetEndTimeToNow();
        }

        if (!noUiUpdates)
        {
            RefreshUI();

            // Update the toolbar Start and Stop buttons.
            SendMessage(g_hWndToolbar, TB_CHECKBUTTON, IDM_STOP_TIME_TRACKING, MAKELONG(TRUE, 0));
        }
    }
}

void PauseTrackingBecauseAway(HWND hWnd)
{
    g_isUserAway = true;
    if (g_isTrackingTime)
    {
        UpdateTrackingTimer();
        RecordInactiveState();
        RefreshUI();
    }
}

void ResumeTrackingBecauseBack(HWND hWnd)
{
    g_isUserAway = false;
    if (g_isTrackingTime)
    {
        UpdateTrackingTimer();
        // Complete the away time entry.
        if (!g_timeEntries.empty())
        {
            TimeEntry& lastEntry = g_timeEntries.back();
            lastEntry.SetEndTimeToNow();
        }
        RecordActiveWindowDetails(/*tryToMergeWithPreviousEntry*/ false);
        RefreshUI();
    }
}

bool WindowTitlesAreEquivalent(std::wstring_view first, std::wstring_view second)
{
    // Windows frequently indicate document save status with a little circle (asterisk, bullet, black circle, black large circle...)
    // which doesn't meaningfully change the window title, and we don't want to create separate time entries for the same window
    // just because of such a status change.
    //
    // Some other interesting cases include file copies updating the title bar ("55%", "56%", ...), but those need more complex
    // handling to avoid false positives. So for now we'll just handle the save status indicators.

    constexpr wchar_t ignorableCharacters[] = { L' ', L'*', L'•', L'●', L'⬤'};
    for (size_t i = 0, j = 0; i < first.size() && j < second.size(); )
    {
        wchar_t ch1 = first[i];
        wchar_t ch2 = second[j];

        if (std::ranges::contains(ignorableCharacters, ch1))
        {
            ++i;
            continue;
        }
        if (std::ranges::contains(ignorableCharacters, ch2))
        {
            ++j;
            continue;
        }
        if (ch1 != ch2)
        {
            return false;
        }
        ++i;
        ++j;
    }
    return true;
}

// Record the active window's details (title, process, start time), either extending the previous entry
// or creating a new time entry if the active window has changed since the last.
void RecordActiveWindowDetails(bool tryToMergeWithPreviousEntry)
{
    WCHAR title[256] = {};

    HWND hForeground = GetForegroundWindow();

    // There can be weird transient states where the foreground window is null , such as when the user
    // is coming out from the lock screen and the original foreground window has not been reset yet.
    if (hForeground == nullptr)
    {
        return; // There is no foreground window.
    }

    if (!g_trackChildDialogs)
    {
        // Get the main application window rather the the dialog name.
        HWND hOwner = GetWindow(hForeground, GW_OWNER);
        if (hOwner)
        {
            hForeground = hOwner;
        }
    }
    GetWindowText(hForeground, title, 256);

    std::wstring windowTitle = title;
    std::wstring processName = GetProcessName(hForeground);

    // For derpy UWP apps, retry to get the actual process using the focus window.
    // We still use the active HWND by default because otherwise some cases (like with Explorer's taskbar)
    // don't yield any useful result as the focus HWND is still null.
    if (processName == L"ApplicationFrameHost.exe")
    {
        GUITHREADINFO guiThreadInfo = { sizeof(GUITHREADINFO) };
        GetGUIThreadInfo(0, &guiThreadInfo); // 0 means to use the foreground thread.

        // Use hwndFocus rather than hwndActive (as active would just be the framehost again).
        // https://stackoverflow.com/a/51946137/937938
        if (guiThreadInfo.hwndFocus != nullptr)
        {
            processName = GetProcessName(guiThreadInfo.hwndFocus);
        }
    }

    SetFileModifiedState(true);

    SYSTEMTIME now;
    GetSystemTime(&now);

    // Try to merge this entry with the previous one if it's the same window & process
    // and the entry is consistent with the last recorded time (to rule out weird effects
    // where you delete an entry in the middle of recording, which causes the last entry's
    // end time to update to now and suddenly introduce a large gap).
    if (tryToMergeWithPreviousEntry && !g_timeEntries.empty())
    {
        TimeEntry& lastEntry = g_timeEntries.back();

        // Update the end time of the last time entry.
        if (IsSystemTimeAfterOrEqual(lastEntry.endTime, g_lastRecordedTime))
        {
            lastEntry.endTime = now;
            g_lastRecordedTime = now;
            lastEntry.RecomputeDuration();

            if (lastEntry.processName == processName && WindowTitlesAreEquivalent(lastEntry.windowTitle, windowTitle))
            {
                // Continue the previous entry since it's the same window.
                return;
            }
        }
        // Different window. So fall through to add a new entry...
    }

    TimeEntry newEntry =
    {
        .windowTitle = windowTitle,
        .processName = processName,
        .startTime = now,
        .endTime = now,
        .durationMilliseconds = 0,
    };
    g_lastRecordedTime = now;

    g_timeEntries.push_back(std::move(newEntry));
}

void RecordInactiveState()
{
    if (!g_timeEntries.empty())
    {
        g_timeEntries.back().SetEndTimeToNow();
    }

    TimeEntry newEntry =
    {
        .windowTitle = L"Away",
        .processName = L"Away",
    };
    newEntry.ResetStartAndEndTimeToNow();
    g_lastRecordedTime = newEntry.endTime;

    g_timeEntries.push_back(std::move(newEntry));
    SetFileModifiedState(true);
}

void SelectLastItemInRawList()
{
    if (GetFocus() != g_hwndTimeEntriesList)
    {
        int count = (int)SendMessage(g_hwndTimeEntriesList, LB_GETCOUNT, 0, 0);
        if (count > 0)
        {
            SendMessage(g_hwndTimeEntriesList, LB_SETTOPINDEX, count - 1, 0);
        }
    }
}

std::wstring GetProcessName(HWND hWnd)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(hWnd, &processId);

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess)
    {
        DWORD pathLength = MAX_PATH * 4;
        WCHAR path[MAX_PATH * 4] = {};

        // Use QueryFullProcessImageName over GetModuleFileNameEx since we only care about the exe,
        // not any DLLs loaded in that exe, as it's more robust to limited information cases and WoW64.
        if (QueryFullProcessImageName(hProcess, 0, path, &pathLength))
        {
            std::wstring fullPath = path;
            size_t pos = fullPath.find_last_of(L"\\/");
            if (pos != std::wstring::npos)
            {
                CloseHandle(hProcess);
                return fullPath.substr(pos + 1);
            }
        }
        CloseHandle(hProcess);
    }

    // Fallback case - use window class name for processes we can't open.
    // I'm not sure if this actually works, because I haven't found a case, but it could occur if run with limited permissions.
    WCHAR className[256] = {};
    if (GetClassName(hWnd, className, 256) > 0)
    {
        std::wstring classNameString = className;
        
        // Detect UWP/Windows Store apps.
        if (classNameString == L"ApplicationFrameWindow" || 
            classNameString == L"Windows.UI.Core.CoreWindow")
        {
            return L"UWP App";
        }
        
        // Return class name as fallback.
        if (!classNameString.empty())
        {
            return classNameString;
        }
    }

    std::wstring name = std::format(L"Unknown{}-{}", processId, int(hWnd));
    return name;
}

LRESULT CALLBACK EmptyListboxSubclassProcedure(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_PAINT)
    {
        // If the list is empty, draw centered text on opaque background using the message string stored in dwRefData.
        int itemCount = (int)SendMessage(hWnd, LB_GETCOUNT, 0, 0);
        if (itemCount == 0 || itemCount == LB_ERR)
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT clientRect;
            GetClientRect(hWnd, &clientRect);

            const wchar_t* message = (const wchar_t*)dwRefData;
            if (message != nullptr)
            {
                HFONT oldFont = (HFONT)SelectObject(hdc, g_hLabelFont);
                SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
                SetBkMode(hdc, OPAQUE);
                SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
                DrawText(hdc, message, -1, &clientRect, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
                SelectObject(hdc, oldFont);
            }

            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    else if (uMsg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hWnd, EmptyListboxSubclassProcedure, uIdSubclass);
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void UpdateTimeEntriesList()
{
    int listboxCount = (int)SendMessage(g_hwndTimeEntriesList, LB_GETCOUNT, 0, 0);
    int entriesCount = (int)g_timeEntries.size();

    if (listboxCount != entriesCount)
    {
        // Save selection state before LB_SETCOUNT destroys it.
        int selectionCount = (int)SendMessage(g_hwndTimeEntriesList, LB_GETSELCOUNT, 0, 0);
        std::vector<int> selectedIndices;
        if (selectionCount > 0) // Note LB_ERR == -1, which tests for that too.
        {
            selectedIndices.resize(selectionCount);
            SendMessage(g_hwndTimeEntriesList, LB_GETSELITEMS, selectionCount, (LPARAM)selectedIndices.data());
        }

        // Preserve scroll position when count changes.
        int caretIndex = (int)SendMessage(g_hwndTimeEntriesList, LB_GETCARETINDEX, 0, 0);
        int topIndex = (int)SendMessage(g_hwndTimeEntriesList, LB_GETTOPINDEX, 0, 0);

        SendMessage(g_hwndTimeEntriesList, LB_SETCOUNT, entriesCount, 0);

        // Restore selection state.
        for (int index : selectedIndices)
        {
            if (index < entriesCount)
            {
                SendMessage(g_hwndTimeEntriesList, LB_SETSEL, TRUE, index);
            }
        }

        // Changing the count sadly resets the caret and top index. So restore them.
        SendMessage(g_hwndTimeEntriesList, LB_SETCARETINDEX, caretIndex, 0);
        SendMessage(g_hwndTimeEntriesList, LB_SETTOPINDEX, topIndex, 0);
    }

    if (entriesCount > 0)
    {
        RECT itemRect;
        SendMessage(g_hwndTimeEntriesList, LB_GETITEMRECT, entriesCount - 1, (LPARAM)&itemRect);
        InvalidateRect(g_hwndTimeEntriesList, &itemRect, TRUE);
    }
}

void UpdateAggregatedTimeEntriesList()
{
    int listboxCount = (int)SendMessage(g_hwndAggregatedTimeEntriesList, LB_GETCOUNT, 0, 0);
    int entriesCount = (int)g_aggregatedEntries.size();

    if (listboxCount != entriesCount)
    {
        SendMessage(g_hwndAggregatedTimeEntriesList, LB_SETCOUNT, entriesCount, 0);
    }

    InvalidateRect(g_hwndAggregatedTimeEntriesList, nullptr, TRUE);
}

void UpdateTimerDisplay()
{
    // Calculate total time from all entries.
    UINT64 totalMilliseconds = 0;
    for (const auto& entry : g_aggregatedEntries)
    {
        totalMilliseconds += entry.totalMilliseconds;
    }

    std::wstring text = FormatDuration(totalMilliseconds);
    SetWindowText(g_hwndTimerDisplay, text.c_str());
}

void RefreshUI()
{
    if (g_hWndMainWindow && !IsIconic(g_hWndMainWindow))
    {
        UpdateTimeEntriesList();
        RecalculateAggregatedData();
        UpdateAggregatedTimeEntriesList();
        UpdateTimerDisplay();
        InvalidateRect(g_hwndPieChart, nullptr, TRUE);
        InvalidateRect(g_hwndCalendar, nullptr, TRUE);

        // If tracking is active, scroll to show the latest entry.
        if (g_isTrackingTime && !g_timeEntries.empty())
        {
            SelectLastItemInRawList();
        }

        g_needsUIRefresh = false;
    }
    else
    {
        g_needsUIRefresh = true;
    }
}

void RecalculateAggregatedData()
{
    g_aggregatedEntries.clear();

    std::map<std::wstring_view, UINT64> processTimeMap;
    UINT64 totalTime = 0; // In milliseconds

    for (const auto& entry : g_timeEntries)
    {
        // Apply filters.
        if (!g_showAwayTime && entry.processName == L"Away")
        {
            continue;
        }
        if (!g_showSelf && entry.processName == L"WhereDoesTimeGo.exe")
        {
            continue;
        }

        processTimeMap[entry.processName] += entry.durationMilliseconds;
        totalTime += entry.durationMilliseconds;
    }

    for (const auto& [processName, milliseconds] : processTimeMap)
    {
        AggregatedTimeEntry aggregateTimeEntry = {};
        aggregateTimeEntry.processName = processName;
        aggregateTimeEntry.totalMilliseconds = milliseconds;
        aggregateTimeEntry.percentage = totalTime > 0 ? (float)milliseconds / totalTime * 100.0f : 100.0f;
        g_aggregatedEntries.push_back(aggregateTimeEntry);
    }

    std::ranges::sort(
        g_aggregatedEntries,
        [](const AggregatedTimeEntry& a, const AggregatedTimeEntry& b) {
            return a.totalMilliseconds > b.totalMilliseconds;
        }
    );
}

Gdiplus::Color g_chartColors[] =
{
    Gdiplus::Color(255,  66, 133, 244),
    Gdiplus::Color(255, 234,  67,  53),
    Gdiplus::Color(255, 251, 188,   5),
    Gdiplus::Color(255,  52, 168,  83),
    Gdiplus::Color(255, 171,  71, 188),
    Gdiplus::Color(255, 255, 112,  67),
    Gdiplus::Color(255,   0, 172, 193),
    Gdiplus::Color(255, 158, 158, 158)
};
constexpr int colorCount = int(std::size(g_chartColors));

// Get chart color appropriate to index.
// Start with the base color, then blend it with increasing saturation toward gray for entries beyond the colors array.
Gdiplus::Color GetChartColor(int i)
{
    Gdiplus::Color baseColor = g_chartColors[i % colorCount];
    int cycleGroup = (int)(i / colorCount);
    float grayBlendFactor = std::min(0.8f, cycleGroup * 0.5f);

    BYTE r = (BYTE)(std::lerp<float>(baseColor.GetR(), 128, grayBlendFactor));
    BYTE g = (BYTE)(std::lerp<float>(baseColor.GetG(), 128, grayBlendFactor));
    BYTE b = (BYTE)(std::lerp<float>(baseColor.GetB(), 128, grayBlendFactor));

    Gdiplus::Color color(255, r, g, b);
    return color;
};

constexpr int c_pieChartMargin = 50;

void DrawPieChart(HDC hdc, RECT& rect)
{
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    HDC memoryDC = CreateCompatibleDC(hdc);
    HBITMAP memoryBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memoryDC, memoryBitmap);

    Gdiplus::Graphics graphics(memoryDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf); // Fix ridiculous default. Pixel corners are logical.

    COLORREF backgroundColor = GetSysColor(COLOR_BTNFACE);
    graphics.Clear(Gdiplus::Color(255, GetRValue(backgroundColor), GetGValue(backgroundColor), GetBValue(backgroundColor)));

    int diameter = std::min(width, height) - (c_pieChartMargin * 2);
    int centerX = width / 2;
    int centerY = height / 2;
    int x = centerX - diameter / 2;
    int y = centerY - diameter / 2;

    float startAngle = -90.0f; // Start at 12 o'clock (top) instead of 3 o'clock (right).

    // Draw pie slices.
    for (size_t i = 0; i < g_aggregatedEntries.size(); i++)
    {
        float sweepAngle = g_aggregatedEntries[i].percentage * 3.6f;
        Gdiplus::SolidBrush brush(GetChartColor(int(i)));
        graphics.FillPie(&brush, x, y, diameter, diameter, startAngle, sweepAngle);
        startAngle += sweepAngle;
    }

    // If empty, draw a full circle "no data" message.
    if (g_aggregatedEntries.empty())
    {
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 200, 200, 200));
        graphics.FillPie(&brush, x, y, diameter, diameter, 0, 360);

        Gdiplus::Font font(L"Segoe UI", 12);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 0, 0, 0));
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF layoutRect(0, 0, (float)width, (float)height);
        graphics.DrawString(L"No data to display", -1, &font, layoutRect, &format, &textBrush);

        // If there are time entries but no aggregated entries, then all entries were filtered out.
        // So show a warning message to lessen potential confusion, especially since it appears
        // the timer is just frozen.
        if (!g_timeEntries.empty() && (!g_showAwayTime || !g_showSelf))
        {
            Gdiplus::Font font2(L"Segoe UI", 12, Gdiplus::FontStyleBold);
            layoutRect.Offset(0, 28);
            graphics.DrawString(L"(⚠ away time or self time is excluded)", -1, &font2, layoutRect, &format, &textBrush);
        }
    }
    else // Draw the legend.
    {
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone);

        Gdiplus::Font font(L"Segoe UI", 10);
        Gdiplus::SolidBrush blackBrush(Gdiplus::Color::Black);
        Gdiplus::SolidBrush backgroundBrush(Gdiplus::Color(0xFF000000 | backgroundColor));

        int legendX = 10;
        int legendY = 10;
        int legendItemHeight = 20;

        for (size_t i = 0; i < g_aggregatedEntries.size(); i++)
        {
            Gdiplus::SolidBrush legendBrush(GetChartColor(int(i)));

            graphics.FillRectangle(&legendBrush, legendX, int(legendY + i * legendItemHeight), 15, 15);

            std::wstringstream stringStream;
            stringStream << g_aggregatedEntries[i].processName
                         << L" - "
                         << std::fixed
                         << std::setprecision(1)
                         << g_aggregatedEntries[i].percentage
                         << L"%";

            Gdiplus::PointF point((float)(legendX + 20), (float)(legendY + i * legendItemHeight));

            // Draw gray "shadow" for basic blur effect behind text, which helps when the text is drawn over the chart.
            for (int dx = -1; dx <= 1; dx++)
            {
                for (int dy = -1; dy <= 1; dy++)
                {
                    if (dx != 0 || dy != 0)
                    {
                        Gdiplus::PointF shadowPoint(point.X + dx, point.Y + dy);
                        graphics.DrawString(stringStream.str().c_str(), -1, &font, shadowPoint, &backgroundBrush);
                    }
                }
            }

            // Draw black text on top.
            graphics.DrawString(stringStream.str().c_str(), -1, &font, point, &blackBrush);
        }
    }

    BitBlt(hdc, 0, 0, width, height, memoryDC, 0, 0, SRCCOPY);

    SelectObject(memoryDC, oldBitmap);
    DeleteObjectAndNullify(memoryBitmap);
    DeleteDC(memoryDC);
}

void DrawCalendar(HDC hdc, RECT& rect)
{
    const int width  = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    // Create memory DC for flicker-free drawing.
    HDC memoryDC = CreateCompatibleDC(hdc);
    HBITMAP memoryBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memoryDC, memoryBitmap);

    Gdiplus::Graphics graphics(memoryDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    COLORREF backgroundColor = GetSysColor(COLOR_BTNFACE);
    graphics.Clear(Gdiplus::Color(255, GetRValue(backgroundColor), GetGValue(backgroundColor), GetBValue(backgroundColor)));

    // Define margins and label sizes.
    constexpr int leftLabelWidth = 60;    // Space for hour labels (00:00 - 23:00)
    constexpr int topLabelHeight = 30;    // Space for minute labels (0, 5, 10, ..., 55)
    constexpr int margin = 5;

    // Calculate interior region.
    int interiorLeft    = leftLabelWidth + margin;
    int interiorTop     = topLabelHeight + margin;
    int interiorRight   = width - margin;
    int interiorBottom  = height - margin;
    int interiorWidth   = interiorRight - interiorLeft;
    int interiorHeight  = interiorBottom - interiorTop;

    if (interiorWidth <= 0 || interiorHeight <= 0)
        return;

    Gdiplus::Font labelFont(L"Segoe UI", 9);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 0, 0, 0));
    Gdiplus::StringFormat centerFormat;
    centerFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // Draw hour labels on the left (00:00 to 23:00).
    for (int hour = 0; hour < 24; ++hour)
    {
        float y = interiorTop + (hour * interiorHeight / 24.0f);
        WCHAR label[16];
        swprintf_s(label, L"%02d:00", hour);
        Gdiplus::RectF labelRect(0.0f, y, (float)leftLabelWidth, 20.0f);
        graphics.DrawString(label, -1, &labelFont, labelRect, &centerFormat, &textBrush);
    }

    // Draw minute labels on the top (0, 5, 10, ..., 55).
    for (int minute = 0; minute < 60; minute += 5)
    {
        float x = interiorLeft + (minute * interiorWidth / 60.0f);
        WCHAR label[16];
        swprintf_s(label, L"%d", minute);
        Gdiplus::RectF labelRect(x - 15.0f, 0.0f, 30.0f, (float)topLabelHeight);
        graphics.DrawString(label, -1, &labelFont, labelRect, &centerFormat, &textBrush);
    }

    // Use the last selected time to determine which day to display.
    SYSTEMTIME localSelectedTime;
    SystemTimeToTzSpecificLocalTime(nullptr, &g_lastSelectedTime, &localSelectedTime);

    // Build a map from process name to aggregated entry index for color lookup.
    std::map<std::wstring_view, int> processToColorIndex;
    for (size_t i = 0; i < g_aggregatedEntries.size(); ++i)
    {
        processToColorIndex[g_aggregatedEntries[i].processName] = (int)i;
    }

    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf); // Fix ridiculous default. Pixel corners are logical.

    // Draw time entries for the selected day.
    for (const auto& entry : g_timeEntries)
    {
        // Convert UTC times to local time for display.
        SYSTEMTIME localStart, localEnd;
        SystemTimeToTzSpecificLocalTime(nullptr, &entry.startTime, &localStart);
        SystemTimeToTzSpecificLocalTime(nullptr, &entry.endTime, &localEnd);

        // Check if entry starts on the selected day's date.
        if (localStart.wYear != localSelectedTime.wYear || localStart.wMonth != localSelectedTime.wMonth || localStart.wDay != localSelectedTime.wDay)
            continue;

        // Calculate start and end positions within the day (in seconds from midnight).
        int startSeconds = localStart.wHour * 3600 + localStart.wMinute * 60 + localStart.wSecond;
        int endSeconds = localEnd.wHour * 3600 + localEnd.wMinute * 60 + localEnd.wSecond;

        // If end is on a different day, clamp to end of today.
        if (localEnd.wYear != localSelectedTime.wYear || localEnd.wMonth != localSelectedTime.wMonth || localEnd.wDay != localSelectedTime.wDay)
        {
            endSeconds = 86400; // End of day (24 * 3600).
        }

        // Clamp to [0, 86400] (24 hours * 3600 seconds).
        if (endSeconds > 86400)
            endSeconds = 86400;
        if (startSeconds < 0)
            startSeconds = 0;

        if (startSeconds >= endSeconds)
            continue;

        // Determine color from aggregated entries.
        int colorIndex = 0;
        auto it = processToColorIndex.find(entry.processName);
        if (it != processToColorIndex.end())
        {
            colorIndex = it->second;
        }
        Gdiplus::Color color = GetChartColor(colorIndex);
        Gdiplus::SolidBrush brush(color);
        Gdiplus::Pen borderPen(Gdiplus::Color(255, 100, 100, 100), 1.0f);

        // Draw boxes for each hour the entry spans.
        int startHour = startSeconds / 3600;
        int endHour   = (endSeconds - 1) / 3600; // Subtract 1 to handle exact hour boundaries correctly.

        for (int hour = startHour; hour <= endHour; ++hour)
        {
            if (hour >= 24)
                break;

            // Clamp the entry to the current hour.
            int hourStartSeconds = hour * 3600;
            int hourEndSeconds   = (hour + 1) * 3600;
            int clampedStart     = std::max(startSeconds, hourStartSeconds);
            int clampedEnd       = std::min(endSeconds, hourEndSeconds);

            if (clampedStart >= clampedEnd)
                continue;

            // Calculate pixel positions for this hour's segment.
            float rowTop    = interiorTop + (hour * interiorHeight / 24.0f);
            float rowBottom = interiorTop + ((hour + 1) * interiorHeight / 24.0f);

            // Calculate second position within the hour [0, 3600).
            // Convert seconds within hour to minutes for horizontal positioning (0-60 range).
            int secondInHourStart   = clampedStart - hourStartSeconds;
            int secondInHourEnd     = clampedEnd - hourStartSeconds;
            float minuteInHourStart = secondInHourStart / 60.0f;
            float minuteInHourEnd   = secondInHourEnd / 60.0f;
            float boxLeft = interiorLeft + (minuteInHourStart / 60.0f) * interiorWidth;
            float boxRight = interiorLeft + (minuteInHourEnd / 60.0f) * interiorWidth;
            float boxWidth = boxRight - boxLeft;
            float boxHeight = rowBottom - rowTop;

            if (boxWidth > 0 && boxHeight > 0)
            {
                graphics.FillRectangle(&brush, boxLeft, rowTop, boxWidth, boxHeight);
            }
        }
    }

    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeDefault);

    // Draw grid lines.
    Gdiplus::Pen gridPen(Gdiplus::Color(128, 220, 220, 220), 1.0f);
    for (int hour = 0; hour <= 24; ++hour)
    {
        float y = interiorTop + (hour * interiorHeight / 24.0f);
        graphics.DrawLine(&gridPen, (float)interiorLeft, y, (float)interiorRight, y);
    }
    for (int minute = 0; minute <= 60; minute += 5)
    {
        float x = interiorLeft + (minute * interiorWidth / 60.0f);
        graphics.DrawLine(&gridPen, x, (float)interiorTop, x, (float)interiorBottom);
    }

    // Blit to screen.
    BitBlt(hdc, 0, 0, width, height, memoryDC, 0, 0, SRCCOPY);

    SelectObject(memoryDC, oldBitmap);
    DeleteObjectAndNullify(memoryBitmap);
    DeleteDC(memoryDC);
}

int MapPieChartCoordinateToTaskIndex(POINT pt, RECT& rect)
{
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    if (g_aggregatedEntries.empty())
        return -1;

    // Calculate pie chart geometry (must match DrawPieChart).
    int diameter = std::min(width, height) - (c_pieChartMargin * 2);
    int centerX  = width / 2;
    int centerY  = height / 2;
    int radius   = diameter / 2;

    // Check if point is within the pie circle.
    int dx = pt.x - centerX;
    int dy = pt.y - centerY;
    float distanceSquared = (float)(dx * dx + dy * dy);
    if (distanceSquared > radius * radius)
        return -1; // Outside the pie.

    // Calculate angle from center, where straight (12 o'clock noon) is zero degrees.
    float angleRadians = atan2f((float)dx, (float)-dy);
    float anglePercent = float(angleRadians * 100 / (std::numbers::pi_v<float> * 2)); // Sadly C++ is missing tau.

    // Normalize to [0, 100) range.
    if (anglePercent < 0)
        anglePercent += 100.0f;

    // Match against pie slices.
    float startAngle = 0.0f;
    for (size_t i = 0, c = g_aggregatedEntries.size(); i < c; i++)
    {
        float sweepAngle = g_aggregatedEntries[i].percentage;
        float endAngle = startAngle + sweepAngle;

        if (anglePercent >= startAngle && anglePercent < endAngle)
            return (int)i;

        startAngle = endAngle;
    }

    return -1;
}

int MapCalendarCoordinateToTimeEntryIndex(POINT pt, RECT& rect)
{
    const int width  = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    // Calculate interior region (must match DrawCalendar).
    constexpr int leftLabelWidth = 60;
    constexpr int topLabelHeight = 30;
    constexpr int margin = 5;

    int interiorLeft   = leftLabelWidth + margin;
    int interiorTop    = topLabelHeight + margin;
    int interiorRight  = width - margin;
    int interiorBottom = height - margin;
    int interiorWidth  = interiorRight - interiorLeft;
    int interiorHeight = interiorBottom - interiorTop;

    // Check if point is within interior region.
    if (pt.x < interiorLeft || pt.x >= interiorRight || pt.y < interiorTop || pt.y >= interiorBottom)
        return -1;

    // Convert to selected day (using g_lastSelectedTime).
    SYSTEMTIME localSelectedTime;
    SystemTimeToTzSpecificLocalTime(nullptr, &g_lastSelectedTime, &localSelectedTime);

    // Check each time entry on the selected day.
    for (size_t i = 0; i < g_timeEntries.size(); ++i)
    {
        const auto& entry = g_timeEntries[i];

        // Convert UTC times to local time.
        SYSTEMTIME localStart, localEnd;
        SystemTimeToTzSpecificLocalTime(nullptr, &entry.startTime, &localStart);
        SystemTimeToTzSpecificLocalTime(nullptr, &entry.endTime, &localEnd);

        // Check if entry starts on the selected day.
        if (localStart.wYear != localSelectedTime.wYear || localStart.wMonth != localSelectedTime.wMonth || localStart.wDay != localSelectedTime.wDay)
            continue;

        // Calculate start and end positions within the day (in seconds from midnight).
        int startSeconds = localStart.wHour * 3600 + localStart.wMinute * 60 + localStart.wSecond;
        int endSeconds = localEnd.wHour * 3600 + localEnd.wMinute * 60 + localEnd.wSecond;

        // If end is on a different day, clamp to end of today.
        if (localEnd.wYear != localSelectedTime.wYear || localEnd.wMonth != localSelectedTime.wMonth || localEnd.wDay != localSelectedTime.wDay)
        {
            endSeconds = 86400;
        }

        if (endSeconds > 86400)
            endSeconds = 86400;
        if (startSeconds < 0)
            startSeconds = 0;

        if (startSeconds >= endSeconds)
            continue;

        // Check each hour segment this entry spans.
        int startHour = startSeconds / 3600;
        int endHour   = (endSeconds - 1) / 3600;

        for (int hour = startHour; hour <= endHour; ++hour)
        {
            if (hour >= 24)
                break;

            // Clamp the entry to the current hour.
            int hourStartSeconds = hour * 3600;
            int hourEndSeconds   = (hour + 1) * 3600;
            int clampedStart     = std::max(startSeconds, hourStartSeconds);
            int clampedEnd       = std::min(endSeconds, hourEndSeconds);

            if (clampedStart >= clampedEnd)
                continue;

            // Calculate pixel bounds for this hour's segment.
            float rowTop    = interiorTop + (hour * interiorHeight / 24.0f);
            float rowBottom = interiorTop + ((hour + 1) * interiorHeight / 24.0f);

            int secondInHourStart   = clampedStart - hourStartSeconds;
            int secondInHourEnd     = clampedEnd - hourStartSeconds;
            float minuteInHourStart = secondInHourStart / 60.0f;
            float minuteInHourEnd   = secondInHourEnd / 60.0f;
            float boxLeft  = interiorLeft + (minuteInHourStart / 60.0f) * interiorWidth;
            float boxRight = interiorLeft + (minuteInHourEnd / 60.0f) * interiorWidth;

            // Check if point is within this box.
            if (pt.x >= boxLeft && pt.x < boxRight && pt.y >= rowTop && pt.y < rowBottom)
                return (int)i;
        }
    }

    return -1;
}

// Compute the total line count by dividing the listbox height by the height of a single item.
int GetVisibleLineCount(HWND hListBox)
{
    RECT rect;
    GetClientRect(hListBox, &rect);
    int clientHeight = rect.bottom - rect.top;
    int itemHeight = (int)SendMessage(hListBox, LB_GETITEMHEIGHT, 0, 0);
    
    // Calculate visible line count
    if (itemHeight > 0 && clientHeight > 0)
    {
        return clientHeight / itemHeight;
    }
    
    return 0;
}

void EnsureListboxIndexVisible(HWND hwndListbox, int itemIndex)
{
    int currentTopLineIndex = (int)SendMessage(hwndListbox, LB_GETTOPINDEX, 0, 0);
    int visibleLineCount = GetVisibleLineCount(hwndListbox);
    int newTopLineIndex = 0;
    if (itemIndex < currentTopLineIndex)
    {
        newTopLineIndex = itemIndex;
    }
    else if (itemIndex >= currentTopLineIndex + visibleLineCount)
    {
        newTopLineIndex = itemIndex - visibleLineCount + 1;
    }
    else
    {
        return;
    }

    SendMessage(hwndListbox, LB_SETTOPINDEX, newTopLineIndex, 0);
}

bool UpdateListboxHoverIndex(HWND hwndListbox, /*inout*/ int& currentHoverIndex, int newHoverIndex)
{
    if (currentHoverIndex == newHoverIndex)
    {
        return false; // No change needed.
    }

    auto invalidateItemRect = [=](int index)
    {
        RECT itemRect;
        if (index != -1 && SendMessage(hwndListbox, LB_GETITEMRECT, index, (LPARAM)&itemRect) != LB_ERR)
        {
            InvalidateRect(hwndListbox, &itemRect, FALSE);
        }
    };

    // Invalidate both the previous and new hovered item's rectangle.
    invalidateItemRect(currentHoverIndex);
    invalidateItemRect(newHoverIndex);
    currentHoverIndex = newHoverIndex;

    return true; // Hover index was updated.
}

bool WriteTextFile(const WCHAR* filename, std::wstring_view content)
{
    HANDLE hFile = CreateFile(filename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    constexpr char8_t utf8Bom[] = {0xEF, 0xBB, 0xBF};
    DWORD bytesWritten;
    WriteFile(hFile, utf8Bom, 3, &bytesWritten, nullptr);

    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, content.data(), (int)content.length(), nullptr, 0, nullptr, nullptr);
    if (utf8Length > 0)
    {
        std::vector<char> utf8Buffer(utf8Length);
        WideCharToMultiByte(CP_UTF8, 0, content.data(), (int)content.length(), utf8Buffer.data(), utf8Length, nullptr, nullptr);

        WriteFile(hFile, utf8Buffer.data(), utf8Length, &bytesWritten, nullptr);
    }

    CloseHandle(hFile);
    return true;
}

bool ReadTextFile(const WCHAR* filename, std::wstring& outContent)
{
    outContent.clear();

    HANDLE hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE)
    {
        CloseHandle(hFile);
        return false;
    }

    std::vector<char8_t> utf8Buffer(fileSize + 1);
    DWORD bytesRead;
    if (!ReadFile(hFile, utf8Buffer.data(), fileSize, &bytesRead, nullptr))
    {
        CloseHandle(hFile);
        return false;
    }
    utf8Buffer[bytesRead] = 0;
    CloseHandle(hFile);

    const char8_t* utf8Start = utf8Buffer.data();
    auto utf8View = std::u8string_view(utf8Buffer.data(), utf8Buffer.size());
    constexpr char8_t utf8Bom[] = {0xEF, 0xBB, 0xBF};
    if (utf8View.starts_with(utf8Bom))
    {
        utf8Start += 3;
    }

    int wideLength = MultiByteToWideChar(CP_UTF8, 0, (const char*)utf8Start, -1, nullptr, 0);
    if (wideLength <= 0)
    {
        return false;
    }

    std::vector<WCHAR> wideBuffer(wideLength);
    MultiByteToWideChar(CP_UTF8, 0, (const char*)utf8Start, -1, wideBuffer.data(), wideLength);
    outContent = wideBuffer.data();

    return true;
}

void UpdateWindowTitle()
{
    std::wstring title = g_defaultWindowTitle;
    title.append(L" - ");
    title.append(g_timeEntriesFilePath.empty() ? L"Untitled" : g_timeEntriesFilePath);
    title.append(g_timeEntriesAreModified ? L" *" : L"");
    SetWindowText(g_hWndMainWindow, title.c_str());
}

void SetTimeEntriesFilePath(std::wstring_view filePath)
{
    g_timeEntriesFilePath = filePath;
    g_timeEntriesAreModified = false; // Updating the file path implies we've just saved or loaded. So reset the modified state.
    UpdateWindowTitle();
}

void SetFileModifiedState(bool isModified)
{
    if (g_timeEntriesAreModified != isModified)
    {
        g_timeEntriesAreModified = isModified;
        UpdateWindowTitle();
    }
}

void NewFile(HWND hWnd)
{
    ClearAllEntries(hWnd, /*isNewFile*/ true);
}

// Use the existing file path, if already saved before.
// Otherwise generate a new "Untitled-###..." filename using the current time.
// Append the filename to the existing path if one is passed in.
//
void GetSuitableTimeEntriesSaveFilePath(bool defaultToDocumentsFolder, /*inout*/ std::wstring& filePath)
{
    if (!g_timeEntriesFilePath.empty())
    {
        // We already have a file path from before, such as from loading a file or a previous save. So just use that.
        filePath = g_timeEntriesFilePath;
    }
    else
    {
        // Generate a name.

        // Use the user's documents folder if requested and there's no explicit path already.
        // Ensure any optionally passed in path has a trailing backslash.
        if (filePath.empty())
        {
            if (defaultToDocumentsFolder)
            {
                filePath = GetUserDocumentsDirectory();
            }
        }
        else if (filePath.back() != L'\\' && filePath.back() != L'/')
        {
            filePath.append(L"\\");
        }

        SYSTEMTIME now;
        GetSystemTime(&now);
        filePath.append(std::format(L"Untitled-{0:04}-{1:02}-{2:02}_{3:02}-{4:02}-{5:02}.wdtg.csv", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond));
    }
}

void LoadFromCSV(HWND hWnd, bool mergeWithExisting)
{
    std::wstring filePath(g_timeEntriesFilePath);
    filePath.resize(MAX_PATH * 4); // Allow more space for long filenames.

    OPENFILENAME openFileName =
    {
        .lStructSize = sizeof(OPENFILENAME),
        .hwndOwner = hWnd,
        .lpstrFilter = L"CSV Files (*.wdtg.csv)\0*.wdtg.csv\0All Files (*.*)\0*.*\0",
        .nFilterIndex = 1,
        .lpstrFile = filePath.data(),
        .nMaxFile = DWORD(filePath.size()),
        .lpstrFileTitle = nullptr,
        .nMaxFileTitle = 0,
        .lpstrInitialDir = nullptr,
        .Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST,
        .lpstrDefExt = L"wdtg.csv",
    };

    if (!GetOpenFileName(&openFileName))
    {
        return;
    }

    LoadFromCSV(hWnd, filePath.c_str(), mergeWithExisting);
}

void LoadFromCSV(HWND hWnd, const wchar_t* filePath, bool mergeWithExisting)
{
    std::wstring fileContent;
    if (!ReadTextFile(filePath, /*out*/ fileContent))
    {
        std::wstring errorMessage = L"Failed to read file:\r\n";
        errorMessage.append(filePath);
        MessageBox(hWnd, errorMessage.c_str(), g_defaultWindowTitle, MB_OK | MB_ICONERROR);
        return;
    }

    if (mergeWithExisting)
    {
        std::vector<TimeEntry> newEntries;
        if (ParseCSVEntries(fileContent, newEntries))
        {
            // Append new entries to existing ones.
            // Could just use std::vector::append_move ... if only there was one 🙃.
            g_timeEntries.reserve(g_timeEntries.size() + newEntries.size());
            for (const auto& entry : newEntries)
            {
                g_timeEntries.push_back(std::move(entry));
            }

            SetFileModifiedState(true);
            SortTimeEntriesByTime();
        }
    }
    else // Loading a new file.
    {
        StopTracking(hWnd); // It's weird to automatically start tracking a file that's just being loaded.
        SetTimeEntriesFilePath(filePath);

        g_timeEntries.clear();
        ParseCSVEntries(fileContent, /*out*/ g_timeEntries);
        SetFileModifiedState(false);
    }

    // Update the last selected time so that if you open the calendar, it will show the last entry's date.
    if (!g_timeEntries.empty())
    {
        g_lastSelectedTime = g_timeEntries.back().endTime;
    }

    RefreshUI();
    SelectLastItemInRawList();
}

// Save the time entries to a CSV file, prompting for a filename if needed.
void SaveToCSV(HWND hWnd, bool alwaysAskForName, bool askForNameIfNeeded)
{
    std::wstring filePath;
    if (alwaysAskForName || (askForNameIfNeeded && g_timeEntriesFilePath.empty()))
    {
        GetSuitableTimeEntriesSaveFilePath(/*defaultToDocumentsFolder*/ false, /*out*/ filePath);
        filePath.resize(MAX_PATH * 4); // Allow more space for long filenames.

        OPENFILENAME openFileName =
        {
            .lStructSize = sizeof(OPENFILENAME),
            .hwndOwner = hWnd,
            .lpstrFilter = L"CSV Files (*.wdtg.csv)\0*.wdtg.csv\0All Files (*.*)\0*.*\0",
            .nFilterIndex = 1,
            .lpstrFile = filePath.data(),
            .nMaxFile = DWORD(filePath.size()),
            .lpstrFileTitle = nullptr,
            .nMaxFileTitle = 0,
            .lpstrInitialDir = nullptr,
            .Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT,
            .lpstrDefExt = L"wdtg.csv",
        };

        if (!GetSaveFileName(&openFileName))
        {
            return;
        }
    }
    else
    {
        // Otherwise the program (or even whole operating system) might be shutting down.
        // So just use a generated file path in the settings folder.
        GetSuitableTimeEntriesSaveFilePath(/*defaultToDocumentsFolder*/ true, /*inout*/ filePath);
    }

    SaveToCSV(hWnd, filePath.c_str());
}

// Save the time entries to a CSV file at the specified path.
void SaveToCSV(HWND hWnd, wchar_t const* filePath)
{
    std::wstringstream csvData;
    csvData << L"Start Time,End Time,Process Name,Window Title\n";

    for (const auto& entry : g_timeEntries)
    {
        std::wstring windowTitle = entry.windowTitle;
        std::replace(windowTitle.begin(), windowTitle.end(), L',', L';');
        std::replace(windowTitle.begin(), windowTitle.end(), L'\n', L' ');
        std::replace(windowTitle.begin(), windowTitle.end(), L'\r', L' ');
        std::replace(windowTitle.begin(), windowTitle.end(), L'"', L'\'');

        csvData << FormatDateTime(entry.startTime, /*adjustToLocalTime*/ false) << L","
                << FormatDateTime(entry.endTime, /*adjustToLocalTime*/ false) << L","
                << entry.processName << L","
                << L"\"" << windowTitle << L"\"\n";

        // Don't save durationSeconds, since it can be recomputed and removes potential inconsistencies if the file is edited manually.
    }

    SetTimeEntriesFilePath(filePath);
    if (!WriteTextFile(filePath, csvData.str()))
    {
        std::wstring errorMessage = L"Failed to write file:\r\n";
        errorMessage.append(filePath);
        MessageBox(hWnd, errorMessage.c_str(), g_defaultWindowTitle, MB_OK | MB_ICONERROR);
    }
    else
    {
        SetFileModifiedState(false);
    }
}

bool ParseCSVEntries(const std::wstring& fileContent, std::vector<TimeEntry>& outEntries)
{
    std::wstringstream fileStream(fileContent);
    std::wstring line;
    std::getline(fileStream, line); // Skip header

    while (std::getline(fileStream, line))
    {
        if (line.empty())
        {
            continue;
        }

        TimeEntry entry = {};

        std::wstringstream ss(line);
        std::wstring token;

        std::getline(ss, token, L',');
        int scannedStart = swscanf_s(
            token.c_str(),
            L"%04hu-%02hu-%02hu %02hu:%02hu:%02hu.%03hu",
            &entry.startTime.wYear, &entry.startTime.wMonth,  &entry.startTime.wDay,
            &entry.startTime.wHour, &entry.startTime.wMinute, &entry.startTime.wSecond,
            &entry.startTime.wMilliseconds
        );

        token.clear(); // Sadly getline doesn't clear the token string on end-of-file, just leaving previous litter.
        std::getline(ss, token, L',');
        int scannedEnd = swscanf_s(
            token.c_str(),
            L"%04hu-%02hu-%02hu %02hu:%02hu:%02hu.%03hu",
            &entry.endTime.wYear, &entry.endTime.wMonth,  &entry.endTime.wDay,
            &entry.endTime.wHour, &entry.endTime.wMinute, &entry.endTime.wSecond,
            &entry.endTime.wMilliseconds
        );
        // Expect scannedStart == 7 && scannedEnd == 7.

        entry.durationMilliseconds = CalculateDurationInMilliseconds(entry.startTime, entry.endTime);

        std::getline(ss, entry.processName, L',');

        token.clear();
        std::getline(ss, token, L',');

        std::wstring_view tokenView = token;
        size_t position1 = tokenView.find(L'"');
        if (position1 != std::wstring::npos)
        {
            tokenView = tokenView.substr(position1 + 1);
        }
        size_t position2 = tokenView.find(L'"');
        if (position2 != std::wstring::npos)
        {
            tokenView = tokenView.substr(0, position2);
        }
        entry.windowTitle = tokenView;

        outEntries.push_back(entry);
    }

    return !outEntries.empty();
}

// Get the user's Documents directory path, ensuring it ends with a backslash.
std::wstring GetUserDocumentsDirectory()
{
    WCHAR path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, path)))
    {
        auto filePath = std::wstring(path);
        if (!filePath.empty() && filePath.back() != L'\\')
        {
            filePath += L'\\';
        }
        return filePath;
    }
    return L"";
}

std::wstring GetSettingsFilePath(bool createPath, bool addFileName)
{
    WCHAR path[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path);
    std::wstring settingsPath = path;
    settingsPath += L"\\Pikensoft";
    if (createPath)
    {
        CreateDirectory(settingsPath.c_str(), nullptr);
    }
    settingsPath += L"\\WhereDoesTimeGo";
    if (createPath)
    {
        CreateDirectory(settingsPath.c_str(), nullptr);
    }
    if (addFileName)
    {
        settingsPath += L"\\settings.ini";
    }
    return settingsPath;
}

void SaveSettings()
{
    std::wstring path = GetSettingsFilePath(/*createPathIfNeeded*/ true, /*addFileName*/ true);
    std::wofstream file(path);
    
    file << L"; WhereDoesTimeGo v1.0 2026-06-18\n\n";
    file << L"[View]\n";
    file << L"ShowTimeEntries=" << g_showTimeEntries << L"\n";
    file << L"ShowPieChart=" << g_showPieChart << L"\n";
    file << L"ShowCalendar=" << g_showCalendar << L"\n";
    file << L"ShowTimer=" << g_showTimer << L"\n";
    file << L"ShowAwayTime=" << g_showAwayTime << L"\n";
    file << L"ShowSelf=" << g_showSelf << L"\n";
    
    file << L"\n[File]\n";
    file << L"SaveOnExit=" << g_saveOnExit << L"\n";
    
    file << L"\n[Track]\n";
    file << L"PollingInterval=" << g_pollingInterval << L"\n";
    file << L"TrackChildDialogs=" << g_trackChildDialogs << L"\n";
    
    file.close();
}

bool ParseBool(const std::wstring& value, bool defaultValue = false)
{
    if (value.empty()) return defaultValue;
    return value == L"1" || value == L"true" || value == L"True";
}

int ParseInt(const std::wstring& value, int defaultValue = 0)
{
    if (value.empty()) return defaultValue;
    return _wtoi(value.c_str());
}

void LoadSettings()
{
    std::wstring path = GetSettingsFilePath(/*createPathIfNeeded*/ false, /*addFileName*/ true);
    std::wifstream file(path);
    
    if (!file.is_open())
    {
        return; // Use defaults
    }
    
    std::wstring line, section;
    std::map<std::wstring, std::wstring> settings;
    
    while (std::getline(file, line))
    {
        line.erase(0, line.find_first_not_of(L" \t\r\n"));
        line.erase(line.find_last_not_of(L" \t\r\n") + 1);
        
        if (line.empty() || line[0] == L';')
        {
            continue;
        }
        
        if (line[0] == L'[' && line.back() == L']')
        {
            section = line.substr(1, line.length() - 2);
        }
        else
        {
            size_t position = line.find(L'=');
            if (position != std::wstring::npos)
            {
                std::wstring key = section + L"." + line.substr(0, position);
                std::wstring value = line.substr(position + 1);
                key.erase(key.find_last_not_of(L" \t") + 1);
                value.erase(0, value.find_first_not_of(L" \t"));
                settings[key] = value;
            }
        }
    }
    
    g_showTimeEntries = ParseBool(settings[L"View.ShowTimeEntries"], true);
    g_showPieChart = ParseBool(settings[L"View.ShowPieChart"], true);
    g_showCalendar = ParseBool(settings[L"View.ShowCalendar"], true);
    g_showTimer = ParseBool(settings[L"View.ShowTimer"], true);
    g_showAwayTime = ParseBool(settings[L"View.ShowAwayTime"], true);
    g_showSelf = ParseBool(settings[L"View.ShowSelf"], true);
    g_saveOnExit = ParseBool(settings[L"File.SaveOnExit"], false);
    g_pollingInterval = ParseInt(settings[L"Track.PollingInterval"], DEFAULT_POLLING_INTERVAL);
    g_trackChildDialogs = ParseBool(settings[L"Track.TrackChildDialogs"], true);
}

void SortTimeEntriesByTime()
{
    std::sort(
        g_timeEntries.begin(),
        g_timeEntries.end(),
        [](const TimeEntry& a, const TimeEntry& b)
        {
            FILETIME ftA, ftB;
            SystemTimeToFileTime(&a.startTime, &ftA);
            SystemTimeToFileTime(&b.startTime, &ftB);

            ULARGE_INTEGER uliA, uliB;
            uliA.LowPart = ftA.dwLowDateTime;
            uliA.HighPart = ftA.dwHighDateTime;
            uliB.LowPart = ftB.dwLowDateTime;
            uliB.HighPart = ftB.dwHighDateTime;

            return uliA.QuadPart < uliB.QuadPart;
        }
    );
}

void ClearAllEntries(HWND hWnd, bool isNewFile)
{
    g_timeEntries.clear();
    g_aggregatedEntries.clear();
    GetSystemTime(&g_lastRecordedTime); // Reset selected time to now.
    g_lastSelectedTime = g_lastRecordedTime;

    if (isNewFile)
    {
        SetTimeEntriesFilePath(L"");
    }
    else
    {
        SetFileModifiedState(true);
    }

    RefreshUI();
}

void DeleteSelectedEntries()
{
    int count = (int)SendMessage(g_hwndTimeEntriesList, LB_GETSELCOUNT, 0, 0);
    if (count > 0)
    {
        std::vector<int> selectedIndices(count);
        SendMessage(g_hwndTimeEntriesList, LB_GETSELITEMS, count, (LPARAM)selectedIndices.data());

        std::sort(selectedIndices.begin(), selectedIndices.end(), std::greater<int>());

        for (int index : selectedIndices)
        {
            if (index >= 0 && index < (int)g_timeEntries.size())
            {
                g_timeEntries.erase(g_timeEntries.begin() + index);
            }
        }
        SetFileModifiedState(true);

        RefreshUI();
    }
}

void AddLapEntry(HWND hWnd)
{
    if (g_timeEntries.empty())
    {
        // No existing entry to duplicate. So just add a new entry with the current time.
        RecordActiveWindowDetails(/*tryToMergeWithPreviousEntry*/ false);
    }
    else
    {
        // Duplicate the last entry, with empty time continuing from the previous entry.
        g_timeEntries.reserve(g_timeEntries.size() + 1);
        g_timeEntries.push_back(g_timeEntries.back());
        g_timeEntries.back().SetStartTimeToEndTime();
        g_lastRecordedTime = g_timeEntries.back().endTime;
    }

    SetFileModifiedState(true);
    RefreshUI();
}

void InsertEntry(HWND hWnd)
{
    // Clear the editing entry, and set it to the current time before opening the dialog.
    g_editingEntry = {};
    g_editingEntry.ResetStartAndEndTimeToNow();

    size_t selectedIndex = size_t(SendMessage(g_hwndTimeEntriesList, LB_GETCURSEL, 0, 0));

    if (DialogBox(g_instanceHandle, MAKEINTRESOURCE(IDD_EDITENTRY), hWnd, &EditTimeEntryDialog) == IDOK)
    {
        size_t insertionIndex = std::min(size_t(selectedIndex), g_timeEntries.size());
        g_timeEntries.insert(g_timeEntries.begin() + insertionIndex, g_editingEntry);

        RefreshUI();
        SelectLastItemInRawList();
        SetFileModifiedState(true);
    }
}

void EditSelectedEntry(HWND hWnd)
{
    size_t selectedIndex = size_t(SendMessage(g_hwndTimeEntriesList, LB_GETCURSEL, 0, 0));

    if (selectedIndex < g_timeEntries.size())
    {
        g_editingEntry = g_timeEntries[selectedIndex];
        if (DialogBox(g_instanceHandle, MAKEINTRESOURCE(IDD_EDITENTRY), hWnd, &EditTimeEntryDialog) == IDOK)
        {
            if (selectedIndex < g_timeEntries.size()) // Retest again in case something changed while the dialog was open, like deletion of the entry being edited.
            {
                g_timeEntries[selectedIndex] = std::move(g_editingEntry);
                SetFileModifiedState(true);
                RefreshUI();
            }
        }
    }
}

void UpdateLastSelectedTimeToSelectedEntry(size_t selectedIndex)
{
    if (selectedIndex < g_timeEntries.size())
    {
        // Must convert to local time before time comparison.
        const TimeEntry& timeEntry = g_timeEntries[selectedIndex];
        SYSTEMTIME localStartTime;
        SYSTEMTIME localSelectedTime;
        SystemTimeToTzSpecificLocalTime(nullptr, &timeEntry.startTime, &localStartTime);
        SystemTimeToTzSpecificLocalTime(nullptr, &g_lastSelectedTime, &localSelectedTime);

        bool dayChanged = localSelectedTime.wYear  != localStartTime.wYear  ||
                          localSelectedTime.wMonth != localStartTime.wMonth ||
                          localSelectedTime.wDay   != localStartTime.wDay;

        g_lastSelectedTime = timeEntry.startTime;

        // If the day changed, invalidate the calendar to redraw.
        if (dayChanged && g_showCalendar)
        {
            InvalidateRect(g_hwndCalendar, nullptr, TRUE);
        }
    }
}
 
void TimeEntry::ResetStartAndEndTimeToNow()
{
    GetSystemTime(&startTime);
    endTime = startTime;
    durationMilliseconds = 0;
}

void TimeEntry::SetStartTimeToEndTime()
{
    startTime = endTime;
    durationMilliseconds = 0;
}

void TimeEntry::SetEndTimeToNow()
{
    GetSystemTime(&endTime);
    durationMilliseconds = CalculateDurationInMilliseconds(startTime, endTime);
}

void TimeEntry::RecomputeDuration()
{
    durationMilliseconds = CalculateDurationInMilliseconds(startTime, endTime);
}

UINT64 CalculateDurationInMilliseconds(const SYSTEMTIME& timeStart, const SYSTEMTIME& timeEnd)
{
    FILETIME fileTimeStart, fileTimeEnd;
    SystemTimeToFileTime(&timeStart, &fileTimeStart);
    SystemTimeToFileTime(&timeEnd, &fileTimeEnd);

    ULARGE_INTEGER uliStart, uliEnd;
    uliStart.LowPart = fileTimeStart.dwLowDateTime;
    uliStart.HighPart = fileTimeStart.dwHighDateTime;
    uliEnd.LowPart = fileTimeEnd.dwLowDateTime;
    uliEnd.HighPart = fileTimeEnd.dwHighDateTime;

    if (uliEnd.QuadPart < uliStart.QuadPart)
    {
        return 0;
    }

    ULONGLONG diff = uliEnd.QuadPart - uliStart.QuadPart;
    return diff / 10000; // Convert from 100-nanosecond intervals to milliseconds.
}

bool IsSystemTimeAfterOrEqual(const SYSTEMTIME& time1, const SYSTEMTIME& time2)
{
    if (time1.wYear   != time2.wYear)   return time1.wYear >= time2.wYear;
    if (time1.wMonth  != time2.wMonth)  return time1.wMonth >= time2.wMonth;
    if (time1.wDay    != time2.wDay)    return time1.wDay >= time2.wDay;
    if (time1.wHour   != time2.wHour)   return time1.wHour >= time2.wHour;
    if (time1.wMinute != time2.wMinute) return time1.wMinute >= time2.wMinute;
    if (time1.wSecond != time2.wSecond) return time1.wSecond >= time2.wSecond;
    return time1.wMilliseconds >= time2.wMilliseconds;
}

// Use YYYY-MM-DD HH:MM:SS.mmm
// Note we use a space to separate date and time rather than ISO's "T", which is easier to read visually
// and works better with Excel when importing CSV files.
std::wstring FormatDateTime(const SYSTEMTIME& time, bool adjustToLocalTime)
{
    // Convert UTC time to local time for display.
    SYSTEMTIME displayTime;
    if (adjustToLocalTime)
    {
        SystemTimeToTzSpecificLocalTime(nullptr, &time, &displayTime);
    }
    else
    {
        displayTime = time;
    }

    WCHAR buffer[64];
    swprintf_s(
        buffer,
        L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
        displayTime.wYear, displayTime.wMonth, displayTime.wDay,
        displayTime.wHour, displayTime.wMinute, displayTime.wSecond, displayTime.wMilliseconds
    );
    return buffer;
}

std::wstring FormatDuration(UINT64 totalMilliseconds)
{
    UINT64 totalSeconds = totalMilliseconds / 1000;
    UINT64 hours   = totalSeconds / 3600;
    UINT64 minutes = (totalSeconds % 3600) / 60;
    UINT64 seconds = totalSeconds % 60;

    WCHAR buffer[64];
    swprintf_s(buffer, L"%02lluh %02llum %02llus", hours, minutes, seconds);
    return buffer;
}

void DrawListboxItem(std::wstring_view text, HDC hdc, const RECT& rcItem, UINT itemState)
{
    int backgroundColorIndex = (itemState & ODS_HOTLIGHT) ? COLOR_MENUHILIGHT :
                     (itemState & ODS_SELECTED) ? COLOR_HIGHLIGHT : COLOR_WINDOW;
    COLORREF textColor = GetSysColor((itemState & (ODS_SELECTED | ODS_HOTLIGHT)) ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);

    HBRUSH backgroundBrush = GetSysColorBrush(backgroundColorIndex);
    FillRect(hdc, &rcItem, backgroundBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    RECT textRect = rcItem;
    textRect.left += 4;

    HFONT oldFont = (HFONT)SelectObject(hdc, g_hLabelFont);
    DrawText(hdc, text.data(), int(text.size()), &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    SelectObject(hdc, oldFont);

    if (itemState & ODS_FOCUS)
    {
        DrawFocusRect(hdc, &rcItem);
    }
}

LRESULT CALLBACK WindowProcedure(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_NCCREATE:
        g_hWndMainWindow = hWnd;
        return TRUE;

    case WM_CREATE:
        SetTimeEntriesFilePath(L"");
        // Sigh, the inconsistency between CREATE and NCCREATE is confusing.
        // In WM_NCCREATE, TRUE means continue, FALSE means abort.
        // In WM_CREATE, 0 means continue, -1 means abort.
        return 0;

    case WM_SIZE:
        ResizeControls(hWnd);
        // If window was restored from minimized and we have pending UI updates, refresh now.
        if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
        {
            UpdateTrackingTimer(); // Restore the timer to normal frequency.
            if (g_needsUIRefresh)
            {
                RefreshUI();
            }
        }
        else if (wParam == SIZE_MINIMIZED)
        {
            UpdateTrackingTimer(); // Throttle the timer when minimized to reduce CPU usage.
        }
        break;

    case WM_TIMER:
        if (wParam == TIMER_TRACK_ID && g_isTrackingTime)
        {
            RecordActiveWindowDetails(/*tryToMergeWithPreviousEntry*/ true);
            RefreshUI();
        }
        break;

    case WM_POWERBROADCAST:
        switch (wParam)
        {
        case PBT_APMSUSPEND:
        case PBT_APMSTANDBY:
            PauseTrackingBecauseAway(hWnd);
            break;
        case PBT_APMRESUMESUSPEND:
        case PBT_APMRESUMESTANDBY:
        case PBT_APMRESUMECRITICAL:
        case PBT_APMRESUMEAUTOMATIC:
            ResumeTrackingBecauseBack(hWnd);
            break;
        }
        break;

    case WM_WTSSESSION_CHANGE:
        // Message enabled by WTSRegisterSessionNotification.
        switch (wParam)
        {
        case WTS_SESSION_LOCK:
            PauseTrackingBecauseAway(hWnd);
            break;
        case WTS_SESSION_UNLOCK:
            ResumeTrackingBecauseBack(hWnd);
            break;
        }
        break;

    case WM_INITMENUPOPUP:
    {
        HMENU hMenu = (HMENU)wParam;
        // Update checkmarks for File menu items.
        CheckMenuItem(hMenu, IDM_SAVE_ON_EXIT, MF_BYCOMMAND | (g_saveOnExit ? MF_CHECKED : MF_UNCHECKED));
        // Update checkmarks for View menu items.
        CheckMenuItem(hMenu, IDM_SHOW_TIME_ENTRIES, MF_BYCOMMAND | (g_showTimeEntries ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_SHOW_TIMER, MF_BYCOMMAND | (g_showTimer ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_SHOW_PIE_CHART, MF_BYCOMMAND | (g_showPieChart ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_SHOW_CALENDAR, MF_BYCOMMAND | (g_showCalendar ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_SHOW_AWAY_TIME, MF_BYCOMMAND | (g_showAwayTime ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_SHOW_SELF, MF_BYCOMMAND | (g_showSelf ? MF_CHECKED : MF_UNCHECKED));
        // Update checkmarks for Track > Polling Frequency submenu.
        CheckMenuItem(hMenu, IDM_POLLING_NEVER, MF_BYCOMMAND | (g_pollingInterval == 0 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_POLLING_1SEC, MF_BYCOMMAND | (g_pollingInterval == 1000 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_POLLING_10SEC, MF_BYCOMMAND | (g_pollingInterval == 10000 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_POLLING_60SEC, MF_BYCOMMAND | (g_pollingInterval == 60000 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_POLLING_10MIN, MF_BYCOMMAND | (g_pollingInterval == 600000 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_TRACK_CHILD_DIALOGS, MF_BYCOMMAND | (g_trackChildDialogs ? MF_CHECKED : MF_UNCHECKED));
    }
    break;

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        if (wmEvent == LBN_DBLCLK && wmId == IDC_RAW_TIME_ENTRY_LIST)
        {
            EditSelectedEntry(hWnd);
            break;
        }

        if (wmEvent == LBN_SELCHANGE && wmId == IDC_RAW_TIME_ENTRY_LIST)
        {
            // Update g_lastSelectedTime when a time entry is selected.
            size_t selectedIndex = size_t(SendMessage(g_hwndTimeEntriesList, LB_GETCURSEL, 0, 0));
            UpdateLastSelectedTimeToSelectedEntry(selectedIndex);
            break;
        }

        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(g_instanceHandle, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, &AboutDialog);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDM_NEW:
            NewFile(hWnd);
            break;
        case IDM_OPEN:
            LoadFromCSV(hWnd, /*mergeWithExisting*/ false);
            break;
        case IDM_MERGE:
            LoadFromCSV(hWnd, /*mergeWithExisting*/ true);
            break;
        case IDM_SAVE:
            SaveToCSV(hWnd, /*alwaysAskForName*/ false, /*askForNameIfNeeded*/ true);
            break;
        case IDM_SAVEAS:
            SaveToCSV(hWnd, /*alwaysAskForName*/ true, /*askForNameIfNeeded*/ true);
            break;
        case IDM_INSERT:
            InsertEntry(hWnd);
            break;
        case IDM_EDIT:
            EditSelectedEntry(hWnd);
            break;
        case IDM_DELETE:
            DeleteSelectedEntries();
            break;
        case IDM_CLEAR_ENTRIES:
            ClearAllEntries(hWnd, /*isNewFile*/ false);
            break;
        case IDM_SAVE_ON_EXIT:
            g_saveOnExit = !g_saveOnExit;
            break;
        case IDM_START_TIME_TRACKING:
            StartTracking(hWnd);
            break;
        case IDM_ADD_LAP_ENTRY:
            AddLapEntry(hWnd);
            break;
        case IDM_STOP_TIME_TRACKING:
            StopTracking(hWnd);
            break;
        case IDM_SHOW_TIME_ENTRIES:
            g_showTimeEntries = !g_showTimeEntries;
            SendMessage(g_hWndToolbar, TB_CHECKBUTTON, IDM_SHOW_TIME_ENTRIES, MAKELONG(g_showTimeEntries, 0));
            ResizeControls(hWnd);
            break;
        case IDM_SHOW_PIE_CHART:
            g_showPieChart = !g_showPieChart;
            SendMessage(g_hWndToolbar, TB_CHECKBUTTON, IDM_SHOW_PIE_CHART, MAKELONG(g_showPieChart, 0));
            ResizeControls(hWnd);
            break;
        case IDM_SHOW_CALENDAR:
            g_showCalendar = !g_showCalendar;
            SendMessage(g_hWndToolbar, TB_CHECKBUTTON, IDM_SHOW_CALENDAR, MAKELONG(g_showCalendar, 0));
            ResizeControls(hWnd);
            break;
        case IDM_SHOW_TIMER:
            g_showTimer = !g_showTimer;
            SendMessage(g_hWndToolbar, TB_CHECKBUTTON, IDM_SHOW_TIMER, MAKELONG(g_showTimer, 0));
            ResizeControls(hWnd);
            break;
        case IDM_SHOW_AWAY_TIME:
            g_showAwayTime = !g_showAwayTime;
            RefreshUI();
            break;
        case IDM_SHOW_SELF:
            g_showSelf = !g_showSelf;
            RefreshUI();
            break;
        case IDM_POLLING_NEVER:
            g_pollingInterval = 0;
            UpdateTrackingTimer();
            break;
        case IDM_POLLING_1SEC:
            g_pollingInterval = 1000;
            UpdateTrackingTimer();
            break;
        case IDM_POLLING_10SEC:
            g_pollingInterval = 10000;
            UpdateTrackingTimer();
            break;
        case IDM_POLLING_60SEC:
            g_pollingInterval = 60000;
            UpdateTrackingTimer();
            break;
        case IDM_POLLING_10MIN:
            g_pollingInterval = 600000;
            UpdateTrackingTimer();
            break;
        case IDM_TRACK_CHILD_DIALOGS:
            g_trackChildDialogs = !g_trackChildDialogs;
            break;
        case IDM_LOGS_FOLDER:
            {
                // Open the folder in Windows Explorer.
                SaveSettings();

                // The settings path should have already been created by SaveSettings().
                std::wstring settingsPath = GetSettingsFilePath(/*createPathIfNeeded*/ false, /*addFileName*/ false);
                ShellExecute(nullptr, L"explore", settingsPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            break;
        case IDM_MINIMIZE_TO_TRAY:
            MinimizeToSystemTray(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDOWN || lParam == WM_LBUTTONDBLCLK)
        {
            RestoreFromSystemTray(hWnd);
        }
        break;

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    {
        // Track mouse over pie chart and calendar for hover effects.
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND hwndUnderMouse = ChildWindowFromPoint(hWnd, point);
        HWND listboxHwnd = nullptr;

        int newHoveredIndex = -1;
        bool changeHappened = false;

        if (hwndUnderMouse == g_hwndPieChart || hwndUnderMouse == g_hwndCalendar)
        {
            // Convert to client coordinates of the pie chart.
            RECT controlRect;
            GetClientRect(hwndUnderMouse, &controlRect);
            MapWindowPoints(hWnd, hwndUnderMouse, &point, 1);

            if (hwndUnderMouse == g_hwndPieChart)
            {
                newHoveredIndex = MapPieChartCoordinateToTaskIndex(point, controlRect);
                listboxHwnd = g_hwndAggregatedTimeEntriesList;
                UpdateListboxHoverIndex(listboxHwnd, /*inout*/ g_hoveredAggregateEntryIndex, newHoveredIndex);
            }
            else // hwndUnderMouse == g_hwndCalendar
            {
                listboxHwnd = g_hwndTimeEntriesList;
                newHoveredIndex = MapCalendarCoordinateToTimeEntryIndex(point, controlRect);
                UpdateListboxHoverIndex(listboxHwnd, /*inout*/ g_hoveredTimeEntryIndex, newHoveredIndex);
            }

            // On click or drag, ensure the item is visible.
            if ((wParam & MK_LBUTTON) && newHoveredIndex != -1)
            {
                EnsureListboxIndexVisible(listboxHwnd, newHoveredIndex);
            }
        }

        // Enable mouse leave tracking.
        TRACKMOUSEEVENT tme =
        {
            .cbSize = sizeof(tme),
            .dwFlags = TME_LEAVE,
            .hwndTrack = hWnd,
        };
        TrackMouseEvent(&tme);
    }
    break;

    case WM_MOUSELEAVE:
    {
        // Clear hover states when mouse leaves the window.
        UpdateListboxHoverIndex(g_hwndAggregatedTimeEntriesList, g_hoveredAggregateEntryIndex, -1);
        UpdateListboxHoverIndex(g_hwndTimeEntriesList, g_hoveredTimeEntryIndex, -1);
    }
    break;

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT drawItemStruct = (LPDRAWITEMSTRUCT)lParam;

        if (drawItemStruct->CtlID == IDC_RAW_TIME_ENTRY_LIST)
        {
            size_t index = (size_t)drawItemStruct->itemID;
            if (index >= g_timeEntries.size()) // This also covers the case of itemID == -1, since size_t(-1) is a very large number.
                break;

            const TimeEntry& entry = g_timeEntries[index];

            // Format: "YYYY-MM-DD HH:MM:SS.mmm (duration) - ProcessName - WindowTitle"
            std::wstring text = FormatDateTime(entry.startTime, /*adjustToLocalTime*/ true) +
                                L" (" + FormatDuration(entry.durationMilliseconds) + L") - " +
                                entry.processName + L" - " +
                                entry.windowTitle;
            if (text.length() > 255)
            {
                text = text.substr(0, 252) + L"...";
            }

            auto itemState = drawItemStruct->itemState | (g_hoveredTimeEntryIndex == (int)index ? ODS_HOTLIGHT : 0);
            DrawListboxItem(text, drawItemStruct->hDC, drawItemStruct->rcItem, itemState);
        }
        else if (drawItemStruct->CtlID == IDC_AGGREGATED_TIME_ENTRY_LIST)
        {
            size_t index = (size_t)drawItemStruct->itemID;
            if (index >= g_aggregatedEntries.size()) // This also covers the case of itemID == -1, since size_t(-1) is a very large number.
                break;

            const AggregatedTimeEntry& entry = g_aggregatedEntries[index];

            // Format: "duration (percentage%) - ProcessName"
            WCHAR textBuffer[256];
            swprintf_s(
                textBuffer,
                L"%s (%04.1f%%) - %s",
                FormatDuration(entry.totalMilliseconds).c_str(),
                entry.percentage,
                entry.processName.c_str()
            );

            auto itemState = drawItemStruct->itemState | (g_hoveredAggregateEntryIndex == (int)index ? ODS_HOTLIGHT : 0);;
            DrawListboxItem(textBuffer, drawItemStruct->hDC, drawItemStruct->rcItem, itemState);
        }
        else if (drawItemStruct->CtlID == IDC_PIECHART)
        {
            DrawPieChart(drawItemStruct->hDC, drawItemStruct->rcItem);
        }
        else if (drawItemStruct->CtlID == IDC_CALENDAR)
        {
            DrawCalendar(drawItemStruct->hDC, drawItemStruct->rcItem);
        }
    }
    break;

    // The default height is just slightly tight, which might have worked well for 1990's VGA era monitors.
    case WM_MEASUREITEM:
    {
        LPMEASUREITEMSTRUCT measureItemStruct = (LPMEASUREITEMSTRUCT)lParam;
        if (measureItemStruct->CtlID == IDC_RAW_TIME_ENTRY_LIST || measureItemStruct->CtlID == IDC_AGGREGATED_TIME_ENTRY_LIST)
        {
            measureItemStruct->itemHeight += 4; // Pad single line height.
        }
    }
    break;

    case WM_DESTROY:
        StopTracking(hWnd, /*noUiUpdates*/ true);

        // Save on exit if option is enabled and there are any unsaved entries.
        if (g_saveOnExit && !g_timeEntries.empty() && g_timeEntriesAreModified)
        {
            SaveToCSV(hWnd, /*alwaysAskForName*/ false, /*askForNameIfNeeded*/ false);
        }

        DeleteResourceAndNullify(g_hWinEventHook, &UnhookWinEvent);
        WTSUnRegisterSessionNotification(hWnd);
        RemoveSystemTrayIcon(hWnd);

        DeleteObjectAndNullify(g_hLabelFont);
        DeleteObjectAndNullify(g_hTimerFont);
        DeleteObjectAndNullify(g_hMegaTimerFont);
        PostQuitMessage(0);
        break;

    // In WindowProcedure, replace/add WM_ACTIVATE case (after WM_WTSSESSION_CHANGE around line 1412):
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            // Window is being deactivated - save the current focus
            HWND hwndFocus = GetFocus();
            // Only save if it's a child of our window
            if (hwndFocus && (hwndFocus == hWnd || IsChild(hWnd, hwndFocus)))
            {
                g_hwndLastFocus = hwndFocus;
            }
        }
        else
        {
            // Window is being activated - restore focus
            if (g_hwndLastFocus && IsWindow(g_hwndLastFocus) && IsWindowVisible(g_hwndLastFocus) && IsWindowEnabled(g_hwndLastFocus))
            {
                SetFocus(g_hwndLastFocus);
            }
            else
            {
                // Find first focusable control in tab order
                HWND hwndFirstControl = GetNextDlgTabItem(hWnd, NULL, FALSE);
                if (hwndFirstControl)
                {
                    SetFocus(hwndFirstControl);
                }
            }
        }
        return 0;

    case WM_SETFOCUS:
        // Main window received focus - delegate to appropriate child.
        if (g_hwndLastFocus && IsWindow(g_hwndLastFocus) && IsWindowVisible(g_hwndLastFocus) && IsWindowEnabled(g_hwndLastFocus))
        {
            SetFocus(g_hwndLastFocus);
        }
        else
        {
            // Find first focusable control in tab order
            HWND hwndFirstControl = GetNextDlgTabItem(hWnd, NULL, FALSE);
            if (hwndFirstControl)
            {
                SetFocus(hwndFirstControl);
            }
        }
        return 0;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void CALLBACK WinEventHookProcedure(
    HWINEVENTHOOK hWinEventHook,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD dwEventThread,
    DWORD dwmsEventTime
    )
{
    if (event == EVENT_SYSTEM_FOREGROUND && g_isTrackingTime)
    {
        PostMessage(g_hWndMainWindow, WM_TIMER, TIMER_TRACK_ID, 0);

        // Alternately we could have called RecordActiveWindowDetails() and RefreshUI() directly here,
        // but posting a message minimizes the amount of time spent inside the event hook,
        // and it avoids any potential weird edge cases or reentrancy issues that could arise
        // from messing with the UI.
    }
}

INT_PTR CALLBACK AboutDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK EditTimeEntryDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
    {
        TimeEntry& entry = g_editingEntry;

        // If entry is empty (inserting new entry), change dialog title.
        if (entry.windowTitle.empty() && entry.processName.empty())
        {
            SetWindowText(hDlg, L"Insert Time Entry");
        }

        SetDlgItemText(hDlg, IDC_EDIT_TITLE, entry.windowTitle.c_str());
        SetDlgItemText(hDlg, IDC_EDIT_PROCESS, entry.processName.c_str());

        HWND hDateTimeStart = GetDlgItem(hDlg, IDC_DATETIME_START);
        HWND hDateTimeEnd = GetDlgItem(hDlg, IDC_DATETIME_END);

        // Sadly the date time picker control does not support milliseconds. So we will just display seconds.
        DateTime_SetFormat(hDateTimeStart, L"yyyy'-'MM'-'dd HH':'mm':'ss");
        DateTime_SetFormat(hDateTimeEnd, L"yyyy'-'MM'-'dd HH':'mm':'ss");

        SYSTEMTIME localStartTime, localEndTime;
        SystemTimeToTzSpecificLocalTime(nullptr, &entry.startTime, &localStartTime);
        SystemTimeToTzSpecificLocalTime(nullptr, &entry.endTime, &localEndTime);
        DateTime_SetSystemtime(hDateTimeStart, GDT_VALID, &localStartTime);
        DateTime_SetSystemtime(hDateTimeEnd, GDT_VALID, &localEndTime);

        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            TimeEntry& entry = g_editingEntry;

            WCHAR buffer[256];
            GetDlgItemText(hDlg, IDC_EDIT_TITLE, buffer, 256);
            entry.windowTitle = buffer;

            GetDlgItemText(hDlg, IDC_EDIT_PROCESS, buffer, 256);
            entry.processName = buffer;

            HWND hDateTimeStart = GetDlgItem(hDlg, IDC_DATETIME_START);
            HWND hDateTimeEnd = GetDlgItem(hDlg, IDC_DATETIME_END);

            SYSTEMTIME localStartTime, localEndTime;
            DateTime_GetSystemtime(hDateTimeStart, &localStartTime);
            DateTime_GetSystemtime(hDateTimeEnd, &localEndTime);
            if (!IsSystemTimeAfterOrEqual(localEndTime, localStartTime))
            {
                localEndTime = localStartTime;
                DateTime_SetSystemtime(hDateTimeEnd, GDT_VALID, &localEndTime);
                MessageBox(hDlg, L"End time cannot be before start time. End time has been set to match start time.", L"Invalid Time Range", MB_OK | MB_ICONWARNING);
                return (INT_PTR)TRUE;
            }
               
            TzSpecificLocalTimeToSystemTime(nullptr, &localStartTime, &entry.startTime);
            TzSpecificLocalTimeToSystemTime(nullptr, &localEndTime, &entry.endTime);
            entry.durationMilliseconds = CalculateDurationInMilliseconds(entry.startTime, entry.endTime);

            EndDialog(hDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void MinimizeToSystemTray(HWND hWnd)
{
    if (g_isTrayIconActive)
    {
        return;
    }

    NOTIFYICONDATA notifyIconData =
    {
        .cbSize = sizeof(NOTIFYICONDATA),
        .hWnd = hWnd,
        .uID = TRAY_ICON_ID,
        .uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP,
        .uCallbackMessage = WM_TRAYICON,
        .hIcon = LoadIcon(g_instanceHandle, MAKEINTRESOURCE(IDI_WHEREDOESTIMEGO)),
    };
    wcscpy_s(notifyIconData.szTip, L"WhereDoesTimeGo - Click to restore");

    if (!Shell_NotifyIcon(NIM_ADD, &notifyIconData))
    {
        return; // Failed for some reason - do NOT hide main window.
    }

    g_isTrayIconActive = true;
    ShowWindow(hWnd, SW_HIDE);
    UpdateTrackingTimer(); // Restore the timer to normal frequency.
}

void RemoveSystemTrayIcon(HWND hWnd)
{
    if (!g_isTrayIconActive)
    {
        return;
    }

    NOTIFYICONDATA notifyIconData =
    {
        .cbSize = sizeof(NOTIFYICONDATA),
        .hWnd = hWnd,
        .uID = TRAY_ICON_ID,
    };

    Shell_NotifyIcon(NIM_DELETE, &notifyIconData);
    g_isTrayIconActive = false;
}

void RestoreFromSystemTray(HWND hWnd)
{
    if (!g_isTrayIconActive)
    {
        return;
    }

    RemoveSystemTrayIcon(hWnd);
    ShowWindow(hWnd, SW_RESTORE);
    UpdateTrackingTimer(); // Restore the timer to normal frequency.
}
