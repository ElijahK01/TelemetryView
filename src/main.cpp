// Dear ImGui: standalone example application for DirectX 12

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// Important: to compile on 32-bit systems, the DirectX12 backend requires code to be compiled with '#define ImTextureID ImU64'.
// This is because we need ImTextureID to carry a 64-bit value and by default ImTextureID is defined as void*.
// This define is set in the example .vcxproj file and need to be replicated in your app or by adding it to your imconfig.h file.

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <tchar.h>
#include <implot.h>
#include <simple-serial-port/simple-serial-port/SimpleSerial.h>
#include <iostream>
#include <thread>
#include <future>
#include <algorithm>
#include <fstream>
#include <list>
#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

// Serial task masks
#define SEND_ENABLE 1
#define SEND_DISABLE 2
#define READ_SERIAL 4

uint8_t passedSerialParams = 0;


struct FrameContext
{
    ID3D12CommandAllocator* CommandAllocator;
    UINT64                  FenceValue;
};

struct RollingBuffer {
    float Span;
    ImVector<ImVec2> Data;
    RollingBuffer() {
        Span = 10.0f;
        Data.reserve(2000);
    }
    void AddPoint(float x, float y) {
        float xmod = fmodf(x, Span);
        if (!Data.empty() && xmod < Data.back().x)
            Data.shrink(0);
        Data.push_back(ImVec2(xmod, y));
    }
};

// Data
static int const                    NUM_FRAMES_IN_FLIGHT = 3;
static FrameContext                 g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
static UINT                         g_frameIndex = 0;

static int const                    NUM_BACK_BUFFERS = 3;
static ID3D12Device* g_pd3dDevice = nullptr;
static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
static ID3D12CommandQueue* g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList* g_pd3dCommandList = nullptr;
static ID3D12Fence* g_fence = nullptr;
static HANDLE                       g_fenceEvent = nullptr;
static UINT64                       g_fenceLastSignaledValue = 0;
static IDXGISwapChain3* g_pSwapChain = nullptr;
static HANDLE                       g_hSwapChainWaitableObject = nullptr;
static ID3D12Resource* g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForLastSubmittedFrame();
void LinkedText(bool active, char text[]);
float vecMag(float a, float b, float c);

string serialCommTasks();
string GetSerialData();
void payloadReleaseEnable();
void payloadReleaseDisable();

FrameContext* WaitForNextFrameResources();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

char com_port[] = "\\\\.\\COM5";
DWORD COM_BAUD_RATE = CBR_9600;
SimpleSerial Serial(com_port, COM_BAUD_RATE);



// Main code
int main(int, char**)
{

    // Create application window
    //ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX12 Example", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(g_pd3dDevice, NUM_FRAMES_IN_FLIGHT,
        DXGI_FORMAT_R8G8B8A8_UNORM, g_pd3dSrvDescHeap,
        g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != nullptr);

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    bool show_telemetry = true;

    bool show_overview = true;
    bool show_altitude = false;
    bool show_orientation = false;
    bool show_acceleration = false;
    bool show_velocity = false;
    bool logData = false;

    string lastData = "Data:00:00:00:00:00:00:00:00:00:00:00:00:";
    float lastNum1 = 0;
    float lastNum2 = 0;

    std::ofstream dataFile;
    dataFile.open("data.csv");
    dataFile << "x orientatuin, y orientation, z orientation, x acceleration, y acceleration, z acceleration, x velocity, y velocity, z velocity, time";
    

    uint8_t state = 0;

    ImVec4 clear_color = ImVec4(0.4f, 0.35f, 0.7f, 1.00f);

    // graph data points
    static RollingBuffer altitude;
    altitude.AddPoint(0, 0);
    static RollingBuffer xOrient;
    xOrient.AddPoint(0, 0);
    static RollingBuffer yOrient;
    yOrient.AddPoint(0, 0);
    static RollingBuffer zOrient;
    zOrient.AddPoint(0, 0);
    static RollingBuffer xAccel;
    xAccel.AddPoint(0, 0);
    static RollingBuffer yAccel;
    yAccel.AddPoint(0, 0);
    static RollingBuffer zAccel;
    zAccel.AddPoint(0, 0);
    static RollingBuffer AccelerationMagnitude;
    AccelerationMagnitude.AddPoint(0, 0);
    static RollingBuffer xMag;
    xMag.AddPoint(0, 0);
    static RollingBuffer yMag;
    yMag.AddPoint(0, 0);
    static RollingBuffer zMag;
    zMag.AddPoint(0, 0);

    static RollingBuffer force;
    force.AddPoint(0, 0);
    static RollingBuffer temp;
    temp.AddPoint(0, 0);
    static RollingBuffer time;
    time.AddPoint(0, 0);

    std::future<string> dataThread = std::async(GetSerialData);
    uint8_t serialActions = 0;

    // Main loop
    bool done = false;

    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Start the Dear ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // serial communication
        if (Serial.connected_) {

            ImGui::Begin("Rocket Altitude", &show_telemetry);
            ImGui::Text("Arduino is connected :)");

            string incoming = "nothin";

            if (dataThread._Is_ready())
            {
                incoming = dataThread.get();
                passedSerialParams = serialActions |= READ_SERIAL; // always read serial
                dataThread = std::async(serialCommTasks);
            }
                
            // multithreading:
            // 1. start reading serial port near program start with thread that returns the string
            // 2. check if thread has finished
            // 3. if finished, use value returned as data and start new thread listening for data
            // 4. if not finished, continue and do not update data.
            // "Data:" + String(recieveData.data.xG) + ":"
            /*+String(recieveData.data.yG) + ":"
                + String(recieveData.data.zG) + ":"
                + String(recieveData.data.xA) + ":"
                + String(recieveData.data.yA) + ":"
                + String(recieveData.data.zA) + ":"
                + String(recieveData.data.xM) + ":"
                + String(recieveData.data.yM) + ":"
                + String(recieveData.data.zM) + ":"
                + String(recieveData.data.f) + ":"
                + String(recieveData.data.t) + ":"
                + String(recieveData.data.time) + ":"
                + String(recieveData.data.alt) + ":"
            */

            
            if (count(incoming.begin(), incoming.end(), ':') >= 13)
            {
                lastData = incoming;  
                if (lastData.length() >= 10)
                {
                    int delimiterPositions[16];
                    delimiterPositions[0] = lastData.find_first_of(':');

                    for (int i = 1; i < 16; i++)
                    {
                        delimiterPositions[i] = lastData.find(':', delimiterPositions[i-1] + 1);
                    }

                    int currentTime = ImGui::GetTime();

                    xOrient.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[1] + 1, delimiterPositions[2] - 1)) / 1.0f);
                    yOrient.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[2] + 1, delimiterPositions[3] - 1)) / 1.0f);
                    zOrient.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[3] + 1, delimiterPositions[4] - 1)) / 1.0f);
                    xAccel.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[4] + 1, delimiterPositions[5] - 1)) / 1.0f);
                    yAccel.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[5] + 1, delimiterPositions[6] - 1)) / 1.0f);
                    zAccel.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[6] + 1, delimiterPositions[7] - 1)) / 1.0f);
                    xMag.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[7] + 1, delimiterPositions[8] - 1)) / 1.0f);
                    yMag.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[8] + 1, delimiterPositions[9] - 1)) / 1.0f);
                    zMag.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[9] + 1, delimiterPositions[10] - 1)) / 1.0f);

                    AccelerationMagnitude.AddPoint(currentTime, vecMag(xAccel.Data.back()[0], yAccel.Data.back()[0], zAccel.Data.back()[0]));

                    force.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[10] + 1, delimiterPositions[11] - 1)) / 1.0f);
                    temp.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[11] + 1, delimiterPositions[12] - 1)) / 1.0f);
                    time.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[12] + 1, delimiterPositions[13] - 1)) / 1.0f);

                    altitude.AddPoint(currentTime, stoi(lastData.substr(delimiterPositions[14] + 1, delimiterPositions[15])) / 1.0f);

                    if (logData)
                    {
                        // save data to csv file
                        dataFile << xOrient.Data.back().x;
                        dataFile << yOrient.Data.back().x;
                        dataFile << zOrient.Data.back().x;
                        dataFile << xAccel.Data.back().x;
                        dataFile << yAccel.Data.back().x;
                        dataFile << zAccel.Data.back().x;
                    }
                }
            }
            
            ImGui::Text(lastData.c_str());
            ImGui::Text(to_string(lastNum1).c_str());

            if (ImGui::BeginTable("split", 2))
            {
                ImGui::TableSetupColumn("Graph Selection", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Rocket State", ImGuiTableColumnFlags_WidthFixed, 150.0f);

                ImGui::TableNextColumn();

                if (ImGui::BeginTable("split", 5))
                {
                    ImGui::TableNextColumn(); ImGui::Selectable("Overview", &show_overview);
                    ImGui::TableNextColumn(); ImGui::Selectable("Altitude", &show_altitude);
                    ImGui::TableNextColumn(); ImGui::Selectable("Velocity", &show_velocity);
                    ImGui::TableNextColumn(); ImGui::Selectable("Orientation", &show_orientation);
                    ImGui::TableNextColumn(); ImGui::Selectable("Acceleration", &show_acceleration);
                    ImGui::EndTable();
                }

                static ImPlotAxisFlags flags = ImPlotAxisFlags_NoTickLabels;
                static float history = 20.0f;
                altitude.Span = history;

                if (show_overview)
                {
                    if (ImPlot::BeginPlot("Overview", ImVec2(-1, 300))) {
                        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0, history, ImGuiCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1);
                        ImPlot::PlotLine("Altitude", &altitude.Data[0].x, &altitude.Data[0].y, altitude.Data.size(), 0, 0, 2 * sizeof(float));
                        //ImPlot::PlotLine("Velocity", &velMag.Data[0].x, &velMag.Data[0].y, velMag.Data.size(), 0, 0, 2 * sizeof(float));
                        ImPlot::PlotLine("Acceleration", &AccelerationMagnitude.Data[0].x, &AccelerationMagnitude.Data[0].y, AccelerationMagnitude.Data.size(), 0, 0, 2 * sizeof(float));
                        ImPlot::EndPlot();
                    }
                }

                if (show_altitude)
                {
                    if (ImPlot::BeginPlot("Altiude", ImVec2(-1, 300))) {
                        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0, history, ImGuiCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1);
                        ImPlot::PlotLine("Rocket Alt", &altitude.Data[0].x, &altitude.Data[0].y, altitude.Data.size(), 0, 0, 2 * sizeof(float));
                        ImPlot::EndPlot();
                    }
                }

                if (show_velocity)
                {
                    if (ImPlot::BeginPlot("Velocity", ImVec2(-1, 300))) {
                        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0, history, ImGuiCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1);
                        //ImPlot::PlotLine("N-S Speed", &xVel.Data[0].x, &xVel.Data[0].y, xVel.Data.size(), 0, 0, 2 * sizeof(float));
                        //ImPlot::PlotLine("E-W Speed", &yVel.Data[0].x, &yVel.Data[0].y, yVel.Data.size(), 0, 0, 2 * sizeof(float));
                        //ImPlot::PlotLine("Veritcal Speed", &zVel.Data[0].x, &zVel.Data[0].y, zVel.Data.size(), 0, 0, 2 * sizeof(float));
                        ImPlot::EndPlot();
                    }
                }

                if (show_orientation)
                {
                    if (ImPlot::BeginPlot("Orientation", ImVec2(-1, 150))) {
                        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0, history, ImGuiCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1);
                        ImPlot::PlotLine("X Orientation", &xOrient.Data[0].x, &xOrient.Data[0].y, xOrient.Data.size(), 0, 0, 2 * sizeof(float));
                        ImPlot::PlotLine("Y Orientation", &yOrient.Data[0].x, &yOrient.Data[0].y, yOrient.Data.size(), 0, 0, 2 * sizeof(float));
                        ImPlot::PlotLine("Z Orientation", &zOrient.Data[0].x, &zOrient.Data[0].y, zOrient.Data.size(), 0, 0, 2 * sizeof(float));
                        ImPlot::EndPlot();
                    }
                }

                if (show_acceleration)
                {
                    if (ImPlot::BeginPlot("Acceleration", ImVec2(-1, 150))) {
                        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0, history, ImGuiCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1);
                        ImPlot::PlotLine("X Acceleration", &xAccel.Data[0].x, &xAccel.Data[0].y, xAccel.Data.size(), 0, 0, 2 * sizeof(float));
                        ImPlot::PlotLine("Y Acceleration", &yAccel.Data[0].x, &yAccel.Data[0].y, yAccel.Data.size(), 0, 0, 2 * sizeof(float));
                        ImPlot::PlotLine("Z Acceleration", &zAccel.Data[0].x, &zAccel.Data[0].y, zAccel.Data.size(), 0, 0, 2 * sizeof(float));
                        ImPlot::EndPlot();
                    }
                }
                ImGui::TableNextColumn();

                // rocket state table
                

                char onPad[] = "On Pad";
                char launched[] = "Launched";
                char apogee[] = "Apogee";
                char asc[] = "Ascending";
                char desc[] = "Descending";
                char drgDepl[] = "Drogue Deployed";
                char mainDep[] = "Main Deployed";
                char landed[] = "Landed";

                LinkedText(state & 0x80, onPad);
                LinkedText(state & 0x40, launched);
                LinkedText(state & 0x20, apogee);
                LinkedText(state & 0x10, asc);
                LinkedText(state & 0x08, desc);
                LinkedText(state & 0x04, drgDepl);
                LinkedText(state & 0x02, mainDep);
                LinkedText(state & 0x01, landed);

                // Rocket enable button
                if (ImGui::Button("Release Payload"))
                    serialActions |= SEND_ENABLE;

                if (ImGui::Button("Cancel Release"))
                   serialActions |= SEND_DISABLE;

                ImGui::Checkbox("Enable Logging", &logData);

                ImGui::EndTable();
            }
            
            ImGui::End();
        }
           

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &show_another_window);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        // Telemetry Graphs
        if(show_telemetry){
            ImGui::Begin("Telemetry Graphs", &show_telemetry);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Telemetry Recieved From Rocket");
            if (ImGui::Button("Close Me"))
                show_telemetry = false;
            ImGui::End();
        }

        // Rendering
        ImGui::Render();

        FrameContext* frameCtx = WaitForNextFrameResources();
        UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
        frameCtx->CommandAllocator->Reset();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
        g_pd3dCommandList->ResourceBarrier(1, &barrier);

        // Render Dear ImGui graphics
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
        g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
        g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g_pd3dCommandList->ResourceBarrier(1, &barrier);
        g_pd3dCommandList->Close();

        g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);

        g_pSwapChain->Present(1, 0); // Present with vsync
        //g_pSwapChain->Present(0, 0); // Present without vsync

        UINT64 fenceValue = g_fenceLastSignaledValue + 1;
        g_pd3dCommandQueue->Signal(g_fence, fenceValue);
        g_fenceLastSignaledValue = fenceValue;
        frameCtx->FenceValue = fenceValue;
    }

    WaitForLastSubmittedFrame();

    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    dataFile.close();
    return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC1 sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = NUM_BACK_BUFFERS;
        sd.Width = 0;
        sd.Height = 0;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;
    }

    // [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* pdx12Debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif

    // Create device
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
        return false;

    // [DEBUG] Setup debug interface to break on any warnings/errors
#ifdef DX12_ENABLE_DEBUG_LAYER
    if (pdx12Debug != nullptr)
    {
        ID3D12InfoQueue* pInfoQueue = nullptr;
        g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
        pInfoQueue->Release();
        pdx12Debug->Release();
    }
#endif

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
            return false;

        SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        {
            g_mainRenderTargetDescriptor[i] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
            return false;
    }

    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
            return false;
    }

    for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
            return false;

    if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK ||
        g_pd3dCommandList->Close() != S_OK)
        return false;

    if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
        return false;

    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_fenceEvent == nullptr)
        return false;

    {
        IDXGIFactory4* dxgiFactory = nullptr;
        IDXGISwapChain1* swapChain1 = nullptr;
        if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
            return false;
        if (dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
            return false;
        if (swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
            return false;
        swapChain1->Release();
        dxgiFactory->Release();
        g_pSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
        g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->SetFullscreenState(false, nullptr); g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_hSwapChainWaitableObject != nullptr) { CloseHandle(g_hSwapChainWaitableObject); }
    for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        if (g_frameContext[i].CommandAllocator) { g_frameContext[i].CommandAllocator->Release(); g_frameContext[i].CommandAllocator = nullptr; }
    if (g_pd3dCommandQueue) { g_pd3dCommandQueue->Release(); g_pd3dCommandQueue = nullptr; }
    if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
    if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = nullptr; }
    if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = nullptr; }
    if (g_fence) { g_fence->Release(); g_fence = nullptr; }
    if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* pDebug = nullptr;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
    {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
        pDebug->Release();
    }
#endif
}

void CreateRenderTarget()
{
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource* pBackBuffer = nullptr;
        g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_mainRenderTargetDescriptor[i]);
        g_mainRenderTargetResource[i] = pBackBuffer;
    }
}

void CleanupRenderTarget()
{
    WaitForLastSubmittedFrame();

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = nullptr; }
}

void WaitForLastSubmittedFrame()
{
    FrameContext* frameCtx = &g_frameContext[g_frameIndex % NUM_FRAMES_IN_FLIGHT];

    UINT64 fenceValue = frameCtx->FenceValue;
    if (fenceValue == 0)
        return; // No fence was signaled

    frameCtx->FenceValue = 0;
    if (g_fence->GetCompletedValue() >= fenceValue)
        return;

    g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
    WaitForSingleObject(g_fenceEvent, INFINITE);
}

FrameContext* WaitForNextFrameResources()
{
    UINT nextFrameIndex = g_frameIndex + 1;
    g_frameIndex = nextFrameIndex;

    HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, nullptr };
    DWORD numWaitableObjects = 1;

    FrameContext* frameCtx = &g_frameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
    UINT64 fenceValue = frameCtx->FenceValue;
    if (fenceValue != 0) // means no fence was signaled
    {
        frameCtx->FenceValue = 0;
        g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
        waitableObjects[1] = g_fenceEvent;
        numWaitableObjects = 2;
    }

    WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

    return frameCtx;
}

void LinkedText(bool active, char text[])
{
    if (active)
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), text);
    else
        ImGui::TextDisabled(text);
}

string serialCommTasks()
{
    string output = "";
    if (passedSerialParams & SEND_ENABLE)
        payloadReleaseEnable();
    if (passedSerialParams & SEND_DISABLE)
        payloadReleaseDisable();
    if (passedSerialParams & READ_SERIAL)
        output = GetSerialData();

    return output;
}

string GetSerialData()
{
    int reply_wait_time = 1;
    string syntax_type = "json";

    return Serial.ReadSerialPort(reply_wait_time, syntax_type);
}

void payloadReleaseEnable()
{
    char data = 'r';
    Serial.WriteSerialPort(&data);
}

void payloadReleaseDisable()
{
    char data = 'u';
    Serial.WriteSerialPort(&data);
}

float vecMag(float a, float b, float c)
{
    return sqrt(a * a + b * b + c * c);
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            WaitForLastSubmittedFrame();
            CleanupRenderTarget();
            HRESULT result = g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
            assert(SUCCEEDED(result) && "Failed to resize swapchain.");
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
