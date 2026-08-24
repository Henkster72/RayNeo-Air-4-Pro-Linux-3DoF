#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
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

static uint16_t readLe16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t readLe32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static bool captureDesktop(DesktopImage &image, const char *scope)
{
    static std::atomic<unsigned long> sequence{0};
    char path[160];
    std::snprintf(path, sizeof(path), "/tmp/rayneo-pinned-%ld-%lu.bmp",
                  static_cast<long>(getpid()), sequence.fetch_add(1));
    char command[320];
    std::snprintf(command, sizeof(command),
                  "spectacle --background --nonotify --pointer %s --output %s", scope, path);
    if (std::system(command) != 0)
    {
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
    // Extra virtual monitor: reuse the captured Samsung region on the left.
    const float extraTextureWidth = extraWidth / desktopWidth;
    glTexCoord2f(0, 0); glVertex2f(0, desktopY);
    glTexCoord2f(extraTextureWidth, 0); glVertex2f(extraWidth, desktopY);
    glTexCoord2f(extraTextureWidth, 1); glVertex2f(extraWidth, desktopY + desktopHeight);
    glTexCoord2f(0, 1); glVertex2f(0, desktopY + desktopHeight);

    // Captured desktop: Samsung followed by the existing right-hand region.
    glTexCoord2f(0, 0); glVertex2f(desktopX, desktopY);
    glTexCoord2f(1, 0); glVertex2f(desktopX + desktopWidth, desktopY);
    glTexCoord2f(1, 1); glVertex2f(desktopX + desktopWidth, desktopY + desktopHeight);
    glTexCoord2f(0, 1); glVertex2f(desktopX, desktopY + desktopHeight);
    glEnd();
    glDisable(GL_TEXTURE_2D);
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

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::printf("SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    DesktopImage desktop;
    if (!captureDesktop(desktop, "--fullscreen"))
    {
        SDL_Quit();
        return 1;
    }
    std::printf("Captured desktop: %dx%d\n", desktop.width, desktop.height);

    int targetDisplay = 0;
    const int displayCount = SDL_GetNumVideoDisplays();
    std::printf("Displays: %d\n", displayCount);
    for (int i = 0; i < displayCount; ++i)
    {
        const char *name = SDL_GetDisplayName(i);
        std::printf("  %d: %s\n", i, name ? name : "(unnamed)");
        if (name && (std::strcmp(name, "DP-3") == 0 ||
                     std::strstr(name, "SmartGlasses") != nullptr ||
                     std::strstr(name, "RayNeo") != nullptr))
            targetDisplay = i;
    }
    std::printf("Target display: %d: %s\n", targetDisplay,
                SDL_GetDisplayName(targetDisplay));

    SDL_Rect bounds{};
    SDL_GetDisplayBounds(targetDisplay, &bounds);
    SDL_Window *window = SDL_CreateWindow(
        "RayNeo pinned viewport", bounds.x, bounds.y, bounds.w, bounds.h,
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
        Rayneo_SetTargetVidPid(ctx, 0x1BBB, 0xAF50) != RAYNEO_OK ||
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
    GLuint desktopTexture = uploadDesktop(desktop);

    LiveCapture liveCapture;
    std::thread liveCaptureThread([&]() {
        std::printf("Live desktop capture started; updating the Samsung region\n");
        while (!liveCapture.stop.load())
        {
            DesktopImage frame;
            if (captureDesktop(frame, "--fullscreen"))
            {
                std::lock_guard<std::mutex> lock(liveCapture.mutex);
                liveCapture.pending = std::move(frame);
                liveCapture.ready = true;
                ++liveCapture.frames;
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });

    auto applyLiveFrame = [&](const DesktopImage &frame) {
        if (frame.width < width || frame.height != height ||
            desktop.width < width || desktop.height < height)
            return;
        for (int y = 0; y < height; ++y)
        {
            std::memcpy(desktop.pixels.data() + static_cast<size_t>(y) * desktop.width * 4,
                        frame.pixels.data() + static_cast<size_t>(y) * frame.width * 4,
                        static_cast<size_t>(width) * 4);
        }
        glBindTexture(GL_TEXTURE_2D, desktopTexture);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, desktop.width);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE, desktop.pixels.data());
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    };

    const float extraMonitorWidth = static_cast<float>(width);
    const float desktopX = extraMonitorWidth;
    const float canvasWidth = desktopX + static_cast<float>(desktop.width);
    const float canvasHeight = static_cast<float>(height) * 3.0f;
    const float desktopY = (canvasHeight - desktop.height) * 0.5f;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    Tracker tracker;
    bool running = true;
    uint64_t samples = 0;
    double lastReport = 0;
    while (running)
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
                if (captureDesktop(refreshed, "--fullscreen"))
                {
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
        const float maxYaw = 30.0f * 0.0174532925f;
        // A left head turn should reveal the left side of the anchored canvas.
        const float normalizedYaw = std::max(-1.0f, std::min(1.0f, -yaw / maxYaw));
        const float maxOffset = std::max(0.0f, canvasWidth - width);
        const float viewX = maxOffset * (0.5f + normalizedYaw * 0.5f);
        const float maxPitch = 20.0f * 0.0174532925f;
        const float normalizedPitch = std::max(-1.0f, std::min(1.0f, -tracker.orientation().pitch / maxPitch));
        const float maxOffsetY = std::max(0.0f, canvasHeight - height);
        const float viewY = maxOffsetY * (0.5f + normalizedPitch * 0.5f);

        glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(-viewX, -viewY, 0);
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
    Rayneo_DisableImu(ctx);
    Rayneo_Destroy(ctx);
    glDeleteTextures(1, &desktopTexture);
    SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
