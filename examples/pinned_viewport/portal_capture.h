#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PortalFrame
{
    uint32_t nodeId{0};
    uint32_t sourceType{0};
    int width{0};
    int height{0};
    int logicalWidth{0};
    int logicalHeight{0};
    int stride{0};
    bool bgra{true};
    bool virtualOutput{false};
    std::vector<uint8_t> pixels;
};

class PortalCapture
{
public:
    PortalCapture();
    ~PortalCapture();

    PortalCapture(const PortalCapture &) = delete;
    PortalCapture &operator=(const PortalCapture &) = delete;

    bool start(std::string &error);
    void stop();
    bool takeFrame(std::size_t index, PortalFrame &frame);
    std::size_t streamCount() const;
    uint32_t sourceType(std::size_t index) const;
    bool isVirtualOutput(std::size_t index) const;
    void movePointerAbsolute(std::size_t index, double x, double y);
    void pointerButton(int button, bool pressed);

private:
    struct Impl;
    Impl *impl_;
};
