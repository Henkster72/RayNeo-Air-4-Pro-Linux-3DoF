#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <string>
#include <utility>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>

#include "rayneo_api.h"

#include <SDL.h>
#include <SDL_opengl.h>

struct Quaternion
{
    float w{1}, x{0}, y{0}, z{0};
};

struct Orientation
{
    float yaw{0}, pitch{0}, roll{0};
};

struct DesktopImage
{
    int width{0};
    int height{0};
    std::vector<uint8_t> pixels;
};

static uint16_t readHexEnvironment(const char *name, uint16_t fallback)
{
    const char *text = std::getenv(name);
    if (!text || !*text)
        return fallback;
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 16);
    if (end == text || *end != '\0' || value > 0xfffful)
    {
        std::printf("Invalid %s=%s; using default %04x\n", name, text, fallback);
        return fallback;
    }
    return static_cast<uint16_t>(value);
}

static bool cropDisplay(const DesktopImage &full, const SDL_Rect &bounds,
                        int virtualLeft, int virtualTop, DesktopImage &cropped)
{
    const int x = bounds.x - virtualLeft;
    const int y = bounds.y - virtualTop;
    if (x < 0 || y < 0 || bounds.w <= 0 || bounds.h <= 0 ||
        x + bounds.w > full.width || y + bounds.h > full.height)
        return false;

    cropped.width = bounds.w;
    cropped.height = bounds.h;
    cropped.pixels.resize(static_cast<size_t>(bounds.w) * bounds.h * 4);
    for (int row = 0; row < bounds.h; ++row)
    {
        const uint8_t *source = full.pixels.data() +
            (static_cast<size_t>(y + row) * full.width + x) * 4;
        uint8_t *destination = cropped.pixels.data() +
            static_cast<size_t>(row) * bounds.w * 4;
        std::memcpy(destination, source, static_cast<size_t>(bounds.w) * 4);
    }
    return true;
}

static uint16_t readLe16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t readLe32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static volatile std::sig_atomic_t stopRequested = 0;

static void handleStopSignal(int)
{
    stopRequested = 1;
}

static bool runSpectacle(const char *scope, const char *path)
{
    const pid_t child = fork();
    if (child < 0)
        return false;
    if (child == 0)
    {
        execlp("spectacle", "spectacle", "--background", "--nonotify",
               scope, "--output", path, static_cast<char *>(nullptr));
        _exit(127);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0)
    {
        if (errno != EINTR)
            return false;
        if (stopRequested)
            kill(child, SIGTERM);
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool switchKdeDesktop(int desktop)
{
    const std::string desktopNumber = std::to_string(desktop);
    const pid_t child = fork();
    if (child < 0)
        return false;
    if (child == 0)
    {
        execlp("qdbus6", "qdbus6", "org.kde.KWin", "/KWin",
               "setCurrentDesktop", desktopNumber.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0)
    {
        if (errno != EINTR)
            return false;
        if (stopRequested)
            kill(child, SIGTERM);
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool captureDesktop(DesktopImage &image, const char *scope)
{
    static std::atomic<unsigned long> sequence{0};
    char path[160];
    std::snprintf(path, sizeof(path), "/tmp/rayneo-pinned-%ld-%lu.bmp",
                  static_cast<long>(getpid()), sequence.fetch_add(1));
    if (!runSpectacle(scope, path))
    {
        if (!stopRequested)
            std::printf("Desktop capture failed; is spectacle installed?\n");
        return false;
    }

    FILE *file = std::fopen(path, "rb");
    if (!file)
    {
        std::printf("Desktop capture did not produce %s\n", path);
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const long fileSize = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (fileSize < 54)
    {
        std::fclose(file);
        std::printf("Desktop capture is too small\n");
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    const size_t readSize = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    unlink(path);
    if (readSize != bytes.size() || bytes[0] != 'B' || bytes[1] != 'M')
    {
        std::printf("Desktop capture is not a BMP image\n");
        return false;
    }

    const uint32_t pixelOffset = readLe32(bytes.data() + 10);
    const int32_t width = static_cast<int32_t>(readLe32(bytes.data() + 18));
    const int32_t storedHeight = static_cast<int32_t>(readLe32(bytes.data() + 22));
    const uint16_t planes = readLe16(bytes.data() + 26);
    const uint16_t bitsPerPixel = readLe16(bytes.data() + 28);
    const uint32_t compression = readLe32(bytes.data() + 30);
    if (width <= 0 || storedHeight == 0 || planes != 1 || compression != 0 ||
        (bitsPerPixel != 24 && bitsPerPixel != 32))
    {
        std::printf("Unsupported desktop BMP format\n");
        return false;
    }

    const int height = storedHeight < 0 ? -storedHeight : storedHeight;
    const size_t rowBytes = ((static_cast<size_t>(width) * bitsPerPixel + 31) / 32) * 4;
    if (pixelOffset >= bytes.size() || rowBytes > bytes.size() / static_cast<size_t>(height) ||
        pixelOffset + rowBytes * static_cast<size_t>(height) > bytes.size())
    {
        std::printf("Desktop BMP pixel data is truncated\n");
        return false;
    }

    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
    {
        const int sourceY = storedHeight < 0 ? y : height - 1 - y;
        const uint8_t *source = bytes.data() + pixelOffset + rowBytes * static_cast<size_t>(sourceY);
        uint8_t *destination = image.pixels.data() + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x)
        {
            const uint8_t *pixel = source + static_cast<size_t>(x) * bitsPerPixel / 8;
            destination[x * 4 + 0] = pixel[2];
            destination[x * 4 + 1] = pixel[1];
            destination[x * 4 + 2] = pixel[0];
            destination[x * 4 + 3] = 255;
        }
    }
    return true;
}

struct LiveCapture
{
    std::mutex mutex;
    DesktopImage pending;
    bool ready{false};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> frames{0};
};

static Quaternion multiply(const Quaternion &a, const Quaternion &b)
{
    return {
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

static void normalize(Quaternion &q)
{
    float length = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (length > 1e-6f)
    {
        q.w /= length;
        q.x /= length;
        q.y /= length;
        q.z /= length;
    }
}

static void updateEuler(const Quaternion &q, Orientation &o)
{
    const float r02 = 2.0f * (q.x * q.z + q.w * q.y);
    const float r10 = 2.0f * (q.x * q.y + q.w * q.z);
    const float r11 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    const float r12 = 2.0f * (q.y * q.z - q.w * q.x);
    const float r22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    o.yaw = std::atan2(r02, r22);
    o.pitch = std::asin(std::max(-1.0f, std::min(1.0f, -r12)));
    o.roll = std::atan2(r10, r11);
}

class Tracker
{
public:
    void recenter()
    {
        calibrated_ = false;
        calibrationSamples_ = 0;
        lastTick_ = 0;
        stillSamples_ = 0;
        bias_[0] = bias_[1] = bias_[2] = 0;
        q_ = Quaternion{};
        orientation_ = Orientation{};
        std::printf("Recenter started; hold still for 1 second\n");
    }

    bool update(const RAYNEO_ImuSample &s)
    {
        const float rawMagnitude = std::sqrt(
            s.gyroDps[0] * s.gyroDps[0] +
            s.gyroDps[1] * s.gyroDps[1] +
            s.gyroDps[2] * s.gyroDps[2]);

        if (!calibrated_)
        {
            if (rawMagnitude >= kStationaryDps)
            {
                calibrationSamples_ = 0;
                bias_[0] = bias_[1] = bias_[2] = 0;
                lastTick_ = s.tick;
                return false;
            }
            for (int i = 0; i < 3; ++i)
                bias_[i] = (bias_[i] * calibrationSamples_ + s.gyroDps[i]) / (calibrationSamples_ + 1);
            ++calibrationSamples_;
            lastTick_ = s.tick;
            if (calibrationSamples_ >= 500)
            {
                calibrated_ = true;
                q_ = Quaternion{};
                orientation_ = Orientation{};
                std::printf("Gyro calibrated; viewport tracking enabled\n");
            }
            return false;
        }

        float dt = 0.002f;
        if (lastTick_ != 0 && s.tick > lastTick_)
        {
            const float tickDt = (s.tick - lastTick_) * 0.0001f;
            if (tickDt > 0.0001f && tickDt < 0.1f)
                dt = tickDt;
        }
        lastTick_ = s.tick;

        if (rawMagnitude < kStationaryDps)
        {
            ++stillSamples_;
            if (stillSamples_ > 250)
            {
                for (int i = 0; i < 3; ++i)
                    bias_[i] = bias_[i] * 0.999f + s.gyroDps[i] * 0.001f;
            }
        }
        else
        {
            stillSamples_ = 0;
        }

        float rate[3];
        for (int i = 0; i < 3; ++i)
        {
            rate[i] = s.gyroDps[i] - bias_[i];
            if (stillSamples_ > 250 || std::fabs(rate[i]) < kDeadzoneDps)
                rate[i] = 0;
            rate[i] *= 0.0174532925f;
        }

        q_ = multiply(q_, Quaternion{1.0f, 0.5f * rate[0] * dt,
                                      0.5f * rate[1] * dt,
                                      0.5f * rate[2] * dt});
        normalize(q_);
        updateEuler(q_, orientation_);
        return true;
    }

    const Orientation &orientation() const { return orientation_; }
    bool calibrated() const { return calibrated_; }

private:
    static constexpr float kStationaryDps = 4.0f;
    static constexpr float kDeadzoneDps = 1.0f;
    bool calibrated_{false};
    int calibrationSamples_{0};
    uint32_t lastTick_{0};
    int stillSamples_{0};
    float bias_[3]{0, 0, 0};
    Quaternion q_{};
    Orientation orientation_{};
};

static void drawDesktop(GLuint texture, float desktopX, float extraWidth,
                        float desktopY, float desktopWidth, float desktopHeight)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    // Extra virtual monitor: reuse the captured source region on the left.
    const float extraTextureWidth = extraWidth / desktopWidth;
    glTexCoord2f(0, 0); glVertex2f(0, desktopY);
    glTexCoord2f(extraTextureWidth, 0); glVertex2f(extraWidth, desktopY);
    glTexCoord2f(extraTextureWidth, 1); glVertex2f(extraWidth, desktopY + desktopHeight);
    glTexCoord2f(0, 1); glVertex2f(0, desktopY + desktopHeight);

    // Captured desktop: source monitor followed by the existing right-hand region.
    glTexCoord2f(0, 0); glVertex2f(desktopX, desktopY);
    glTexCoord2f(1, 0); glVertex2f(desktopX + desktopWidth, desktopY);
    glTexCoord2f(1, 1); glVertex2f(desktopX + desktopWidth, desktopY + desktopHeight);
    glTexCoord2f(0, 1); glVertex2f(desktopX, desktopY + desktopHeight);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

static void drawCapturedScreen(GLuint texture, float sourceWidth, float textureWidth,
                               float screenWidth, float screenHeight)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor3f(1, 1, 1);
    const float u = sourceWidth / textureWidth;
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(u, 0); glVertex2f(screenWidth, 0);
    glTexCoord2f(u, 1); glVertex2f(screenWidth, screenHeight);
    glTexCoord2f(0, 1); glVertex2f(0, screenHeight);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

static void drawHeadTrackingPanels(float width, float height)
{
    const float colors[3][3][3] = {
        {{0.18f, 0.28f, 0.55f}, {0.30f, 0.18f, 0.45f}, {0.18f, 0.45f, 0.35f}},
        {{0.55f, 0.28f, 0.12f}, {0.08f, 0.12f, 0.20f}, {0.15f, 0.48f, 0.25f}},
        {{0.42f, 0.18f, 0.38f}, {0.25f, 0.42f, 0.18f}, {0.50f, 0.30f, 0.12f}}};

    glBegin(GL_QUADS);
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            const float x = column * width;
            const float y = row * height;
            glColor3f(colors[row][column][0], colors[row][column][1], colors[row][column][2]);
            glVertex2f(x, y);
            glVertex2f(x + width, y);
            glVertex2f(x + width, y + height);
            glVertex2f(x, y + height);
        }
    }
    glEnd();

    glLineWidth(6.0f);
    glColor3f(0.95f, 0.95f, 0.95f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(width, height);
    glVertex2f(width * 2.0f, height);
    glVertex2f(width * 2.0f, height * 2.0f);
    glVertex2f(width, height * 2.0f);
    glEnd();
    glLineWidth(1.0f);
}

static GLuint uploadDesktop(const DesktopImage &image)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    return texture;
}

int main(int argc, char **argv)
{
    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);

    bool kdeWorkspaceMode = false;
    bool kdeTwoWorkspaceMode = false;
    bool pinnedSourceMode = false;
    bool headTrackingDemoMode = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--kde-workspaces") == 0)
            kdeWorkspaceMode = true;
        else if (std::strcmp(argv[i], "--kde-two-workspaces") == 0)
            kdeTwoWorkspaceMode = true;
        else if (std::strcmp(argv[i], "--pinned-source") == 0)
            pinnedSourceMode = true;
        else if (std::strcmp(argv[i], "--headtracking") == 0)
            headTrackingDemoMode = true;
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            std::printf("Usage: %s [--headtracking|--pinned-source|--kde-two-workspaces|--kde-workspaces]\n", argv[0]);
            std::printf("  --headtracking        render immediate synthetic panels; do not capture the desktop\n");
            std::printf("  --pinned-source       track a cropped source monitor without switching KDE desktops\n");
            std::printf("  --kde-two-workspaces  switch Center/Right KDE workspaces\n");
            std::printf("  --kde-workspaces       switch the advanced 3x3 KDE grid\n");
            return 0;
        }
    }

    if (kdeTwoWorkspaceMode)
        kdeWorkspaceMode = false;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::printf("SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    int targetDisplay = 0;
    const int displayCount = SDL_GetNumVideoDisplays();
    std::printf("Displays: %d\n", displayCount);
    std::vector<SDL_Rect> displayBounds(static_cast<size_t>(std::max(0, displayCount)));
    int virtualLeft = 0;
    int virtualTop = 0;
    bool haveVirtualBounds = false;
    const char *displaySelector = std::getenv("RAYNEO_DISPLAY");
    bool displaySelected = false;
    for (int i = 0; i < displayCount; ++i)
    {
        const char *name = SDL_GetDisplayName(i);
        std::printf("  %d: %s\n", i, name ? name : "(unnamed)");
        SDL_GetDisplayBounds(i, &displayBounds[static_cast<size_t>(i)]);
        const SDL_Rect &bounds = displayBounds[static_cast<size_t>(i)];
        bool matchesTarget = false;
        if (!haveVirtualBounds)
        {
            virtualLeft = bounds.x;
            virtualTop = bounds.y;
            haveVirtualBounds = true;
        }
        else
        {
            virtualLeft = std::min(virtualLeft, bounds.x);
            virtualTop = std::min(virtualTop, bounds.y);
        }
        if (displaySelector && *displaySelector)
        {
            char *end = nullptr;
            const long selectedIndex = std::strtol(displaySelector, &end, 10);
            if (end != displaySelector && *end == '\0' && selectedIndex == i)
                matchesTarget = true;
            else if (name && std::strstr(name, displaySelector) != nullptr)
                matchesTarget = true;
        }
        if ((!displaySelector || !*displaySelector) && name &&
            (std::strstr(name, "SmartGlasses") != nullptr ||
                     std::strstr(name, "RayNeo") != nullptr))
            matchesTarget = true;
        if (matchesTarget)
        {
            targetDisplay = i;
            displaySelected = true;
        }
    }
    if (!displaySelected && displayCount > 1)
    {
        targetDisplay = displayCount - 1;
        std::printf("No RayNeo output name matched; using display %d. Set RAYNEO_DISPLAY to an index or name if needed.\n",
                    targetDisplay);
    }
    std::printf("Target display: %d: %s\n", targetDisplay,
                SDL_GetDisplayName(targetDisplay));

    int sourceDisplay = -1;
    for (int i = 0; i < displayCount; ++i)
    {
        if (i != targetDisplay)
        {
            sourceDisplay = i;
            break;
        }
    }
    if (!headTrackingDemoMode &&
        (kdeWorkspaceMode || kdeTwoWorkspaceMode || pinnedSourceMode) && sourceDisplay < 0)
    {
        std::printf("A separate source monitor is required for KDE workspace mode\n");
        SDL_Quit();
        return 1;
    }
    const bool cropSourceDisplay = !headTrackingDemoMode &&
                                   (pinnedSourceMode || kdeWorkspaceMode || kdeTwoWorkspaceMode);
    const char *sourceName = sourceDisplay >= 0 ?
        SDL_GetDisplayName(sourceDisplay) : "full desktop";
    if (cropSourceDisplay)
        std::printf("Source display: %d: %s\n", sourceDisplay, sourceName ? sourceName : "(unnamed)");

    DesktopImage desktop;
    bool liveUpdatesSourceRegion = false;
    int liveUpdateX = 0;
    int liveUpdateY = 0;
    if (headTrackingDemoMode)
    {
        std::printf("Head-tracking demo mode enabled; desktop capture is disabled\n");
    }
    else
    {
        DesktopImage captured;
        if (!captureDesktop(captured, "--fullscreen"))
        {
            SDL_Quit();
            return 1;
        }
        std::printf("Captured combined desktop framebuffer: %dx%d\n",
                    captured.width, captured.height);
        if (pinnedSourceMode)
        {
            // Keep the initial RayNeo-side image as a stable snapshot. The
            // viewport will cover that output once it starts, so refreshing
            // the full desktop would capture the viewport recursively.
            desktop = std::move(captured);
            liveUpdatesSourceRegion = true;
            liveUpdateX = displayBounds[static_cast<size_t>(sourceDisplay)].x - virtualLeft;
            liveUpdateY = displayBounds[static_cast<size_t>(sourceDisplay)].y - virtualTop;
            std::printf("Using combined desktop snapshot: %dx%d\n",
                        desktop.width, desktop.height);
        }
        else if (cropSourceDisplay)
        {
            if (!cropDisplay(captured, displayBounds[static_cast<size_t>(sourceDisplay)],
                             virtualLeft, virtualTop, desktop))
            {
                std::printf("Could not extract the source monitor from the desktop capture\n");
                SDL_Quit();
                return 1;
            }
            std::printf("Using source monitor image: %dx%d\n", desktop.width, desktop.height);
        }
        else
            desktop = std::move(captured);
    }

    SDL_Rect bounds = displayBounds[static_cast<size_t>(targetDisplay)];
    SDL_GetDisplayBounds(targetDisplay, &bounds);
    std::string windowTitle = headTrackingDemoMode ?
        "RayNeo head-tracking demo" : "RayNeo pinned viewport";
    if (const char *targetName = SDL_GetDisplayName(targetDisplay))
        windowTitle += " - " + std::string(targetName);
    SDL_Window *window = SDL_CreateWindow(
        windowTitle.c_str(), bounds.x, bounds.y, bounds.w, bounds.h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);
    if (!window)
    {
        std::printf("Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowPosition(window, bounds.x, bounds.y);
    SDL_ShowWindow(window);
    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    if (!glctx)
    {
        std::printf("OpenGL context failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    RAYNEO_Context ctx{};
    if (Rayneo_Create(&ctx) != RAYNEO_OK ||
        Rayneo_SetTargetVidPid(ctx,
                               readHexEnvironment("RAYNEO_VID", 0x1BBB),
                               readHexEnvironment("RAYNEO_PID", 0xAF50)) != RAYNEO_OK ||
        Rayneo_Start(ctx, 0) != RAYNEO_OK ||
        Rayneo_EnableImu(ctx) != RAYNEO_OK)
    {
        std::printf("RayNeo startup failed\n");
        if (ctx)
            Rayneo_Destroy(ctx);
        SDL_GL_DeleteContext(glctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    GLuint desktopTexture = headTrackingDemoMode ? 0 : uploadDesktop(desktop);

    LiveCapture liveCapture;
    std::thread liveCaptureThread;
    if (!headTrackingDemoMode)
    {
        liveCaptureThread = std::thread([&]() {
            std::printf("Live desktop capture started; updating the %s region\n", sourceName);
            while (!liveCapture.stop.load())
            {
                DesktopImage frame;
                if (captureDesktop(frame, "--fullscreen"))
                {
                    if (cropSourceDisplay)
                    {
                        DesktopImage sourceFrame;
                        if (!cropDisplay(frame, displayBounds[static_cast<size_t>(sourceDisplay)],
                                         virtualLeft, virtualTop, sourceFrame))
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            continue;
                        }
                        frame = std::move(sourceFrame);
                    }
                    std::lock_guard<std::mutex> lock(liveCapture.mutex);
                    liveCapture.pending = std::move(frame);
                    liveCapture.ready = true;
                    ++liveCapture.frames;
                }
                else
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    auto applyLiveFrame = [&](const DesktopImage &frame) {
        if ((liveUpdatesSourceRegion &&
             (frame.width != displayBounds[static_cast<size_t>(sourceDisplay)].w ||
              frame.height != displayBounds[static_cast<size_t>(sourceDisplay)].h)) ||
            (!liveUpdatesSourceRegion && cropSourceDisplay &&
             (frame.width != desktop.width || frame.height != desktop.height)) ||
            (!cropSourceDisplay && (frame.width < width || frame.height != height ||
                                    desktop.width < width || desktop.height < height)))
            return;
        const int copyWidth = liveUpdatesSourceRegion ? frame.width :
                              (cropSourceDisplay ? desktop.width : width);
        const int copyHeight = liveUpdatesSourceRegion ? frame.height :
                               (cropSourceDisplay ? desktop.height : height);
        const int destinationX = liveUpdatesSourceRegion ? liveUpdateX : 0;
        const int destinationY = liveUpdatesSourceRegion ? liveUpdateY : 0;
        if (destinationX < 0 || destinationY < 0 ||
            destinationX + copyWidth > desktop.width ||
            destinationY + copyHeight > desktop.height)
            return;
        for (int y = 0; y < copyHeight; ++y)
        {
            std::memcpy(desktop.pixels.data() +
                            (static_cast<size_t>(destinationY + y) * desktop.width + destinationX) * 4,
                        frame.pixels.data() + static_cast<size_t>(y) * frame.width * 4,
                        static_cast<size_t>(copyWidth) * 4);
        }
        glBindTexture(GL_TEXTURE_2D, desktopTexture);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, desktop.width);
        glTexSubImage2D(GL_TEXTURE_2D, 0, destinationX, destinationY, copyWidth, copyHeight,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        desktop.pixels.data() +
                            (static_cast<size_t>(destinationY) * desktop.width + destinationX) * 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    };

    const float extraMonitorWidth = static_cast<float>(width);
    const float desktopX = extraMonitorWidth;
    const float canvasWidth = headTrackingDemoMode ? static_cast<float>(width) * 3.0f :
                               (pinnedSourceMode ? static_cast<float>(desktop.width) :
                                desktopX + static_cast<float>(desktop.width));
    const float canvasHeight = static_cast<float>(height) * 3.0f;
    const float desktopY = headTrackingDemoMode ? static_cast<float>(height) :
                            (canvasHeight - desktop.height) * 0.5f;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    Tracker tracker;
    const char *centerDesktopText = std::getenv("RAYNEO_KDE_CENTER_DESKTOP");
    const int centerDesktop = centerDesktopText ? std::atoi(centerDesktopText) :
                               (kdeTwoWorkspaceMode ? 1 : 5);
    int workspaceSlot = 0;
    if (kdeWorkspaceMode)
    {
        std::printf("KDE workspace mode enabled; expected 3x3 grid with center desktop %d\n",
                    centerDesktop);
        std::printf("  center slot=0; left/right=desktop %d/%d; above/below=desktop %d/%d\n",
                    centerDesktop - 1, centerDesktop + 1,
                    centerDesktop - 3, centerDesktop + 3);
    }
    if (kdeTwoWorkspaceMode)
        std::printf("KDE two-workspace mode enabled; center desktop %d, right desktop %d\n",
                    centerDesktop, centerDesktop + 1);
    if (pinnedSourceMode)
        std::printf("Pinned source-monitor mode enabled; KDE desktops will not be switched\n");
    bool running = true;
    uint64_t samples = 0;
    double lastReport = 0;
    uint32_t lastWorkspaceAttempt = 0;
    uint32_t workspaceCooldownUntil = 0;
    uint32_t pendingWorkspaceSince = 0;
    int pendingWorkspaceSlot = -1;
    int failedWorkspaceSlot = -1;
    while (running && !stopRequested)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
                running = false;
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_r)
                tracker.recenter();
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_c)
            {
                DesktopImage refreshed;
                if (!headTrackingDemoMode && captureDesktop(refreshed, "--fullscreen"))
                {
                    if (cropSourceDisplay)
                    {
                        DesktopImage sourceFrame;
                        if (!cropDisplay(refreshed,
                                         displayBounds[static_cast<size_t>(sourceDisplay)],
                                         virtualLeft, virtualTop, sourceFrame))
                            continue;
                        refreshed = std::move(sourceFrame);
                    }
                    applyLiveFrame(refreshed);
                    std::printf("Live desktop frame refreshed\n");
                }
            }
        }

        DesktopImage liveFrame;
        {
            std::lock_guard<std::mutex> lock(liveCapture.mutex);
            if (liveCapture.ready)
            {
                liveFrame = std::move(liveCapture.pending);
                liveCapture.ready = false;
            }
        }
        if (!liveFrame.pixels.empty())
            applyLiveFrame(liveFrame);

        RAYNEO_Event eventData{};
        while (Rayneo_PollEvent(ctx, &eventData, 0) == RAYNEO_OK)
        {
            if (eventData.type == RAYNEO_EVENT_IMU_SAMPLE)
            {
                ++samples;
                tracker.update(eventData.data.imu);
            }
            else if (eventData.type == RAYNEO_EVENT_DEVICE_DETACHED)
            {
                running = false;
            }
        }

        const float yaw = tracker.orientation().yaw;
        float viewX = 0;
        float viewY = 0;
        if (kdeTwoWorkspaceMode)
        {
            // The tested Air 4 Pro orientation reports a negative yaw when
            // the wearer turns right. Keep the physical layout intuitive:
            // looking right selects the Right KDE desktop.
            const float horizontal = yaw * 57.2958f;
            const float enterAngle = 18.0f;
            const float returnAngle = 10.0f;
            int desiredSlot = workspaceSlot;
            if (workspaceSlot == 0 && horizontal <= -enterAngle)
                desiredSlot = 1;
            else if (workspaceSlot == 1 && horizontal > -returnAngle)
                desiredSlot = 0;

            const uint32_t nowTicks = SDL_GetTicks();
            if (desiredSlot != workspaceSlot)
            {
                if (pendingWorkspaceSlot != desiredSlot)
                {
                    pendingWorkspaceSlot = desiredSlot;
                    pendingWorkspaceSince = nowTicks;
                }
                const bool stableLongEnough = nowTicks - pendingWorkspaceSince >= 150;
                const bool retryAllowed = desiredSlot != failedWorkspaceSlot ||
                                          nowTicks - lastWorkspaceAttempt >= 1000;
                const bool cooldownExpired = nowTicks >= workspaceCooldownUntil;
                if (stableLongEnough && retryAllowed && cooldownExpired)
                {
                    const int targetDesktop = centerDesktop + desiredSlot;
                    lastWorkspaceAttempt = nowTicks;
                    if ((targetDesktop == 1 || targetDesktop == 2) &&
                        switchKdeDesktop(targetDesktop))
                    {
                        workspaceSlot = desiredSlot;
                        pendingWorkspaceSlot = -1;
                        failedWorkspaceSlot = -1;
                        workspaceCooldownUntil = nowTicks + 350;
                        std::printf("KDE workspace switched to %s (desktop %d)\n",
                                    workspaceSlot == 0 ? "Center" : "Right", targetDesktop);
                    }
                    else if (!stopRequested)
                    {
                        failedWorkspaceSlot = desiredSlot;
                        std::printf("Could not switch to KDE desktop %d; run tools/setup-kde-two-workspaces.sh first\n",
                                    targetDesktop);
                    }
                }
            }
            else
                pendingWorkspaceSlot = -1;
        }
        else if (kdeWorkspaceMode)
        {
            // Keep the same axis signs as the tested pinned viewport. The
            // two head angles select one of the eight surrounding workspaces.
            const float horizontal = -yaw * 57.2958f;
            const float vertical = -tracker.orientation().pitch * 57.2958f;
            const float enterAngle = 18.0f;
            const float returnAngle = 10.0f;
            int desiredSlot = workspaceSlot;
            if (workspaceSlot == 0)
            {
                const bool horizontalActive = std::fabs(horizontal) >= enterAngle;
                const bool verticalActive = std::fabs(vertical) >= enterAngle;
                if (horizontalActive && verticalActive)
                {
                    if (horizontal > 0)
                        desiredSlot = vertical > 0 ? 7 : 5;
                    else
                        desiredSlot = vertical > 0 ? 8 : 6;
                }
                else if (horizontalActive && std::fabs(horizontal) >= std::fabs(vertical))
                    desiredSlot = horizontal > 0 ? 1 : 2;
                else if (verticalActive)
                    desiredSlot = vertical > 0 ? 4 : 3;
            }
            else if (std::fabs(horizontal) < returnAngle &&
                     std::fabs(vertical) < returnAngle)
            {
                desiredSlot = 0;
            }

            const uint32_t nowTicks = SDL_GetTicks();
            const bool retryAllowed = desiredSlot != failedWorkspaceSlot ||
                                      nowTicks - lastWorkspaceAttempt >= 1000;
            if (desiredSlot != workspaceSlot && retryAllowed)
            {
                static const int desktopOffsets[] = {0, -1, 1, -3, 3, -4, -2, 2, 4};
                const int targetDesktop = centerDesktop + desktopOffsets[desiredSlot];
                lastWorkspaceAttempt = nowTicks;
                if (targetDesktop >= 1 && targetDesktop <= 9 &&
                    switchKdeDesktop(targetDesktop))
                {
                    workspaceSlot = desiredSlot;
                    failedWorkspaceSlot = -1;
                    std::printf("KDE workspace switched to %d (desktop %d)\n",
                                workspaceSlot, targetDesktop);
                }
                else if (!stopRequested)
                {
                    failedWorkspaceSlot = desiredSlot;
                    std::printf("Could not switch to KDE desktop %d; run tools/setup-kde-workspace-grid.sh first\n",
                                targetDesktop);
                }
            }
        }
        else
        {
            const float maxYaw = 30.0f * 0.0174532925f;
            // A left head turn should reveal the left side of the anchored canvas.
            const float normalizedYaw = std::max(-1.0f, std::min(1.0f, -yaw / maxYaw));
            const float maxOffset = std::max(0.0f, canvasWidth - width);
            viewX = maxOffset * (0.5f + normalizedYaw * 0.5f);
            const float maxPitch = 20.0f * 0.0174532925f;
            const float normalizedPitch = std::max(-1.0f, std::min(1.0f, -tracker.orientation().pitch / maxPitch));
            const float maxOffsetY = std::max(0.0f, canvasHeight - height);
            viewY = maxOffsetY * (0.5f + normalizedPitch * 0.5f);
        }

        glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(-viewX, -viewY, 0);
        if (headTrackingDemoMode)
            drawHeadTrackingPanels(width, height);
        else if (kdeWorkspaceMode || kdeTwoWorkspaceMode)
            drawCapturedScreen(desktopTexture, static_cast<float>(width),
                               static_cast<float>(desktop.width),
                               static_cast<float>(width), static_cast<float>(height));
        else if (pinnedSourceMode)
        {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, desktopTexture);
            glColor3f(1, 1, 1);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex2f(0, desktopY);
            glTexCoord2f(1, 0); glVertex2f(static_cast<float>(desktop.width), desktopY);
            glTexCoord2f(1, 1); glVertex2f(static_cast<float>(desktop.width),
                                           desktopY + static_cast<float>(desktop.height));
            glTexCoord2f(0, 1); glVertex2f(0, desktopY + static_cast<float>(desktop.height));
            glEnd();
            glDisable(GL_TEXTURE_2D);
        }
        else
            drawDesktop(desktopTexture, desktopX, extraMonitorWidth,
                        desktopY, desktop.width, desktop.height);
        SDL_GL_SwapWindow(window);

        const double now = SDL_GetTicks() * 0.001;
        if (now - lastReport > 0.5)
        {
            const Orientation &o = tracker.orientation();
            std::printf("viewport samples=%llu live=%llu yaw=%.1f pitch=%.1f roll=%.1f viewX=%.0f viewY=%.0f calibrated=%s\n",
                        static_cast<unsigned long long>(samples),
                        static_cast<unsigned long long>(liveCapture.frames.load()),
                        o.yaw * 57.2958f, o.pitch * 57.2958f, o.roll * 57.2958f,
                        viewX, viewY, tracker.calibrated() ? "yes" : "no");
            lastReport = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    liveCapture.stop.store(true);
    if (liveCaptureThread.joinable())
        liveCaptureThread.join();
    if (kdeTwoWorkspaceMode && workspaceSlot != 0)
        switchKdeDesktop(centerDesktop);
    Rayneo_DisableImu(ctx);
    Rayneo_Destroy(ctx);
    if (desktopTexture != 0)
        glDeleteTextures(1, &desktopTexture);
    SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
