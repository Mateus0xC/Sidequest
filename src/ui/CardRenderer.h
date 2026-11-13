#pragma once
#include "Gfx.h"
#include "View.h"
#include <map>
#include <vector>

namespace sq {
struct Prim {
    enum Kind { Box, Text } kind = Box;
    D2D1_RECT_F r{};
    float radius = 0;
    D2D1_COLOR_F fill{}, border{};
    bool hasFill = false, hasBorder = false;
    ComPtr<IDWriteTextLayout> tl;
    D2D1_COLOR_F color{};
};

struct CardLayout {
    float w = 0, h = 0;
    float pad = 0;
    std::vector<Prim> prims;
    std::vector<D2D1_RECT_F> choiceRects;   // áreas clicáveis (coordenadas do cartão)
    D2D1_RECT_F inputRect{};
    bool hasInput = false;
};

class CardRenderer {
public:
    explicit CardRenderer(Gfx& g) : g_(g) {}
    // `width` = largura do cartão; `s` = escala (altura do monitor / 1080).
    CardLayout layout(const View& v, float width, float s);
    void draw(ID2D1DeviceContext* dc, const CardLayout& L);

private:
    ComPtr<IDWriteTextFormat> format(float size, DWRITE_FONT_WEIGHT weight);
    Gfx& g_;
    std::map<std::pair<int, int>, ComPtr<IDWriteTextFormat>> fmts_;
};
}  // namespace sq
