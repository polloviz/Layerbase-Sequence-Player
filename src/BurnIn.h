#pragma once
#include <cstdint>
#include <string>

// Text burned into exported frames, one string per corner (empty = nothing there).
struct BurnInText {
    std::wstring topLeft, topRight, bottomLeft, bottomRight;
};

// Draws the texts over packed RGB / RGBA pixels (8 or 16 bits per channel, top row first)
// in the corners of the rectangle (rx, ry, rw, rh): white on a dark translucent box, sized
// to the rectangle. Opaque where drawn.
void DrawBurnIn(uint8_t* px, int width, int height, int channels, bool sixteenBit, int rx, int ry, int rw, int rh, const BurnInText& text);

// Paints everything outside the rectangle opaque black (letterbox / pillarbox bars).
void FillOutside(uint8_t* px, int width, int height, int channels, bool sixteenBit, int rx, int ry, int rw, int rh);

// "01:02:03:04" for a frame number counted at `fps` (rounded to whole frames per second).
std::string FrameTimecode(int frame, double fps);
