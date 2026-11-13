#include "CardRenderer.h"
#include <algorithm>

namespace sq {
namespace {
D2D1_COLOR_F rgb(unsigned v, float a = 1.f) {
    return D2D1::ColorF(((v >> 16) & 0xFF) / 255.f, ((v >> 8) & 0xFF) / 255.f, (v & 0xFF) / 255.f, a);
}
const D2D1_COLOR_F kCard = rgb(0x161A23), kCardBorder = rgb(0x2A3042), kText = rgb(0xE8EAF0),
                   kMuted = rgb(0x8A92A6), kAccent = rgb(0x5B8CFF), kRow = rgb(0x1E2330),
                   kSel = rgb(0x26324F), kOk = rgb(0x3DDC84), kBad = rgb(0xFF5C5C),
                   kOkBg = rgb(0x12351F), kBadBg = rgb(0x3A1818);
}  // namespace

ComPtr<IDWriteTextFormat> CardRenderer::format(float size, DWRITE_FONT_WEIGHT weight) {
    auto key = std::make_pair((int)(size * 10), (int)weight);
    auto it = fmts_.find(key);
    if (it != fmts_.end()) return it->second;
    ComPtr<IDWriteTextFormat> f;
    g_.dwrite->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                size, L"pt-BR", &f);
    fmts_[key] = f;
    return f;
}

CardLayout CardRenderer::layout(const View& v, float width, float s) {
    CardLayout L;
    const bool c = v.compact;
    const float pad = (c ? 18.f : 30.f) * s;
    const float inner = width - 2 * pad;
    L.w = width;
    L.pad = pad;
    float y = pad;

    auto text = [&](const std::wstring& t, float size, DWRITE_FONT_WEIGHT wt, D2D1_COLOR_F col, float x, float w) {
        Prim p;
        p.kind = Prim::Text;
        p.color = col;
        g_.dwrite->CreateTextLayout(t.c_str(), (UINT32)t.size(), format(size, wt).Get(), w, 10000.f, &p.tl);
        DWRITE_TEXT_METRICS m{};
        p.tl->GetMetrics(&m);
        p.r = D2D1::RectF(x, y, x + w, y + m.height);
        L.prims.push_back(p);
        return m.height;
    };
    auto box = [&](D2D1_RECT_F r, float radius, bool hasFill, D2D1_COLOR_F fill, bool hasBorder, D2D1_COLOR_F border) {
        Prim p;
        p.kind = Prim::Box;
        p.r = r; p.radius = radius; p.hasFill = hasFill; p.fill = fill; p.hasBorder = hasBorder; p.border = border;
        L.prims.push_back(p);
    };

    if (!v.tag.empty()) y += text(v.tag, (c ? 12.f : 14.f) * s, DWRITE_FONT_WEIGHT_SEMI_BOLD, kAccent, pad, inner) + 10 * s;
    if (!v.title.empty())
        y += text(v.title, (c ? 17.f : 27.f) * s, c ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_SEMI_BOLD, kText,
                  pad, inner) + (c ? 12.f : 20.f) * s;
    for (size_t li = 0; li < v.lines.size(); ++li) {
        const auto& ln = v.lines[li];
        if (ln.empty()) { y += 12 * s; continue; }
        char mk = li < v.lineMark.size() ? v.lineMark[li] : 0;
        y += text(ln, 20.f * s, DWRITE_FONT_WEIGHT_NORMAL, mk == 1 ? kOk : mk == 2 ? kBad : kText, pad, inner) + 4 * s;
    }

    int idx = 1;
    for (auto& ch : v.choices) {
        float badge = (c ? 24.f : 32.f) * s, rp = (c ? 8.f : 12.f) * s;
        float tx = pad + rp + badge + 10 * s, tw = width - pad - rp - tx;
        float top = y;
        // mede o texto para saber a altura da linha
        Prim probe;
        float th;
        {
            ComPtr<IDWriteTextLayout> tl;
            g_.dwrite->CreateTextLayout(ch.text.c_str(), (UINT32)ch.text.size(),
                                        format((c ? 16.f : 22.f) * s, DWRITE_FONT_WEIGHT_NORMAL).Get(), tw, 10000.f, &tl);
            DWRITE_TEXT_METRICS m{};
            tl->GetMetrics(&m);
            th = m.height;
        }
        float rowH = std::max(th, badge) + 2 * rp;
        D2D1_COLOR_F bg = kRow, bd = kCardBorder;
        bool border = false;
        if (ch.mark == ViewChoice::Correct) { bg = kOkBg; bd = kOk; border = true; }
        else if (ch.mark == ViewChoice::Wrong) { bg = kBadBg; bd = kBad; border = true; }
        else if (ch.mark == ViewChoice::Missed) { bd = kOk; border = true; }
        else if (ch.selected) { bg = kSel; bd = kAccent; border = true; }
        box(D2D1::RectF(pad, top, width - pad, top + rowH), 10 * s, true, bg, border, bd);
        L.choiceRects.push_back(D2D1::RectF(pad, top, width - pad, top + rowH));
        y = top + rp;
        std::wstring num = std::to_wstring(idx++);
        float bx = pad + rp;
        box(D2D1::RectF(bx, y, bx + badge, y + badge), 6 * s, true, ch.selected ? kAccent : rgb(0x2E3548), false, kCard);
        {
            float ty = y;
            y = top + rp + (badge - (c ? 15.f : 20.f) * s) / 2 - 2 * s;
            text(num, (c ? 14.f : 18.f) * s, DWRITE_FONT_WEIGHT_BOLD, kText, bx, badge);
            L.prims.back().tl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            y = ty;
        }
        y = top + rp + (rowH - 2 * rp - th) / 2;
        text(ch.text, (c ? 16.f : 22.f) * s, DWRITE_FONT_WEIGHT_NORMAL, kText, tx, tw);
        y = top + rowH + (c ? 6.f : 10.f) * s;
    }

    if (v.gap) {
        float h = (c ? 40.f : 56.f) * s;
        box(D2D1::RectF(pad, y, width - pad, y + h), 10 * s, true, kRow, true, v.inputActive ? kAccent : kCardBorder);
        L.inputRect = D2D1::RectF(pad, y, width - pad, y + h);
        L.hasInput = true;
        std::wstring shown = v.input.empty() && !v.inputActive ? v.placeholder : v.input;
        if (v.inputActive) shown += L"▏";
        float fs = (c ? 17.f : 24.f) * s;
        float oldY = y;
        y += (h - fs * 1.35f) / 2;
        text(shown, fs, DWRITE_FONT_WEIGHT_NORMAL, v.input.empty() && !v.inputActive ? kMuted : kText, pad + 14 * s,
             inner - 28 * s);
        y = oldY + h + 12 * s;
    }
    if (!v.verdict.empty())
        y += text(v.verdict, (c ? 16.f : 22.f) * s, DWRITE_FONT_WEIGHT_BOLD, v.verdictOk ? kOk : kBad, pad, inner) + 8 * s;
    if (!v.expected.empty())
        y += text(v.expected, (c ? 15.f : 20.f) * s, DWRITE_FONT_WEIGHT_SEMI_BOLD, kText, pad, inner) + 8 * s;
    if (!v.explanation.empty())
        y += text(v.explanation, (c ? 14.f : 19.f) * s, DWRITE_FONT_WEIGHT_NORMAL, rgb(0xB8BED0), pad, inner) + 12 * s;
    if (!v.hint.empty()) y += text(v.hint, (c ? 11.f : 14.f) * s, DWRITE_FONT_WEIGHT_NORMAL, kMuted, pad, inner);
    L.h = y + pad;
    return L;
}

void CardRenderer::draw(ID2D1DeviceContext* dc, const CardLayout& L) {
    ComPtr<ID2D1SolidColorBrush> br;
    dc->CreateSolidColorBrush(kCard, &br);
    float rad = std::min(L.pad * 0.6f, 18.f);
    D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, L.w - 0.5f, L.h - 0.5f), rad, rad);
    br->SetColor(kCard);
    dc->FillRoundedRectangle(bg, br.Get());
    br->SetColor(kCardBorder);
    dc->DrawRoundedRectangle(bg, br.Get(), 1.f);
    for (auto& p : L.prims) {
        if (p.kind == Prim::Box) {
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(p.r, p.radius, p.radius);
            if (p.hasFill) { br->SetColor(p.fill); dc->FillRoundedRectangle(rr, br.Get()); }
            if (p.hasBorder) { br->SetColor(p.border); dc->DrawRoundedRectangle(rr, br.Get(), 1.5f); }
        } else {
            br->SetColor(p.color);
            dc->DrawTextLayout(D2D1::Point2F(p.r.left, p.r.top), p.tl.Get(), br.Get());
        }
    }
}
}  // namespace sq
