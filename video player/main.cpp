#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <mfapi.h>
#include <mfplay.h>
#include <mferror.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <gdiplus.h>

#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Custom Messages
#define WM_APP_MEDIA_SET      (WM_APP + 1)
#define WM_APP_PLAYBACK_ENDED (WM_APP + 2)
#define WM_APP_MEDIA_ERROR    (WM_APP + 3)

// Control IDs
#define IDC_BTN_OPEN        101
#define IDC_BTN_PLAYPAUSE   102
#define IDC_BTN_STOP        103
#define IDC_SEEKBAR         104
#define IDC_VOLUMESLIDER    105
#define IDC_BTN_MUTE        106
#define IDC_BTN_FULLSCREEN  107
#define IDC_STATIC_TIME     108
#define IDC_STATIC_TITLE    109

#define IDT_PLAY_TIMER      1

// Forward Declarations
class MediaPlayerCallback;
LRESULT CALLBACK MainWindowProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ViewportWindowProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK VideoWindowProc(HWND, UINT, WPARAM, LPARAM);
void OpenFileAny(const std::wstring& filePath);
bool OpenImageFile(const std::wstring& filePath);
void OpenMediaFile(const std::wstring& filePath);

// Global Variables
HINSTANCE g_hInstance = nullptr;
HWND g_hwndMain = nullptr;
HWND g_hwndViewport = nullptr; // Background container for audio/image/idle display
HWND g_hwndVideo = nullptr;    // Dedicated video renderer window
HWND g_hwndBtnOpen = nullptr;
HWND g_hwndBtnPlay = nullptr;
HWND g_hwndBtnStop = nullptr;
HWND g_hwndSeekBar = nullptr;
HWND g_hwndVolSlider = nullptr;
HWND g_hwndBtnMute = nullptr;
HWND g_hwndBtnFullscreen = nullptr;
HWND g_hwndStaticTime = nullptr;
HWND g_hwndStaticTitle = nullptr;

HFONT g_hFontUI = nullptr;
HFONT g_hFontBig = nullptr;
HBRUSH g_hBrushBg = nullptr;
HBRUSH g_hBrushBar = nullptr;

IMFPMediaPlayer* g_pPlayer = nullptr;
MediaPlayerCallback* g_pCallback = nullptr;

ULONG_PTR g_gdiplusToken = 0;
Gdiplus::Image* g_pImage = nullptr;
bool g_bHasImage = false;

bool g_bHasMedia = false;
bool g_bHasVideo = false;
bool g_bIsPlaying = false;
bool g_bIsSeeking = false;
bool g_bIsMuted = false;
bool g_bIsFullscreen = false;
WINDOWPLACEMENT g_prevPlacement = { sizeof(WINDOWPLACEMENT) };

MFTIME g_duration = 0; // 100-nanosecond units
std::wstring g_currentFile = L"";
std::wstring g_currentFileName = L"";
float g_currentVolume = 0.8f; // 0.0 to 1.0

// Check if a file is an audio-only format
bool IsAudioFile(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return (ext == L"mp3" || ext == L"wav" || ext == L"m4a" || ext == L"flac" ||
            ext == L"aac" || ext == L"wma" || ext == L"ogg" || ext == L"oga" ||
            ext == L"opus" || ext == L"aiff" || ext == L"aif" || ext == L"alac" ||
            ext == L"mid" || ext == L"midi");
}

// Check if a file is an image format
bool IsImageFile(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return (ext == L"png" || ext == L"jpg" || ext == L"jpeg" || ext == L"bmp" ||
            ext == L"gif" || ext == L"webp" || ext == L"ico" || ext == L"tiff" ||
            ext == L"tif" || ext == L"jfif");
}

// Helper to format time (hh:mm:ss or mm:ss)
std::wstring FormatTime(LONGLONG hns) {
    LONGLONG totalSeconds = hns / 10000000;
    LONGLONG hours = totalSeconds / 3600;
    LONGLONG minutes = (totalSeconds % 3600) / 60;
    LONGLONG seconds = totalSeconds % 60;

    std::wostringstream oss;
    if (hours > 0) {
        oss << hours << L":"
            << std::setw(2) << std::setfill(L'0') << minutes << L":"
            << std::setw(2) << std::setfill(L'0') << seconds;
    } else {
        oss << std::setw(2) << std::setfill(L'0') << minutes << L":"
            << std::setw(2) << std::setfill(L'0') << seconds;
    }
    return oss.str();
}

// Media Foundation Callback Implementation
class MediaPlayerCallback : public IMFPMediaPlayerCallback
{
    long m_refCount;
    HWND m_hwnd;
    IMFPMediaPlayer* m_pPlayer;

public:
    MediaPlayerCallback(HWND hwnd) : m_refCount(1), m_hwnd(hwnd), m_pPlayer(nullptr) {}

    void SetPlayer(IMFPMediaPlayer* pPlayer) { m_pPlayer = pPlayer; }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFPMediaPlayerCallback)) {
            *ppv = static_cast<IMFPMediaPlayerCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_refCount);
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) delete this;
        return count;
    }

    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader) override {
        if (FAILED(pEventHeader->hrEvent)) {
            PostMessageW(m_hwnd, WM_APP_MEDIA_ERROR, (WPARAM)pEventHeader->eEventType, (LPARAM)pEventHeader->hrEvent);
            return;
        }

        switch (pEventHeader->eEventType) {
        case MFP_EVENT_TYPE_MEDIAITEM_CREATED: {
            MFP_MEDIAITEM_CREATED_EVENT* pItemCreated = MFP_GET_MEDIAITEM_CREATED_EVENT(pEventHeader);
            if (pItemCreated && pItemCreated->pMediaItem && m_pPlayer) {
                m_pPlayer->SetMediaItem(pItemCreated->pMediaItem);
            }
            break;
        }
        case MFP_EVENT_TYPE_MEDIAITEM_SET:
            PostMessageW(m_hwnd, WM_APP_MEDIA_SET, 0, 0);
            break;
        case MFP_EVENT_TYPE_PLAYBACK_ENDED:
            PostMessageW(m_hwnd, WM_APP_PLAYBACK_ENDED, 0, 0);
            break;
        default:
            break;
        }
    }
};

bool OpenImageFile(const std::wstring& filePath) {
    if (g_pPlayer) {
        g_pPlayer->Stop();
    }
    g_bIsPlaying = false;
    g_bHasMedia = false;
    g_bHasVideo = false;

    if (g_pImage) {
        delete g_pImage;
        g_pImage = nullptr;
    }

    g_pImage = new Gdiplus::Image(filePath.c_str());
    if (!g_pImage || g_pImage->GetLastStatus() != Gdiplus::Ok) {
        if (g_pImage) {
            delete g_pImage;
            g_pImage = nullptr;
        }
        MessageBoxW(g_hwndMain, L"Failed to load image file.", L"Image Error", MB_ICONERROR);
        return false;
    }

    g_bHasImage = true;
    g_currentFile = filePath;
    g_currentFileName = filePath;
    size_t lastSlash = g_currentFileName.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        g_currentFileName = g_currentFileName.substr(lastSlash + 1);
    }

    // Hide video window so viewport displays the image
    ShowWindow(g_hwndVideo, SW_HIDE);

    // Update UI controls
    SetWindowTextW(g_hwndMain, (L"Video Player - " + g_currentFileName).c_str());
    SetWindowTextW(g_hwndStaticTitle, g_currentFileName.c_str());

    std::wostringstream oss;
    oss << g_pImage->GetWidth() << L" x " << g_pImage->GetHeight() << L" px";
    SetWindowTextW(g_hwndStaticTime, oss.str().c_str());

    SetWindowTextW(g_hwndBtnPlay, L"Play");
    SendMessageW(g_hwndSeekBar, TBM_SETRANGE, TRUE, MAKELONG(0, 0));
    SendMessageW(g_hwndSeekBar, TBM_SETPOS, TRUE, 0);

    InvalidateRect(g_hwndViewport, NULL, TRUE);
    UpdateWindow(g_hwndViewport);
    return true;
}

void OpenMediaFile(const std::wstring& filePath) {
    if (!g_pPlayer || filePath.empty()) return;

    if (g_bIsPlaying) {
        g_pPlayer->Stop();
    }

    // Clear any active image
    if (g_pImage) {
        delete g_pImage;
        g_pImage = nullptr;
    }
    g_bHasImage = false;

    g_currentFile = filePath;
    g_currentFileName = filePath;
    size_t lastSlash = g_currentFileName.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        g_currentFileName = g_currentFileName.substr(lastSlash + 1);
    }

    g_duration = 0;
    g_bHasMedia = false;
    g_bIsPlaying = false;

    // AUDIO CHECK: If it's an audio file, immediately clear screen and hide video window
    if (IsAudioFile(filePath)) {
        g_bHasVideo = false;
        ShowWindow(g_hwndVideo, SW_HIDE);
        InvalidateRect(g_hwndViewport, NULL, TRUE);
        UpdateWindow(g_hwndViewport);
    } else {
        // Video file: prepare video window
        g_bHasVideo = true;
        ShowWindow(g_hwndVideo, SW_SHOW);
    }

    SetWindowTextW(g_hwndMain, (L"Video Player - " + g_currentFileName).c_str());
    SetWindowTextW(g_hwndStaticTitle, g_currentFileName.c_str());
    SetWindowTextW(g_hwndStaticTime, L"00:00 / 00:00");
    SendMessageW(g_hwndSeekBar, TBM_SETPOS, TRUE, 0);

    HRESULT hr = g_pPlayer->CreateMediaItemFromURL(filePath.c_str(), FALSE, 0, NULL);
    if (FAILED(hr)) {
        MessageBoxW(g_hwndMain, (L"Failed to open media file.\nError code: 0x" + std::to_wstring(hr)).c_str(), L"Playback Error", MB_ICONERROR);
    }
}

void OpenFileAny(const std::wstring& filePath) {
    if (filePath.empty()) return;
    if (IsImageFile(filePath)) {
        OpenImageFile(filePath);
    } else {
        OpenMediaFile(filePath);
    }
}

void TogglePlayPause() {
    if (g_bHasImage) return; // Images don't have play/pause
    if (!g_pPlayer || !g_bHasMedia) return;

    MFP_MEDIAPLAYER_STATE state;
    if (SUCCEEDED(g_pPlayer->GetState(&state))) {
        if (state == MFP_MEDIAPLAYER_STATE_PLAYING) {
            g_pPlayer->Pause();
            g_bIsPlaying = false;
            SetWindowTextW(g_hwndBtnPlay, L"Play");
        } else {
            g_pPlayer->Play();
            g_bIsPlaying = true;
            SetWindowTextW(g_hwndBtnPlay, L"Pause");
        }
        if (!g_bHasVideo) {
            InvalidateRect(g_hwndViewport, NULL, TRUE);
        }
    }
}

void StopPlayback() {
    if (g_bHasImage) return;
    if (!g_pPlayer || !g_bHasMedia) return;

    g_pPlayer->Stop();
    g_bIsPlaying = false;
    SetWindowTextW(g_hwndBtnPlay, L"Play");
    SendMessageW(g_hwndSeekBar, TBM_SETPOS, TRUE, 0);
    SetWindowTextW(g_hwndStaticTime, (L"00:00 / " + FormatTime(g_duration)).c_str());
    if (!g_bHasVideo) {
        InvalidateRect(g_hwndViewport, NULL, TRUE);
    } else {
        InvalidateRect(g_hwndVideo, NULL, TRUE);
    }
}

void ToggleMute() {
    if (!g_pPlayer) return;

    g_bIsMuted = !g_bIsMuted;
    g_pPlayer->SetMute(g_bIsMuted ? TRUE : FALSE);
    SetWindowTextW(g_hwndBtnMute, g_bIsMuted ? L"Unmute" : L"Mute");
}

void SetVolumeFromSlider(int pos) {
    if (!g_pPlayer) return;
    if (pos < 0) pos = 0;
    if (pos > 100) pos = 100;
    g_currentVolume = (float)pos / 100.0f;
    g_pPlayer->SetVolume(g_currentVolume);
    if (g_bIsMuted && pos > 0) {
        g_bIsMuted = false;
        g_pPlayer->SetMute(FALSE);
        SetWindowTextW(g_hwndBtnMute, L"Mute");
    }
}

void StepPosition(LONGLONG deltaSeconds) {
    if (g_bHasImage) return;
    if (!g_pPlayer || !g_bHasMedia || g_duration <= 0) return;

    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(g_pPlayer->GetPosition(MFP_POSITIONTYPE_100NS, &var))) {
        LONGLONG current = var.uhVal.QuadPart;
        LONGLONG delta = deltaSeconds * 10000000;
        LONGLONG target = current + delta;
        if (target < 0) target = 0;
        if (target > g_duration) target = g_duration;

        PROPVARIANT varTarget;
        PropVariantInit(&varTarget);
        varTarget.vt = VT_UI8;
        varTarget.uhVal.QuadPart = target;
        g_pPlayer->SetPosition(MFP_POSITIONTYPE_100NS, &varTarget);
        PropVariantClear(&varTarget);
    }
    PropVariantClear(&var);
}

void ToggleFullscreen() {
    DWORD dwStyle = (DWORD)GetWindowLongPtrW(g_hwndMain, GWL_STYLE);
    if (!g_bIsFullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(g_hwndMain, &g_prevPlacement) &&
            GetMonitorInfoW(MonitorFromWindow(g_hwndMain, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongPtrW(g_hwndMain, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(g_hwndMain, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g_bIsFullscreen = true;
            SetWindowTextW(g_hwndBtnFullscreen, L"Exit Fullscreen");
        }
    } else {
        SetWindowLongPtrW(g_hwndMain, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_hwndMain, &g_prevPlacement);
        SetWindowPos(g_hwndMain, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_bIsFullscreen = false;
        SetWindowTextW(g_hwndBtnFullscreen, L"Fullscreen");
    }
}

void ChooseFileAndOpen() {
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC fileTypes[] = {
            { L"All Supported Files (*.mp4;*.mp3;*.wav;*.png;*.jpg;...)", L"*.mp4;*.mp3;*.wav;*.mkv;*.avi;*.wmv;*.m4a;*.flac;*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.ico;*.tiff;*.jfif" },
            { L"Video Files (*.mp4;*.mkv;*.avi;*.wmv;*.mov;*.webm)", L"*.mp4;*.mkv;*.avi;*.wmv;*.mov;*.webm" },
            { L"Audio Files (*.mp3;*.wav;*.m4a;*.flac;*.wma;*.aac)", L"*.mp3;*.wav;*.m4a;*.flac;*.wma;*.aac" },
            { L"Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.ico;*.tiff)", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.ico;*.tiff;*.jfif" },
            { L"All Files (*.*)", L"*.*" }
        };
        pFileOpen->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes);
        pFileOpen->SetTitle(L"Open Media or Image File");

        if (SUCCEEDED(pFileOpen->Show(g_hwndMain))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    OpenFileAny(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
}

void LayoutControls(int width, int height) {
    if (width <= 0 || height <= 0) return;

    int controlBarHeight = 90;
    int videoHeight = height - controlBarHeight;
    if (videoHeight < 0) videoHeight = 0;

    // Viewport area and video window layout
    MoveWindow(g_hwndViewport, 0, 0, width, videoHeight, TRUE);
    MoveWindow(g_hwndVideo, 0, 0, width, videoHeight, TRUE);
    if (g_bHasVideo && g_pPlayer) {
        g_pPlayer->UpdateVideo();
    }

    // Bottom controls
    int startY = videoHeight + 5;
    
    // Row 1: Title and Time
    MoveWindow(g_hwndStaticTitle, 15, startY, width - 180, 20, TRUE);
    MoveWindow(g_hwndStaticTime, width - 165, startY, 150, 20, TRUE);

    // Row 2: Seek bar
    int seekY = startY + 22;
    MoveWindow(g_hwndSeekBar, 10, seekY, width - 20, 26, TRUE);

    // Row 3: Buttons and Volume
    int btnY = seekY + 28;
    int btnH = 26;

    MoveWindow(g_hwndBtnOpen,       15,  btnY, 80,  btnH, TRUE);
    MoveWindow(g_hwndBtnPlay,       105, btnY, 80,  btnH, TRUE);
    MoveWindow(g_hwndBtnStop,       195, btnY, 80,  btnH, TRUE);
    MoveWindow(g_hwndBtnMute,       285, btnY, 70,  btnH, TRUE);
    MoveWindow(g_hwndVolSlider,     365, btnY, 110, btnH, TRUE);
    MoveWindow(g_hwndBtnFullscreen, width - 145, btnY, 130, btnH, TRUE);
}

// Viewport Window Procedure (Handles Images, Audio Display, Idle Screen, and Background)
LRESULT CALLBACK ViewportWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_hBrushBg);

        SetBkMode(hdc, TRANSPARENT);

        if (g_bHasImage && g_pImage) {
            // High-quality image rendering preserving aspect ratio
            Gdiplus::Graphics graphics(hdc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

            UINT imgW = g_pImage->GetWidth();
            UINT imgH = g_pImage->GetHeight();

            if (imgW > 0 && imgH > 0 && rc.right > 0 && rc.bottom > 0) {
                float scaleX = (float)rc.right / (float)imgW;
                float scaleY = (float)rc.bottom / (float)imgH;
                float scale = (scaleX < scaleY) ? scaleX : scaleY;

                int destW = (int)(imgW * scale);
                int destH = (int)(imgH * scale);
                int destX = (rc.right - destW) / 2;
                int destY = (rc.bottom - destH) / 2;

                graphics.DrawImage(g_pImage, destX, destY, destW, destH);
            }
        } else if (g_bHasMedia && !g_bHasVideo) {
            int centerY = (rc.bottom - rc.top) / 2;

            // Track file name
            SelectObject(hdc, g_hFontBig);
            SetTextColor(hdc, RGB(245, 245, 245));
            RECT rcTitle = rc;
            rcTitle.top = centerY - 24;
            rcTitle.bottom = centerY + 8;
            DrawTextW(hdc, g_currentFileName.c_str(), -1, &rcTitle, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            // Status (Playing / Paused)
            SelectObject(hdc, g_hFontUI);
            SetTextColor(hdc, RGB(160, 160, 160));
            RECT rcStatus = rc;
            rcStatus.top = centerY + 12;
            rcStatus.bottom = centerY + 36;
            std::wstring status = g_bIsPlaying ? L"Status: Playing" : L"Status: Paused";
            DrawTextW(hdc, status.c_str(), -1, &rcStatus, DT_CENTER | DT_SINGLELINE);
        } else if (!g_bHasMedia) {
            // Idle Screen
            SelectObject(hdc, g_hFontUI);
            SetTextColor(hdc, RGB(180, 180, 180));
            std::wstring msg = L"No Media Loaded\r\n\r\nClick 'Open' or drag & drop video, audio, or image files here";
            DrawTextW(hdc, msg.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDBLCLK:
        ToggleFullscreen();
        return 0;

    case WM_LBUTTONDOWN:
        TogglePlayPause();
        return 0;

    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Dedicated Video Render Window Procedure
LRESULT CALLBACK VideoWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (g_pPlayer && g_bHasMedia && g_bHasVideo) {
            g_pPlayer->UpdateVideo();
        } else {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, g_hBrushBg);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (g_pPlayer && g_bHasMedia && g_bHasVideo) {
            g_pPlayer->UpdateVideo();
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        ToggleFullscreen();
        return 0;

    case WM_LBUTTONDOWN:
        TogglePlayPause();
        return 0;

    case WM_ERASEBKGND:
        if (g_bHasMedia && g_bHasVideo) {
            return 1; // Prevent video flickering
        }
        break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Main Window Procedure
LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // Register Viewport Container Class
        WNDCLASSW wcViewport = { 0 };
        wcViewport.lpfnWndProc = ViewportWindowProc;
        wcViewport.hInstance = g_hInstance;
        wcViewport.lpszClassName = L"ViewportContainerClass";
        wcViewport.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcViewport.style = CS_DBLCLKS;
        RegisterClassW(&wcViewport);

        // Register Video Render Class
        WNDCLASSW wcVideo = { 0 };
        wcVideo.lpfnWndProc = VideoWindowProc;
        wcVideo.hInstance = g_hInstance;
        wcVideo.lpszClassName = L"VideoRenderClass";
        wcVideo.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcVideo.style = CS_DBLCLKS;
        RegisterClassW(&wcVideo);

        // Create Viewport Background Container
        g_hwndViewport = CreateWindowExW(
            0, L"ViewportContainerClass", NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            0, 0, 100, 100, hwnd, NULL, g_hInstance, NULL
        );

        // Create Video Window as child of Viewport
        g_hwndVideo = CreateWindowExW(
            0, L"VideoRenderClass", NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 100, 100, g_hwndViewport, NULL, g_hInstance, NULL
        );

        // Create UI Fonts
        g_hFontUI = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        g_hFontBig = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Create Controls
        g_hwndStaticTitle = CreateWindowExW(0, L"STATIC", L"Ready",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
            0, 0, 0, 0, hwnd, (HMENU)IDC_STATIC_TITLE, g_hInstance, NULL);

        g_hwndStaticTime = CreateWindowExW(0, L"STATIC", L"00:00 / 00:00",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            0, 0, 0, 0, hwnd, (HMENU)IDC_STATIC_TIME, g_hInstance, NULL);

        g_hwndSeekBar = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | TBS_TOOLTIPS,
            0, 0, 0, 0, hwnd, (HMENU)IDC_SEEKBAR, g_hInstance, NULL);

        g_hwndBtnOpen = CreateWindowExW(0, L"BUTTON", L"Open",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_OPEN, g_hInstance, NULL);

        g_hwndBtnPlay = CreateWindowExW(0, L"BUTTON", L"Play",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_PLAYPAUSE, g_hInstance, NULL);

        g_hwndBtnStop = CreateWindowExW(0, L"BUTTON", L"Stop",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_STOP, g_hInstance, NULL);

        g_hwndBtnMute = CreateWindowExW(0, L"BUTTON", L"Mute",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_MUTE, g_hInstance, NULL);

        g_hwndVolSlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            0, 0, 0, 0, hwnd, (HMENU)IDC_VOLUMESLIDER, g_hInstance, NULL);

        g_hwndBtnFullscreen = CreateWindowExW(0, L"BUTTON", L"Fullscreen",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_BTN_FULLSCREEN, g_hInstance, NULL);

        // Apply font
        HWND controls[] = {
            g_hwndStaticTitle, g_hwndStaticTime, g_hwndBtnOpen, g_hwndBtnPlay,
            g_hwndBtnStop, g_hwndBtnMute, g_hwndBtnFullscreen
        };
        for (HWND hCtrl : controls) {
            SendMessageW(hCtrl, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        }

        // Configure sliders
        SendMessageW(g_hwndSeekBar, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(g_hwndVolSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(g_hwndVolSlider, TBM_SETPOS, TRUE, 80);

        // Enable Drag & Drop
        DragAcceptFiles(hwnd, TRUE);

        // Create Media Player
        g_pCallback = new MediaPlayerCallback(hwnd);
        HRESULT hr = MFPCreateMediaPlayer(NULL, FALSE, 0, g_pCallback, g_hwndVideo, &g_pPlayer);
        if (SUCCEEDED(hr)) {
            g_pCallback->SetPlayer(g_pPlayer);
            g_pPlayer->SetVolume(g_currentVolume);
        } else {
            MessageBoxW(hwnd, L"Failed to initialize Windows Media Player engine.", L"Error", MB_ICONERROR);
        }

        // Set up playback update timer (200ms)
        SetTimer(hwnd, IDT_PLAY_TIMER, 200, NULL);
        return 0;
    }

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        LayoutControls(width, height);
        if (g_bHasImage) {
            InvalidateRect(g_hwndViewport, NULL, TRUE);
        }
        return 0;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case IDC_BTN_OPEN:
            ChooseFileAndOpen();
            break;
        case IDC_BTN_PLAYPAUSE:
            TogglePlayPause();
            break;
        case IDC_BTN_STOP:
            StopPlayback();
            break;
        case IDC_BTN_MUTE:
            ToggleMute();
            break;
        case IDC_BTN_FULLSCREEN:
            ToggleFullscreen();
            break;
        }
        return 0;
    }

    case WM_HSCROLL: {
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == g_hwndSeekBar) {
            if (g_bHasImage) return 0;
            int code = LOWORD(wParam);
            if (code == TB_THUMBTRACK) {
                g_bIsSeeking = true;
                int pos = (int)SendMessageW(g_hwndSeekBar, TBM_GETPOS, 0, 0);
                if (g_duration > 0) {
                    LONGLONG targetHns = (LONGLONG)pos * 10000000;
                    SetWindowTextW(g_hwndStaticTime, (FormatTime(targetHns) + L" / " + FormatTime(g_duration)).c_str());
                }
            } else if (code == TB_THUMBPOSITION || code == TB_ENDTRACK) {
                if (g_bIsSeeking) {
                    g_bIsSeeking = false;
                    int pos = (int)SendMessageW(g_hwndSeekBar, TBM_GETPOS, 0, 0);
                    if (g_pPlayer && g_bHasMedia && g_duration > 0) {
                        PROPVARIANT var;
                        PropVariantInit(&var);
                        var.vt = VT_UI8;
                        var.uhVal.QuadPart = (ULONGLONG)pos * 10000000ULL;
                        g_pPlayer->SetPosition(MFP_POSITIONTYPE_100NS, &var);
                        PropVariantClear(&var);
                    }
                }
            }
        } else if (hCtrl == g_hwndVolSlider) {
            int pos = (int)SendMessageW(g_hwndVolSlider, TBM_GETPOS, 0, 0);
            SetVolumeFromSlider(pos);
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam == IDT_PLAY_TIMER) {
            if (g_pPlayer && g_bHasMedia && !g_bIsSeeking && !g_bHasImage) {
                MFP_MEDIAPLAYER_STATE state;
                if (SUCCEEDED(g_pPlayer->GetState(&state)) && state == MFP_MEDIAPLAYER_STATE_PLAYING) {
                    PROPVARIANT var;
                    PropVariantInit(&var);
                    if (SUCCEEDED(g_pPlayer->GetPosition(MFP_POSITIONTYPE_100NS, &var))) {
                        LONGLONG currentHns = var.uhVal.QuadPart;
                        int currentSec = (int)(currentHns / 10000000);
                        SendMessageW(g_hwndSeekBar, TBM_SETPOS, TRUE, currentSec);
                        SetWindowTextW(g_hwndStaticTime, (FormatTime(currentHns) + L" / " + FormatTime(g_duration)).c_str());
                    }
                    PropVariantClear(&var);
                }
            }
        }
        return 0;
    }

    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_SPACE:
            TogglePlayPause();
            return 0;
        case VK_LEFT:
            StepPosition(-5);
            return 0;
        case VK_RIGHT:
            StepPosition(5);
            return 0;
        case VK_UP: {
            int pos = (int)SendMessageW(g_hwndVolSlider, TBM_GETPOS, 0, 0) + 5;
            if (pos > 100) pos = 100;
            SendMessageW(g_hwndVolSlider, TBM_SETPOS, TRUE, pos);
            SetVolumeFromSlider(pos);
            return 0;
        }
        case VK_DOWN: {
            int pos = (int)SendMessageW(g_hwndVolSlider, TBM_GETPOS, 0, 0) - 5;
            if (pos < 0) pos = 0;
            SendMessageW(g_hwndVolSlider, TBM_SETPOS, TRUE, pos);
            SetVolumeFromSlider(pos);
            return 0;
        }
        case 'M':
            ToggleMute();
            return 0;
        case 'F':
            ToggleFullscreen();
            return 0;
        case VK_F11:
            ToggleFullscreen();
            return 0;
        case VK_ESCAPE:
            if (g_bIsFullscreen) {
                ToggleFullscreen();
            }
            return 0;
        }
        break;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        WCHAR szFile[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, szFile, MAX_PATH)) {
            OpenFileAny(szFile);
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(220, 220, 220));
        SetBkColor(hdcStatic, RGB(37, 37, 38));
        return (INT_PTR)g_hBrushBar;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_hBrushBar);
        return 1;
    }

    case WM_APP_MEDIA_SET: {
        g_bHasMedia = true;

        bool bAudioExt = IsAudioFile(g_currentFile);
        BOOL bHasVideo = FALSE, bIsSelected = FALSE;

        IMFPMediaItem* pItem = nullptr;
        if (SUCCEEDED(g_pPlayer->GetMediaItem(&pItem)) && pItem) {
            pItem->HasVideo(&bHasVideo, &bIsSelected);
            pItem->Release();
        }

        // CHECK: If it's an audio format or has no video stream, ALWAYS hide the video window and clear screen
        if (bAudioExt || !bHasVideo || !bIsSelected) {
            g_bHasVideo = false;
            ShowWindow(g_hwndVideo, SW_HIDE);
            InvalidateRect(g_hwndViewport, NULL, TRUE);
            UpdateWindow(g_hwndViewport);
        } else {
            g_bHasVideo = true;
            ShowWindow(g_hwndVideo, SW_SHOW);
            InvalidateRect(g_hwndVideo, NULL, TRUE);
            UpdateWindow(g_hwndVideo);
        }

        // Get total duration
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(g_pPlayer->GetDuration(MFP_POSITIONTYPE_100NS, &var))) {
            g_duration = var.uhVal.QuadPart;
            int totalSeconds = (int)(g_duration / 10000000);
            SendMessageW(g_hwndSeekBar, TBM_SETRANGEMIN, TRUE, 0);
            SendMessageW(g_hwndSeekBar, TBM_SETRANGEMAX, TRUE, totalSeconds);
            SendMessageW(g_hwndSeekBar, TBM_SETPOS, TRUE, 0);
            SetWindowTextW(g_hwndStaticTime, (L"00:00 / " + FormatTime(g_duration)).c_str());
        }
        PropVariantClear(&var);

        // Start playback
        g_pPlayer->Play();
        g_bIsPlaying = true;
        SetWindowTextW(g_hwndBtnPlay, L"Pause");
        return 0;
    }

    case WM_APP_PLAYBACK_ENDED: {
        g_bIsPlaying = false;
        SetWindowTextW(g_hwndBtnPlay, L"Play");
        SendMessageW(g_hwndSeekBar, TBM_SETPOS, TRUE, 0);
        if (!g_bHasVideo) {
            InvalidateRect(g_hwndViewport, NULL, TRUE);
        }
        return 0;
    }

    case WM_APP_MEDIA_ERROR: {
        MessageBoxW(hwnd, L"An error occurred during media playback.", L"Media Error", MB_ICONWARNING);
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, IDT_PLAY_TIMER);
        if (g_pPlayer) {
            g_pPlayer->Shutdown();
            g_pPlayer->Release();
            g_pPlayer = nullptr;
        }
        if (g_pCallback) {
            g_pCallback->Release();
            g_pCallback = nullptr;
        }
        if (g_pImage) {
            delete g_pImage;
            g_pImage = nullptr;
        }
        if (g_gdiplusToken) {
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
        if (g_hFontUI) DeleteObject(g_hFontUI);
        if (g_hFontBig) DeleteObject(g_hFontBig);
        if (g_hBrushBg) DeleteObject(g_hBrushBg);
        if (g_hBrushBar) DeleteObject(g_hBrushBar);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    g_hInstance = hInstance;

    // Initialize COM and Common Controls
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // Initialize GDI+ for Image Support
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

    g_hBrushBg = CreateSolidBrush(RGB(15, 15, 15));
    g_hBrushBar = CreateSolidBrush(RGB(37, 37, 38));

    // Register Main Window Class
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MainWindowProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = g_hBrushBar;
    wcex.lpszClassName = L"ModernVideoPlayerWindowClass";
    RegisterClassExW(&wcex);

    // Create Main Window (Default size: 854x540 for 16:9 preview)
    g_hwndMain = CreateWindowExW(
        0, L"ModernVideoPlayerWindowClass", L"Video Player (C++)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 870, 580,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hwndMain) {
        if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
        CoUninitialize();
        return 1;
    }

    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);

    // Check if a file was passed as argument
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        OpenFileAny(argv[1]);
        LocalFree(argv);
    }

    // Message Loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
