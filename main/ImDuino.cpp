#include "SPI.h"
#include <TFT_22_ILI9225.h>
#include <imgui.h>
#include <examples/imgui_impl_softraster.h>
#include <stdio.h>

extern "C" {
#include <ps3.h>
}

#include "esp32-hal-bt.h"

const static uint8_t TFTLED = 32;
const static uint8_t TFTRST = 33;
const static uint8_t TFTRS  = 27;
const static uint8_t TFTCS  = 15;
const static uint8_t TFTCLK = 14;
const static uint8_t TFTSDI = 13;

// Use a tool such as "SixasixPairTool" to set the PS3 controllers MAC adress
uint8_t ps3PairedMacAddress[6] = {1, 2, 3, 4, 5, 6};

// #define TFTX 220
// #define TFTY 176
#define TFTX 220
#define TFTY 100

texture_color16_t screen;
texture_alpha8_t fontAtlas;
texture_color16_t image;

TFT_22_ILI9225 tft = TFT_22_ILI9225(TFTRST, TFTRS, TFTCS, TFTLED, 32);
SPIClass tftspi(HSPI);

unsigned long drawTime;
unsigned long renderTime;
unsigned long rasterTime;

ImGuiContext *context;

uint8_t stick_deadzone = 10;

int in_deadzone(int8_t value, uint8_t deadzone)
{
    return value == deadzone ||
        (value > 0 && value < deadzone) ||
        (value < 0 && value > -deadzone);
}

void controller_event_cb(ps3_t ps3, ps3_event_t event)
{
    ImGuiIO& io = ImGui::GetIO();

    // io.MousePos = mouse_pos;
    // io.MouseDown[0] = mouse_button_0;
    // io.MouseDown[1] = mouse_button_1;

    const float deadzone = 20.0f;

    /* [0.0f - 1.0f] */
    io.NavInputs[ImGuiNavInput_Activate]    = ps3.button.cross    > 0 ? 1.0f : 0.0f; // activate / open / toggle / tweak value       // e.g. Cross  (PS4), A (Xbox), B (Switch), Space (Keyboard)
    io.NavInputs[ImGuiNavInput_Cancel]      = ps3.button.circle   > 0 ? 1.0f : 0.0f; // cancel / close / exit                        // e.g. Circle (PS4), B (Xbox), A (Switch), Escape (Keyboard)
    io.NavInputs[ImGuiNavInput_Input]       = ps3.button.triangle > 0 ? 1.0f : 0.0f; // text input / on-screen keyboard              // e.g. Triang.(PS4), Y (Xbox), X (Switch), Return (Keyboard)
    io.NavInputs[ImGuiNavInput_Menu]        = ps3.button.square   > 0 ? 1.0f : 0.0f; // tap: toggle menu / hold: focus, move, resize // e.g. Square (PS4), X (Xbox), Y (Switch), Alt (Keyboard)
    io.NavInputs[ImGuiNavInput_DpadLeft]    = ps3.button.left     > 0 ? 1.0f : 0.0f; // move / tweak / resize window (w/ PadMenu)    // e.g. D-pad Left/Right/Up/Down (Gamepads), Arrow keys (Keyboard)
    io.NavInputs[ImGuiNavInput_DpadRight]   = ps3.button.right    > 0 ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_DpadUp]      = ps3.button.up       > 0 ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_DpadDown]    = ps3.button.down     > 0 ? 1.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_TweakSlow]   = ps3.analog.button.l2 / 255.0f; // slower tweaks                                // e.g. L1 or L2 (PS4), LB or LT (Xbox), L or ZL (Switch)
    io.NavInputs[ImGuiNavInput_TweakFast]   = ps3.analog.button.r2 / 255.0f; // faster tweaks                                // e.g. R1 or R2 (PS4), RB or RT (Xbox), R or ZL (Switch)
    io.NavInputs[ImGuiNavInput_LStickUp]    =  ps3.analog.stick.ly > deadzone ? ps3.analog.stick.ly / 125.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_LStickDown]  = -ps3.analog.stick.ly > deadzone ? ps3.analog.stick.ly / 125.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_LStickLeft]  =  ps3.analog.stick.lx > deadzone ? ps3.analog.stick.lx / 125.0f : 0.0f;
    io.NavInputs[ImGuiNavInput_LStickRight] = -ps3.analog.stick.lx > deadzone ? ps3.analog.stick.lx / 125.0f : 0.0f;
}

color16_t image_pixels[6 * 6] = {
    {0xFFFFU}, {0x0000U}, {0xFFFFU}, {0x0000U}, {0xFFFFU}, {0x0000U},
    {0x0000U}, {0xFFFFU}, {0x0000U}, {0xFFFFU}, {0x0000U}, {0xFFFFU},
    {0xFFFFU}, {0x0000U}, {0xFFFFU}, {0x0000U}, {0xFFFFU}, {0x0000U},
    {0x0000U}, {0xFFFFU}, {0x0000U}, {0xFFFFU}, {0x0000U}, {0xFFFFU},
    {0xFFFFU}, {0x0000U}, {0xFFFFU}, {0x0000U}, {0xFFFFU}, {0x0000U},
    {0x0000U}, {0xFFFFU}, {0x0000U}, {0xFFFFU}, {0x0000U}, {0xFFFFU},
};

void setup()
{
    Serial.begin(115200);

    tft.begin(tftspi);
    tft.setFont(Terminal6x8);
    tft.setOrientation(3);
    digitalWrite(TFTLED, HIGH);

    context = ImGui::CreateContext();

    ImGui_ImplSoftraster_Init(&screen);

    ImGuiStyle& style = ImGui::GetStyle();
    style.AntiAliasedLines = false;
    style.AntiAliasedFill = false;
    style.WindowRounding = 0.0f;

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight | ImFontAtlasFlags_NoMouseCursors;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    screen.init(TFTX, TFTY);

    uint8_t* pixels;
    int width, height;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);
    fontAtlas.init(width, height, (alpha8_t*)pixels);
    io.Fonts->TexID = &fontAtlas;

    image.init(6, 6, image_pixels);

    texture_color16_t::bad = color32_t(255, 0, 0, 255);
}

float f = 0.0f;
unsigned long t = 0;
int is_ps3_enabled = 0;
bool updating = true;

void loop()
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f/60.0f;

    ImGui_ImplSoftraster_NewFrame();
    ImGui::NewFrame();
    ImGui::SetWindowPos(ImVec2(0.0, 0.0));
    ImGui::SetWindowSize(ImVec2(screen.w, screen.h));

    if (updating)
    {
        f += 0.05;
        if(f > 1.0f) f = 0.0f;
    }

    unsigned int deltaTime = millis() - t;
    t += deltaTime;

    deltaTime -= (drawTime + renderTime + rasterTime);

    // ImGui::Text("SPI screen draw time %d ms", (int)drawTime);
    // ImGui::Text("Render time %d ms", (int)renderTime);
    ImGui::Text("Raster time %d ms", (int)rasterTime);
    // ImGui::Text("Remaining time %d ms", deltaTime);

    ImGui::Checkbox("Update Slider", &updating);
    ImGui::SameLine();
    ImGui::SliderFloat("##SliderFloat", &f, 0.0f, 1.0f);

    ImGui::Text("MAC: %X:%X:%X:%X:%X:%X",
                (int)ps3PairedMacAddress[0],
                (int)ps3PairedMacAddress[1],
                (int)ps3PairedMacAddress[2],
                (int)ps3PairedMacAddress[3],
                (int)ps3PairedMacAddress[4],
                (int)ps3PairedMacAddress[5]);

    ImGui::Image(&image, ImVec2(image.w, image.h));
    ImGui::SameLine();
    ImGui::Image(&image, ImVec2(image.w, image.h));
    ImGui::SameLine();
    ImGui::Image(&image, ImVec2(image.w, image.h));
    ImGui::SameLine();
    ImGui::Image(&image, ImVec2(image.w, image.h));

    renderTime = millis();
    ImGui::Render();
    renderTime = millis() - renderTime;

    rasterTime = millis();
    ImGui_ImplSoftraster_RenderDrawData(ImGui::GetDrawData());
    rasterTime = millis() - rasterTime;

    drawTime = millis();
    tft.drawBitmap(0, 0, (uint16_t*)screen.pixels, screen.w, screen.h);
    drawTime = millis() - drawTime;

    if (ps3IsConnected())
    {
        if (!is_ps3_enabled)
        {
            printf("Connected!\n");
            ps3Enable();
            is_ps3_enabled = 1;
        }
    }
    else
    {
        if (is_ps3_enabled) is_ps3_enabled = 0;
    }
}

#include <Arduino.h>
#include <esp_bt.h>

extern "C" void app_main()
{
    // Have to call something from esp32-hal-bt.c otherwise btInUse() returns
    // false, causing initArduino() to free the memory for bluetooth
    btStarted();

    initArduino();
    setup();

    ps3SetBluetoothMacAddress(ps3PairedMacAddress);
    ps3SetEventCallback(controller_event_cb);
    ps3Init();

    for (;;)
    {
        loop();
        vTaskDelay(1);
    }
}

#include <examples/imgui_impl_softraster.cpp>